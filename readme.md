# CH570 无线串口 & 电流测量工具

一款在**电脑**与**目标 MCU** 之间实现全双工无线串口透传、并带高侧**电流 / 电压 / 功率测量**的开源硬件工具。成对使用：

- **Dongle（电脑端）**：USB 插入电脑，枚举为**两个虚拟串口**（COM1 透传 / COM2 遥测与控制），经 2.4G 与 Probe 通信。
- **Probe（探针端）**：UART 接目标板，串接 20mΩ 采样电阻 + INA226 测电流，经 2.4G 与 Dongle 通信；未绑定时降级为极简 BLE 广播遥测。

两端主控均为沁恒 RISC-V 无线 SoC **CH570Q**（DFN-10 3×3，2.4G 专有链路，"一碰连"自动配对）。

> 开源硬件项目页：[ch570q-uart1](https://oshwhub.com/fikasgroup/ch570q-uart1)（作者 fikasgroup，立创开源硬件平台）

![](https://image.lceda.cn/oshwhub/pullImage/f7ea1a1f6e5f42ecbac79c3ca6b3ffbb.png)

---

## 目录

- [项目结构](#项目结构)
- [环境要求](#环境要求)
- [一、编译固件](#一编译固件)
- [二、烧录固件](#二烧录固件)
- [三、上位机（运行方法）](#三上位机运行方法)
- [COM2 控制指令](#com2-控制指令)
- [工作原理](#工作原理)
- [文档与资料](#文档与资料)

---

## 项目结构

```text
CH570Q/
├── RF/                       # 固件源码（两端）
│   ├── RF_Uart/              #   Probe 探针端：UART 透传 + INA226 电流测量 + 2.4G 收发
│   ├── RF_UartDongle/        #   Dongle 电脑端：USB 双 CDC-ACM 复合设备 + 2.4G 收发
│   └── LIB/                  #   2.4G 射频库头文件 CH572rf.h（预编译库 libCH57xRF.a 需自备，见下）
├── SRC/                      # CH57x 标准外设库 + 启动 + 链接脚本（两端共用）
│   ├── StdPeriphDriver/      #   外设驱动；inc/ISP572.h 提供 Flash-ROM 擦写例程头
│   ├── Startup/startup_CH572.S
│   ├── RVMSIS/core_riscv.h
│   └── Ld/Link.ld            #   Flash: 0x00000000 / 240K，RAM: 0x20000000 / 12K
├── app_win/                  # Windows 上位机（Dear ImGui + DirectX11 + C++/WinRT BLE + SetupAPI）
├── app_linux/                # Linux 终端上位机 ch570mon（C + ncursesw，htop 风格）
├── docs/                     # 规格书与设计文档（CH572DS1.PDF、设计说明、项目描述等）
├── tools/
│   └── build_linux.sh        # Linux 下的固件编译校验脚本（xPack riscv-none-elf-gcc）
└── readme.md
```

> `tmp/` 为本地调试脚本与原理图/网表收容目录，已在 `.gitignore` 中忽略，不入库。

---

## 环境要求

| 组件 | 平台 | 依赖 |
|---|---|---|
| **固件**（生成 `.hex`） | Windows | MounRiver Studio（自带 `riscv-wch-elf-gcc` + `make`）；链接需 WCH 专有库 `libCH57xRF.a`、`libISP572.a`（见下方说明） |
| **固件**（仅编译校验） | Linux | `tools/xpack-riscv-none-elf-gcc-*/`（xPack riscv-none-elf-gcc，linux-x64，需自备）、`bash`、`make` |
| **app_win** 上位机 | Windows 10/11 | Visual Studio 2022（含"使用 C++ 的桌面开发"）、CMake、DirectX11、C++/WinRT（均随 VS/Windows SDK 提供） |
| **app_linux** 上位机 | Linux | `gcc`、`make`、`pkg-config`、`libncursesw5-dev`（宽字符 ncurses） |
| **烧录** | Windows | [WCHISPTool](http://www.wch.cn/downloads/WCHISPTool_Setup_exe.html)（USB / 串口 ISP，免额外调试器） |

---

## 一、编译固件

固件分两端：`RF/RF_Uart`（Probe）与 `RF/RF_UartDongle`（Dongle）。

### Windows —— 生成可烧录的 `.hex`（推荐）

用 **MounRiver Studio** 打开 `RF/RF_Uart`（Probe）或 `RF/RF_UartDongle`（Dongle）工程，直接 Build；产物在各自 `obj/` 下（`RF_Uart.hex` / `RF_UartDongle.hex`）。MounRiver 自带 WCH 版 `riscv-wch-elf-gcc`、`make` 与下述专有库，是目前唯一能产出可烧录 `.hex` 的路径。

> **专有库说明**：最终链接需要 WCH 的两个预编译库——`libCH57xRF.a`（2.4G 射频协议栈）与 `libISP572.a`（Flash-ROM 擦写例程）。二者**不在本仓库**，随官方 **CH57x EVT/SDK** 分发；使用 MounRiver 官方工程模板即已包含。缺少它们只能编译、不能生成 `.hex`。

### Linux —— 编译校验（不产出 `.hex`）

先把 xPack **riscv-none-elf-gcc（linux-x64）** 解压到 `tools/xpack-riscv-none-elf-gcc-*/`，再运行：

```bash
bash tools/build_linux.sh     # 用标准 RISC-V GCC 编译两端全部翻译单元
```

`build_linux.sh` 以不含 WCH 私有 `xw` 扩展的 `-march` 编译，可完整校验所有源码；因缺少上述两个 `.a`，**不做最终链接**（预期仅剩 WCH `__INTERRUPT` 属性在标准 GCC 下的告警）。

---

## 二、烧录固件

烧录统一使用官方 **WCHISPTool**，支持 USB ISP 与串口（UART）ISP，**无需 WCH-Link/SWD 调试器**。

### 背景：CH570Q 的 ISP 进入机制

CH570Q 属 `ch57x_gen2`，其出厂 Bootloader（独立 8KB 系统引导区）的进入条件如下：

- **引脚**：USB ISP 在 `PA0=D- / PA1=D+`；UART ISP 在 `PA0=RX / PA1=TX`。
- **空片即进 Bootloader**：当 **Flash 0 地址为擦除态（`0xFFFFFFFF`）** 时，芯片上电后**停留在 ISP 而不跳转**用户程序——所以全新芯片第一次上电即可直接被 WCHISPTool 识别。
- **检测时机**：ISP 引脚/总线检测**仅在上电复位后**进行（串口免按键握手指纹约 40ms；串口下载超时或关闭免按键后，再判 `UDP 高 & UDM 低` 进 USB 下载模式）。
- 一旦固件写好，芯片上电便跳入用户代码，Dongle 会枚举成**双虚拟串口**（VID `1A86` / PID `FE0C`），**不再是 ISP 设备**，此时 WCHISPTool 直接找不到它——需按下面方法重新进 Bootloader。

### 首次烧录（空片）

芯片为空时直接连 USB（Dongle）或按下方 H101 接线（Probe），打开 WCHISPTool 选择对应 `.hex` 下载即可。

### Dongle 二次烧录 —— `CMD:DFU`（一键进 Bootloader）

Dongle 已插上电脑、枚举为虚拟串口时，向 **COM2（Sensor Control）** 发送：

```text
CMD:DFU
```

固件会**擦除 Flash 第 0 扇区（抹掉复位向量）并软复位**，芯片因 0 地址为空而重新驻留出厂 Bootloader，两个虚拟串口随即消失。随后：

1. 若 WCHISPTool 未立即识别到 ISP 设备，**拔插一次 USB**（ISP 检测在上电复位后触发，冷启动最稳妥）；
2. 用 WCHISPTool 下载新的 `RF_UartDongle.hex`（新固件会重写第 0 扇区，恢复正常）。

> 该逻辑运行于 RAM（`.highcode`）且擦除前已关中断，避免擦除自身所在的 Flash 代码。

### Probe 烧录 —— 经 H101 排针

Probe 板上两个 Type-C 的 `D+/D-/CC` 是**直通给下游目标板**的，**并未连到 CH570Q**；Probe 自身芯片的 `PA0/PA1` 引到 **1×6P 排针 H101**：

```text
Pin1: MCU_VCC(3.3V) | Pin2: TXD(PA0) | Pin3: RXD(PA1) | Pin4: GND | Pin5: RST# | Pin6: BOOT
```

- **USB ISP（推荐）**：剪一根 USB 线，`D-(白)`→Pin2、`D+(绿)`→Pin3、`GND(黑)`→Pin4，板子自身 Type-C 供电，WCHISPTool 选 USB 模式下载。
- **串口 ISP**：用 CH340/CP2102 等 USB-TTL，模块 `RXD`→Pin2、`TXD`→Pin3、`GND`→Pin4，WCHISPTool 选串口模式、115200 下载。
- **已烧录过的 Probe 重新进 ISP**：在**上电瞬间把 PA1（Pin3/RXD）持续拉高**，触发"串口免按键"下载窗口（约 40ms，需 WCHISPTool 已处于等待）即进入 Bootloader；进入后其 2.4G 射频停摆，**必须保持 USB/串口物理连接**才能重新下载。（Probe 端未做无线 `CMD:DFU`——一旦进 Bootloader 射频即失效，无线下发无意义。）

---

## 三、上位机（运行方法）

两端上位机功能一致：连接 Dongle、实时绘制电流/功率/电压曲线、透传串口、一键复位/进烧录/查询版本。

### Windows —— `app_win`

```bat
cd app_win
build.bat                     :: 或 python build.py（调用 CMake + MSVC Release）
build\Release\CH570_Monitor.exe
```

依赖 DirectX11 与 C++/WinRT（BLE 扫描）、SetupAPI（枚举串口）。连接 COM2 后会自动查询并在顶部状态栏显示 Dongle/Probe 固件版本。

### Linux —— `app_linux`（ch570mon，终端 TUI）

```bash
sudo apt install libncursesw5-dev build-essential   # 首次
cd app_linux && make
./ch570mon                                          # 自动探测控制口/透传口
./ch570mon --control /dev/ttyACM1 --passthrough /dev/ttyACM0 --baud 115200
```

htop 风格单线程 TUI：块字符 sparkline 画电流/功率/电压，下方为串口透传回滚。常用键：`r` 复位目标、`b` 目标进 Bootloader、`s` 交换 UART、`v` 查版本、`c` 查配置、`1/2/3` 聚焦 I/P/V、`p` 暂停、`q` 退出（详见 `app_linux/README.md`）。

---

## COM2 控制指令

向 Dongle 的 **COM2（CH570 Sensor Control）** 发送以下 ASCII 行指令（`\r\n` 结尾）：

| 指令 | 作用 |
|---|---|
| `CMD:RST` | 复位**目标 MCU**（Probe 经 MAX811S 拉 RST#） |
| `CMD:BOOT` | 令**目标 MCU** 进入 Bootloader（无线下载用） |
| `CMD:SWAP` | 交换目标 UART 的 TX/RX |
| `CMD:FSC=<mA>` | 设置电流量程 |
| `CMD:RATE=<10-1000>` | 设置遥测上报周期（ms） |
| `CMD:SHUNT=<uΩ>` | 设置采样电阻阻值 |
| `CMD:AVG=<n>` | 设置 INA226 平均次数 |
| `CMD:SAVE` | 保存配置到 Flash |
| `CMD:CFG?` | 查询当前配置 |
| `CMD:VER?` | 查询 Dongle 与 Probe 固件版本（形如 `2026-09-29 r01`） |
| `CMD:DFU` | 令 **Dongle 自身**擦除复位向量并进入出厂 Bootloader（二次烧录用） |

> COM1（CH570 Wireless UART）为与目标 MCU 的透明串口，收发原样透传，不解析指令。

---

## 工作原理

- **Dongle 端**：USB 虚拟串口 ⇄ 2.4G 无线的双向透传。接收电脑发来的串口参数与数据，经 2.4G 发给 Probe；把 2.4G 收到的数据经 USB 传回电脑。
- **Probe 端**：2.4G 无线 ⇄ 有线 UART 的双向透传，并用 INA226 测量目标板供电电流，周期回传遥测。
- **配对**："一碰连"——两端靠近使信号超过阈值即自动绑定，绑定信息存 Flash，之后上电自动重连；两端 LED 连接后常亮、通信时闪烁。

![](https://image.lceda.cn/pullimage/1cb1L9oXt9vOGGgrkdGKlehWILLaz5GpSBYMhWoP.png)

---

## 文档与资料

- `docs/CH572DS1.PDF` —— CH572/CH570 数据手册
- `docs/max811s_datasheet.pdf` —— MAX811S 复位芯片手册
- `docs/带有电流测量的无线串口工具设计.md` —— 硬件/固件设计说明
- `docs/立创开源硬件平台项目描述.md` —— 开源平台项目描述

**License**：硬件与固件遵循开源硬件项目页所述许可。
