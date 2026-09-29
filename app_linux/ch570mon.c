/*
 * ch570mon.c — htop 风格的终端监测/控制工具，用于 CH570 无线电流测量工具
 *              (Dongle 枚举出的双 CDC-ACM 串口)。
 *
 * 架构参照 htop：
 *   - 单线程事件循环：select() 轮询串口 fd + 节流的 ncurses 重绘（无多线程）
 *   - 模型/视图分离：遥测历史(Hist)、统计、透传回滚缓冲 与 渲染分开
 *   - Meter 式 sparkline（块字符滚动曲线）+ Panel 式透传滚动列表
 *   - termios 直接读写串口，ncursesw 负责 UI，除 libcurses/libm 外无依赖
 *
 * 数据来源（复用现有固件协议，无需改固件）：
 *   COM2(控制/遥测口): 读 "V:5.012V, I:123.125mA, P:617.16mW" / "RSSI:-68 dBm"
 *                      / "[VER] .." / "[CFG] .."；写 "CMD:RST/BOOT/SWAP/VER?/CFG?"
 *   COM1(透传口)     : 目标板原始串口字节流，双向
 *
 * 构建：make            (链接 -lncursesw，回退 -lncurses)
 * 运行：./ch570mon [--control /dev/ttyACM1] [--passthrough /dev/ttyACM0] [--baud 115200]
 *
 * 按键：r=复位  b=进烧录  s=TX/RX换向  v=查版本  c=查配置
 *       1/2/3=聚焦 电流/功率/电压 统计   p=暂停曲线
 *       :=向COM2发命令   t=向COM1(目标板)发数据
 *       PgUp/PgDn/↑/↓=滚动透传区   Ctrl-L=重绘   q=退出
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <locale.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <glob.h>
#include <ncurses.h>

/* ---------------- 可调参数 ---------------- */
#define HIST_CAP     4096      /* 每个指标保留的采样点数 */
#define PB_CAP       2000      /* 透传回滚行数 */
#define PB_LEN       512       /* 每行最大字节 */
#define SPARK_MAXW   900       /* sparkline 最大列数（防缓冲溢出） */
#define REFRESH_MS   33        /* 重绘节流 ~30fps */
#define SELECT_MS    50        /* select 超时（同时决定按键轮询间隔） */

/* 颜色对 */
enum { CP_I = 1, CP_P, CP_V, CP_ACCENT, CP_DIM };

/* ---------------- 模型：历史环形缓冲 ---------------- */
typedef struct {
    double v[HIST_CAP];
    int n;      /* 已存数量 */
    int head;   /* 下一个写入位置 */
} Hist;

static Hist hI, hP, hV;

static void histPush(Hist *h, double x) {
    h->v[h->head] = x;
    h->head = (h->head + 1) % HIST_CAP;
    if (h->n < HIST_CAP) h->n++;
}
static double histGet(const Hist *h, int i) {   /* i: 0=最旧 .. n-1=最新 */
    int idx = (h->head - h->n + i) % HIST_CAP;
    if (idx < 0) idx += HIST_CAP;
    return h->v[idx];
}
static void histStats(const Hist *h, double *mn, double *av, double *mx) {
    if (h->n == 0) { *mn = *av = *mx = 0.0; return; }
    double lo = histGet(h, 0), hi = lo, s = 0.0;
    for (int i = 0; i < h->n; i++) {
        double x = histGet(h, i);
        s += x;
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    *mn = lo; *mx = hi; *av = s / h->n;
}

/* ---------------- 模型：运行状态 ---------------- */
static double curV = 0, curI = 0, curP = 0;
static int    curRssi = 0, hasRssi = 0;
static unsigned long telemCount = 0;
static char   fwDongle[64] = "", fwProbe[64] = "", cfgLine[160] = "";
static char   lastMsg[220] = "";
static int    paused = 0;
static int    focusMetric = 0;              /* 0=I 1=P 2=V */
static int    inputMode = 0;                /* 0=无 1=COM2 2=COM1 */
static char   inputBuf[256];
static int    inputLen = 0;
static volatile sig_atomic_t running = 1;

/* ---------------- 模型：透传回滚（Panel） ---------------- */
static char *pbLines = NULL;                /* PB_CAP * PB_LEN */
static int   pbHead = 0, pbCount = 0, pbScroll = 0;
#define PB(i) (&pbLines[(size_t)(i) * PB_LEN])

static void pbPushLine(const char *s) {
    snprintf(PB(pbHead), PB_LEN, "%s", s);
    pbHead = (pbHead + 1) % PB_CAP;
    if (pbCount < PB_CAP) pbCount++;
}
static const char *pbGet(int i) {             /* i: 0=最旧 */
    int idx = (pbHead - pbCount + i) % PB_CAP;
    if (idx < 0) idx += PB_CAP;
    return PB(idx);
}

/* ---------------- 串口 (termios) ---------------- */
static int   fdCtrl = -1, fdPass = -1;
static char  ctrlPath[64] = "", passPath[64] = "";
static int   passBaud = 115200;

static speed_t baudToSpeed(int b) {
    switch (b) {
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 230400:  return B230400;
        case 460800:  return B460800;
        case 921600:  return B921600;
        case 1000000: return B1000000;
        case 115200:
        default:      return B115200;
    }
}

static int serial_open(const char *path, int baud) {
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        speed_t sp = baudToSpeed(baud);
        cfsetispeed(&t, sp);
        cfsetospeed(&t, sp);
        t.c_cflag |= (CLOCAL | CREAD);
        t.c_cflag &= ~CRTSCTS;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSANOW, &t);
    }
    /* 显式拉起 DTR+RTS（两者同高），避免落入固件 esptool 自动复位/烧录判定
     * (DTR=1,RTS=0 → BOOT; DTR=0,RTS=1 → RESET)。 */
    int bits = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIS, &bits);
    return fd;
}

