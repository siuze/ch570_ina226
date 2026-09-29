# ch570mon — Linux 终端交互式监测工具

htop 风格的终端工具，用于监测 CH570 无线电流测量工具的 **电压 / 电流 / 功率**，
在终端里画滚动曲线，并对目标板做 **串口透传打印** 与 **复位 / 进入烧录** 等操作。

设计参照 [htop](https://htop.dev/)：**C + ncurses(w)**、**单线程 `select()` 事件循环**、
模型/视图分离、Meter 式 sparkline、Panel 式滚动列表。除 `libncursesw`/`libm` 外无运行时依赖。

## 依赖与构建

```bash
# Debian/Ubuntu
sudo apt install libncursesw5-dev build-essential
# Fedora: sudo dnf install ncurses-devel ;  Arch: sudo pacman -S ncurses base-devel

make            # 生成 ./ch570mon
```

## 运行

把 Dongle 插入 Linux 后，双 CDC-ACM 会枚举成两个 `/dev/ttyACM*`：

```bash
./ch570mon                                   # 自动探测控制口与透传口
./ch570mon --control /dev/ttyACM1 --passthrough /dev/ttyACM0 --baud 115200
```

- **控制口 (COM2)**：工具逐个候选口发 `CMD:CFG?`，收到 `[CFG]`/`[VER]` 应答的即为控制口。
- **透传口 (COM1)**：另一个口，用于目标板串口双向透传；`--baud` 会经 Dongle 同步到 Probe。

> 也可用 udev 规则按接口字符串固定符号链接（`CH570 Wireless UART` / `CH570 Sensor Control`），
> 再把 `--control`/`--passthrough` 指向固定名字，避免 ttyACM 编号漂移。

## 界面

```
 CH570 无线电流监测            Ctrl:/dev/ttyACM1  Pass:/dev/ttyACM0  pkts:1234
 V   5.012 V   I  123.125 mA   P   617.16 mW   RSSI  -68 dBm
 电流 I (mA)   min 12.000   avg 120.500   max 410.000   样本 842
 固件 D:2026-09-29 r01  P:2026-09-29 r01   [CFG] FSC=1000mA, RATE=100ms ...
 电流 mA   ▁▂▃▅▇█▇▅▃▂▁▂▃▅▇█▇▅▃▂▁▂▃▅▇█▇▅▃▂▁▂▃▅▇█▇▅▃▂▁   123.125
 功率 mW   ▁▂▃▅▇█▇▅▃▂▁▂▃▅▇█▇▅▃▂▁▂▃▅▇█▇▅▃▂▁▂▃▅▇█▇▅▃▂▁   617.16
 电压 V    ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔     5.012
 ──── 目标串口透传 (COM1) /dev/ttyACM0 ────
 <目标板打印输出在此滚动>
 r:复位 b:烧录 s:换向 v:版本 c:配置  1/2/3:聚焦 p:暂停  ::COM2命令 t:发COM1  PgUp/Dn:滚动 q:退出
```

## 按键（即“按钮”）

| 键 | 动作 | 发送 |
| --- | --- | --- |
| `r` | 目标板复位 | `CMD:RST` → COM2 |
| `b` | 进入 Bootloader（烧录） | `CMD:BOOT` → COM2 |
| `s` | 交换 Probe TX/RX | `CMD:SWAP` → COM2 |
| `v` | 查询固件版本 | `CMD:VER?` → COM2 |
| `c` | 查询配置 | `CMD:CFG?` → COM2 |
| `1`/`2`/`3` | 聚焦 电流/功率/电压 的 min/avg/max 统计 | — |
| `p` | 暂停/继续曲线累积 | — |
| `:` | 输入任意 COM2 命令（回车发送） | → COM2 |
| `t` | 输入数据发给目标板（回车发送） | → COM1 |
| `PgUp`/`PgDn`/`↑`/`↓` | 滚动透传回滚区 | — |
| `Ctrl-L` | 重绘 | — |
| `q` | 退出 | — |

## 协议（复用现有固件，无需改固件）

- 遥测行：`V:5.012V, I:123.125mA, P:617.16mW`
- 信号：`RSSI:-68 dBm`
- 版本：`[VER] Dongle: 2026-09-29 r01` / `[VER] Probe:  2026-09-29 r01`
- 配置：`[CFG] FSC=..mA, RATE=..ms, SHUNT=..uOhm, SWAP=..`
- 控制：`CMD:RST` / `CMD:BOOT` / `CMD:SWAP` / `CMD:VER?` / `CMD:CFG?`（以 `\r\n` 结尾）

## 说明与局限

- 打开串口时显式拉起 `DTR+RTS`（两者同高），避免触发固件的 esptool 自动复位/烧录判定。
- 曲线为窗口内自动量程的 sparkline；暂停（`p`）后停止累积但仍在数值行显示最新值。
- 若终端非 UTF-8 或未链接 `ncursesw`，块字符曲线可能显示异常，请用 UTF-8 locale 运行。