static void serial_write_line(int fd, const char *s) {
    if (fd < 0) { snprintf(lastMsg, sizeof(lastMsg), "端口未连接"); return; }
    ssize_t n = write(fd, s, strlen(s));
    (void)n;
    n = write(fd, "\r\n", 2);
    (void)n;
}

/* ---------------- 端口自动探测 ---------------- */
static int listPorts(char out[][64], int max) {
    int c = 0;
    glob_t g;
    if (glob("/dev/ttyACM*", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && c < max; i++)
            strncpy(out[c++], g.gl_pathv[i], 63);
        globfree(&g);
    }
    if (glob("/dev/ttyUSB*", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && c < max; i++)
            strncpy(out[c++], g.gl_pathv[i], 63);
        globfree(&g);
    }
    out[c < max ? c : max - 1][63] = '\0';
    return c;
}

/* 打开发 CMD:CFG?，看是否回 [CFG]/[VER]，用于识别控制口(COM2)。 */
static int probeIsControl(const char *path) {
    int fd = serial_open(path, 115200);
    if (fd < 0) return 0;
    const char *q = "CMD:CFG?\r\n";
    ssize_t w = write(fd, q, strlen(q));
    (void)w;
    char buf[256];
    int got = 0;
    int isCtrl = 0;
    for (int t = 0; t < 6 && !isCtrl; t++) {
        fd_set s; FD_ZERO(&s); FD_SET(fd, &s);
        struct timeval tv = { 0, 50 * 1000 };
        if (select(fd + 1, &s, NULL, NULL, &tv) > 0) {
            int n = (int)read(fd, buf + got, sizeof(buf) - 1 - (size_t)got);
            if (n > 0) {
                got += n; buf[got] = '\0';
                if (strstr(buf, "[CFG]") || strstr(buf, "[VER]")) isCtrl = 1;
            }
        }
    }
    close(fd);
    return isCtrl;
}

/* ---------------- COM2 行解析 ---------------- */
static void parseCtrlLine(const char *line) {
    double v, i, p;
    int got = sscanf(line, "V:%lfV, I:%lfmA, P:%lfmW", &v, &i, &p);
    if (got >= 2) {
        if (got == 2) p = v * i;
        curV = v; curI = i; curP = p;
        if (!paused) { histPush(&hI, i); histPush(&hP, p); histPush(&hV, v); }
        telemCount++;
        return;
    }
    int r;
    if (sscanf(line, "RSSI:%d", &r) == 1) { curRssi = r; hasRssi = 1; return; }

    if (strstr(line, "[VER]")) {
        const char *col = strchr(line, ':');
        if (col) {
            char tmp[64];
            strncpy(tmp, col + 1, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            char *s = tmp;
            while (*s == ' ' || *s == '\t') s++;
            size_t L = strlen(s);
            while (L && (s[L-1] == ' ' || s[L-1] == '\t' || s[L-1] == '\r')) s[--L] = '\0';
            if (strstr(line, "Probe")) { strncpy(fwProbe, s, sizeof(fwProbe) - 1); fwProbe[sizeof(fwProbe)-1] = '\0'; }
            else                        { strncpy(fwDongle, s, sizeof(fwDongle) - 1); fwDongle[sizeof(fwDongle)-1] = '\0'; }
        }
        snprintf(lastMsg, sizeof(lastMsg), "%.*s", (int)sizeof(lastMsg) - 1, line);
        return;
    }
    if (strstr(line, "[CFG]")) {
        snprintf(cfgLine, sizeof(cfgLine), "%.*s", (int)sizeof(cfgLine) - 1, line);
        snprintf(lastMsg, sizeof(lastMsg), "%.*s", (int)sizeof(lastMsg) - 1, line);
        return;
    }
    snprintf(lastMsg, sizeof(lastMsg), "%.*s", (int)sizeof(lastMsg) - 1, line);
}

/* ---------------- 抽干串口 ---------------- */
static char ctrlAcc[512]; static int ctrlAccLen = 0;
static void drainCtrl(void) {
    char buf[512];
    for (;;) {
        ssize_t n = read(fdCtrl, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t k = 0; k < n; k++) {
            char c = buf[k];
            if (c == '\n') { ctrlAcc[ctrlAccLen] = '\0'; parseCtrlLine(ctrlAcc); ctrlAccLen = 0; }
            else if (c == '\r') { /* skip */ }
            else if (ctrlAccLen < (int)sizeof(ctrlAcc) - 1) ctrlAcc[ctrlAccLen++] = c;
            else ctrlAccLen = 0;
        }
    }
}

static char passAcc[PB_LEN]; static int passAccLen = 0;
static void drainPass(void) {
    char buf[1024];
    for (;;) {
        ssize_t n = read(fdPass, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t k = 0; k < n; k++) {
            char c = buf[k];
            if (c == '\n') { passAcc[passAccLen] = '\0'; pbPushLine(passAcc); passAccLen = 0; }
            else if (c == '\r') { /* skip */ }
            else if (passAccLen < PB_LEN - 1) passAcc[passAccLen++] = c;
            else { passAcc[passAccLen] = '\0'; pbPushLine(passAcc); passAccLen = 0; }
        }
    }
}

/* ---------------- 视图 (ncurses) ---------------- */
static const char *SPARK[8] = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };

static int panelTop(void)  { return 8; }
static int panelRows(void) { int r = (LINES - 3) - panelTop() + 1; return r < 1 ? 1 : r; }
static int pbMaxScroll(void) { int m = pbCount - panelRows(); return m < 0 ? 0 : m; }

static void drawSpark(int row, int pair, const Hist *h, const char *label, double last, const char *fmt) {
    int colStart = 11;
    int width = COLS - colStart - 14;
    if (width < 4) width = 4;
    if (width > SPARK_MAXW) width = SPARK_MAXW;

    mvprintw(row, 1, "%s", label);

    int n = h->n;
    int start = n > width ? n - width : 0;
    int cnt = n - start;
    double mn = 0, mx = 0;
    if (cnt > 0) {
        mn = mx = histGet(h, start);
        for (int i = start; i < n; i++) { double x = histGet(h, i); if (x < mn) mn = x; if (x > mx) mx = x; }
    }
    static char buf[3 * SPARK_MAXW + 8];
    int off = 0;
    for (int s = 0; s < cnt; s++) {
        double x = histGet(h, start + s);
        int lvl = (mx > mn) ? (int)((x - mn) / (mx - mn) * 7.0 + 0.5) : 0;
        if (lvl < 0) lvl = 0;
        if (lvl > 7) lvl = 7;
        const char *ch = SPARK[lvl];
        size_t cl = strlen(ch);
        memcpy(buf + off, ch, cl);
        off += (int)cl;
    }
    buf[off] = '\0';
    attron(COLOR_PAIR(pair));
    mvaddstr(row, colStart, buf);
    attroff(COLOR_PAIR(pair));

    attron(COLOR_PAIR(pair) | A_BOLD);
    mvprintw(row, colStart + width + 1, fmt, last);
    attroff(COLOR_PAIR(pair) | A_BOLD);
}

static void render(void) {
    int H = LINES, W = COLS;
    if (H < 12 || W < 46) {
        erase();
        mvprintw(0, 0, "终端太小，请放大窗口 (需 >= 46x12)");
        refresh();
        return;
    }
    erase();

    /* 行0：标题/状态条 */
    attron(A_REVERSE);
    mvprintw(0, 0, "%-*s", W, "");
    mvprintw(0, 0, " CH570 无线电流监测 ");
    {
        char right[192];
        snprintf(right, sizeof(right), "Ctrl:%s  Pass:%s  pkts:%lu ",
                 fdCtrl >= 0 ? ctrlPath : "-", fdPass >= 0 ? passPath : "-", telemCount);
        int x = W - (int)strlen(right);
        if (x < 21) x = 21;
        mvprintw(0, x, "%s", right);
    }
    attroff(A_REVERSE);

    /* 行1：实时数值 */
    mvprintw(1, 1, "V");
    attron(COLOR_PAIR(CP_V) | A_BOLD); mvprintw(1, 3, "%8.3f", curV); attroff(COLOR_PAIR(CP_V) | A_BOLD);
    mvprintw(1, 12, "V   I");
    attron(COLOR_PAIR(CP_I) | A_BOLD); mvprintw(1, 16, "%9.3f", curI); attroff(COLOR_PAIR(CP_I) | A_BOLD);
    mvprintw(1, 26, "mA   P");
    attron(COLOR_PAIR(CP_P) | A_BOLD); mvprintw(1, 31, "%9.2f", curP); attroff(COLOR_PAIR(CP_P) | A_BOLD);
    mvprintw(1, 41, "mW   RSSI");
    attron(COLOR_PAIR(CP_ACCENT)); mvprintw(1, 50, "%4d", hasRssi ? curRssi : 0); attroff(COLOR_PAIR(CP_ACCENT));
    mvprintw(1, 55, "dBm");

    /* 行2：聚焦指标统计 */
    {
        const char *names[3] = { "电流 I (mA)", "功率 P (mW)", "电压 V (V)" };
        Hist *hs[3] = { &hI, &hP, &hV };
        double mn, av, mx;
        histStats(hs[focusMetric], &mn, &av, &mx);
        attron(A_BOLD);
        mvprintw(2, 1, "%s", names[focusMetric]);
        attroff(A_BOLD);
        printw("   min %.3f   avg %.3f   max %.3f   样本 %d %s",
               mn, av, mx, hs[focusMetric]->n, paused ? " [已暂停]" : "");
    }

    /* 行3：固件版本 + 配置 */
    attron(COLOR_PAIR(CP_DIM));
    mvprintw(3, 1, "固件 D:%s  P:%s",
             fwDongle[0] ? fwDongle : "--", fwProbe[0] ? fwProbe : "--");
    if (cfgLine[0]) mvaddnstr(3, 34, cfgLine, W - 35);
    attroff(COLOR_PAIR(CP_DIM));

    /* 行4/5/6：三条 sparkline */
    drawSpark(4, CP_I, &hI, "电流 mA", curI, "%9.3f");
    drawSpark(5, CP_P, &hP, "功率 mW", curP, "%9.2f");
    drawSpark(6, CP_V, &hV, "电压 V ", curV, "%9.3f");

    /* 行7：分隔 */
    attron(COLOR_PAIR(CP_DIM));
    mvprintw(7, 1, "──── 目标串口透传 (COM1) %s ────", fdPass >= 0 ? passPath : "[未连接]");
    attroff(COLOR_PAIR(CP_DIM));

    /* 行8..H-3：透传回滚 */
    {
        int pr = panelRows();
        int maxScr = pbMaxScroll();
        if (pbScroll > maxScr) pbScroll = maxScr;
        for (int r = 0; r < pr; r++) {
            int fromEnd = (pr - 1 - r) + pbScroll;   /* 0=最新 */
            int i = pbCount - 1 - fromEnd;
            if (i >= 0 && i < pbCount)
                mvaddnstr(panelTop() + r, 1, pbGet(i), W - 2);
        }
        if (pbScroll > 0) {
            attron(A_REVERSE);
            mvprintw(panelTop(), W - 12, " ↑%d ", pbScroll);
            attroff(A_REVERSE);
        }
    }

    /* 行H-2：按键帮助条 */
    attron(A_REVERSE);
    mvprintw(H - 2, 0, "%-*s", W, "");
    mvprintw(H - 2, 0, " r:复位 b:烧录 s:换向 v:版本 c:配置  1/2/3:聚焦 p:暂停  ::COM2命令 t:发COM1  PgUp/Dn:滚动 q:退出 ");
    attroff(A_REVERSE);

    /* 行H-1：输入提示 / 最近消息 */
    if (inputMode) {
        mvprintw(H - 1, 0, "%s> %s", inputMode == 1 ? "COM2" : "COM1", inputBuf);
    } else {
        attron(COLOR_PAIR(CP_DIM));
        mvaddnstr(H - 1, 0, lastMsg, W - 1);
        attroff(COLOR_PAIR(CP_DIM));
    }

    refresh();
}

/* ---------------- 输入模式 & 按键 ---------------- */
static void setMsg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(lastMsg, sizeof(lastMsg), fmt, ap);
    va_end(ap);
}
static void startInput(int mode) { inputMode = mode; inputLen = 0; inputBuf[0] = '\0'; curs_set(1); }
static void endInput(void)       { inputMode = 0; curs_set(0); }
static void commitInput(void) {
    inputBuf[inputLen] = '\0';
    if (inputLen > 0) {
        if (inputMode == 1) { serial_write_line(fdCtrl, inputBuf); setMsg("→ COM2: %s", inputBuf); }
        else                { serial_write_line(fdPass, inputBuf); setMsg("→ COM1: %s", inputBuf); }
    }
    endInput();
}

static void handleKey(int ch) {
    if (inputMode) {
        if (ch == 27) endInput();
        else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) commitInput();
        else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) { if (inputLen > 0) inputBuf[--inputLen] = '\0'; }
        else if (ch >= 32 && ch < 127) { if (inputLen < (int)sizeof(inputBuf) - 1) { inputBuf[inputLen++] = (char)ch; inputBuf[inputLen] = '\0'; } }
        return;
    }
    switch (ch) {
        case 'r': serial_write_line(fdCtrl, "CMD:RST");  setMsg("→ CMD:RST 复位目标板"); break;
        case 'b': serial_write_line(fdCtrl, "CMD:BOOT"); setMsg("→ CMD:BOOT 进入烧录模式"); break;
        case 's': serial_write_line(fdCtrl, "CMD:SWAP"); setMsg("→ CMD:SWAP 交换 TX/RX"); break;
        case 'v': serial_write_line(fdCtrl, "CMD:VER?"); setMsg("→ CMD:VER? 查询固件版本"); break;
        case 'c': serial_write_line(fdCtrl, "CMD:CFG?"); setMsg("→ CMD:CFG? 查询配置"); break;
        case 'p': paused = !paused; setMsg(paused ? "曲线已暂停" : "曲线继续"); break;
        case '1': focusMetric = 0; break;
        case '2': focusMetric = 1; break;
        case '3': focusMetric = 2; break;
        case ':': case '/': startInput(1); break;
        case 't': startInput(2); break;
        case KEY_UP:    if (pbScroll < pbMaxScroll()) pbScroll++; break;
        case KEY_DOWN:  if (pbScroll > 0) pbScroll--; break;
        case KEY_PPAGE: pbScroll += panelRows(); if (pbScroll > pbMaxScroll()) pbScroll = pbMaxScroll(); break;
        case KEY_NPAGE: pbScroll -= panelRows(); if (pbScroll < 0) pbScroll = 0; break;
        case 12:        clear(); break;                         /* Ctrl-L 重绘 */
        case KEY_RESIZE: clear(); break;                        /* 终端尺寸变化 */
        case 'q': case 'Q': running = 0; break;
        default: break;
    }
}

/* ---------------- 信号 & 清理 ---------------- */
static void onSignal(int s) { (void)s; running = 0; }

static void cleanup(void) {
    endwin();
    if (fdCtrl >= 0) close(fdCtrl);
    if (fdPass >= 0) close(fdPass);
    free(pbLines);
}

static void usage(const char *prog) {
    printf("用法: %s [选项]\n"
           "  --control <dev>      指定控制/遥测口 (COM2)，默认自动探测\n"
           "  --passthrough <dev>  指定透传口 (COM1)，默认自动探测\n"
           "  --baud <n>           透传口波特率 (默认 115200)\n"
           "  -h, --help           显示本帮助\n", prog);
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");

    for (int a = 1; a < argc; a++) {
        if ((!strcmp(argv[a], "--control") || !strcmp(argv[a], "-c")) && a + 1 < argc)
            strncpy(ctrlPath, argv[++a], sizeof(ctrlPath) - 1);
        else if ((!strcmp(argv[a], "--passthrough") || !strcmp(argv[a], "-p")) && a + 1 < argc)
            strncpy(passPath, argv[++a], sizeof(passPath) - 1);
        else if (!strcmp(argv[a], "--baud") && a + 1 < argc)
            passBaud = atoi(argv[++a]);
        else if (!strcmp(argv[a], "-h") || !strcmp(argv[a], "--help")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "未知参数: %s\n", argv[a]); usage(argv[0]); return 1; }
    }

    pbLines = calloc((size_t)PB_CAP * PB_LEN, 1);
    if (!pbLines) { fprintf(stderr, "内存分配失败\n"); return 1; }

    /* 端口探测 */
    char ports[16][64];
    int np = listPorts(ports, 16);
    if (ctrlPath[0] == '\0') {
        for (int i = 0; i < np; i++)
            if (probeIsControl(ports[i])) { snprintf(ctrlPath, sizeof(ctrlPath), "%.*s", (int)sizeof(ctrlPath) - 1, ports[i]); break; }
    }
    if (passPath[0] == '\0') {
        for (int i = 0; i < np; i++)
            if (strcmp(ports[i], ctrlPath) != 0) { snprintf(passPath, sizeof(passPath), "%.*s", (int)sizeof(passPath) - 1, ports[i]); break; }
    }
    if (ctrlPath[0] == '\0') {
        fprintf(stderr, "未找到 CH570 控制口 (COM2)。请检查连接，或用 --control /dev/ttyACMx 指定。\n"
                        "提示：控制口会对 'CMD:CFG?' 回应 '[CFG] ...'。\n");
        free(pbLines);
        return 1;
    }

    fdCtrl = serial_open(ctrlPath, 115200);
    if (fdCtrl < 0) { fprintf(stderr, "打不开控制口 %s: %s\n", ctrlPath, strerror(errno)); free(pbLines); return 1; }
    if (passPath[0]) {
        fdPass = serial_open(passPath, passBaud);
        if (fdPass < 0) fprintf(stderr, "警告: 打不开透传口 %s: %s（继续，仅监测）\n", passPath, strerror(errno));
    }

    /* 连接后自动查询版本与配置 */
    serial_write_line(fdCtrl, "CMD:VER?");
    serial_write_line(fdCtrl, "CMD:CFG?");

    /* ncurses 初始化 */
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_I,      COLOR_GREEN,   -1);
        init_pair(CP_P,      COLOR_CYAN,    -1);
        init_pair(CP_V,      COLOR_YELLOW,  -1);
        init_pair(CP_ACCENT, COLOR_MAGENTA, -1);
        init_pair(CP_DIM,    COLOR_BLUE,    -1);
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    setMsg("已连接 %s%s%s", ctrlPath,
           fdPass >= 0 ? " + " : "", fdPass >= 0 ? passPath : "");

    /* ---------------- 单线程事件循环（htop 风格） ---------------- */
    struct timespec lastRender = { 0, 0 };
    while (running) {
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;
        if (fdCtrl >= 0) { FD_SET(fdCtrl, &rfds); if (fdCtrl > maxfd) maxfd = fdCtrl; }
        if (fdPass >= 0) { FD_SET(fdPass, &rfds); if (fdPass > maxfd) maxfd = fdPass; }
        struct timeval tv = { 0, SELECT_MS * 1000 };
        int s = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (s < 0 && errno != EINTR) { /* 忽略瞬时错误 */ }

        if (fdCtrl >= 0) drainCtrl();
        if (fdPass >= 0) drainPass();

        int ch;
        while ((ch = getch()) != ERR) { handleKey(ch); if (!running) break; }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec - lastRender.tv_sec) * 1000 +
                  (now.tv_nsec - lastRender.tv_nsec) / 1000000;
        if (ms >= REFRESH_MS || lastRender.tv_sec == 0) { render(); lastRender = now; }
    }

    cleanup();
    printf("已退出 ch570mon。\n");
    return 0;
}
