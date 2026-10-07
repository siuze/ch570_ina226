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
│   └── build_firmware.ps1    # Windows 下构建 Dongle/Probe ELF/HEX
└── readme.md
```

> `tmp/` 为本地调试脚本与原理图/网表收容目录，已在 `.gitignore` 中忽略，不入库。

---

## 环境要求

| 组件 | 平台 | 依赖 |
|---|---|---|
| **固件**（生成 `.hex`） | Windows | 仓库内的 `tools/riscv-gcc/riscv-none-elf-gcc-12-win-1.92/bin/riscv-wch-elf-gcc.exe`；链接需 WCH 专有库 `libCH57xRF.a`、`libISP572.a` |
| **固件**（仅编译校验） | Linux | `tools/xpack-riscv-none-elf-gcc-*/`（xPack riscv-none-elf-gcc，linux-x64，需自备）、`bash`、`make` |
| **app_win** 上位机 | Windows 10/11 | Visual Studio 2022（含"使用 C++ 的桌面开发"）、CMake、DirectX11、C++/WinRT（均随 VS/Windows SDK 提供） |
| **app_linux** 上位机 | Linux | `gcc`、`make`、`pkg-config`、`libncursesw5-dev`（宽字符 ncurses） |
| **烧录** | Windows | [WCHISPTool](http://www.wch.cn/downloads/WCHISPTool_Setup_exe.html)（USB / 串口 ISP，免额外调试器） |

---

## 一、编译固件

固件分两端：`RF/RF_Uart`（Probe）与 `RF/RF_UartDongle`（Dongle）。

### Windows —— 生成可烧录的 `.hex`（推荐）

在项目根目录运行：

```powershell
.\tools\build_firmware.ps1                 # Dongle
.\tools\build_firmware.ps1 -Target Probe   # Probe
```

脚本使用仓库内的 WCH GCC 12.2.0，从源码重建并分别输出 `RF/RF_UartDongle/obj/RF_UartDongle.hex` 与 `RF/RF_Uart/obj/RF_Uart.hex`；无需另装 `make`。Probe 启动期间不再向功能 UART 写调试日志，避免尚未初始化时卡在发送 FIFO，也避免串口透传数据被日志污染。

USB 故障定位还可构建只初始化 USB、并让 PA3 链路 LED 报告控制传输进度的诊断固件：

```powershell
.\tools\build_firmware.ps1 -Diagnostic
```

产物是 `RF/RF_UartDongle/obj/RF_UartDongle_diagnostic.hex`。它跳过射频初始化和 PA2 PWM，只供排障；恢复正常使用需重新烧录 `RF_UartDongle.hex`。

> **专有库说明**：最终链接需要 WCH 的两个预编译库——`RF/LIB/libCH57xRF.a`（2.4G 射频协议栈）与 `SRC/StdPeriphDriver/libISP572.a`（Flash-ROM 擦写例程）。当前工作目录已有这两个未纳入版本控制的文件；新环境需从官方 CH57x EVT/SDK 补齐。

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

### 推荐：免拆/无线升级（A/B 分区 + 校验，无需 WCH 官方工具）

固件内建 Flash A/B 分区机制（Slot A 为运行区 `0x00000000`，Slot B 为暂存区 `0x00020000`）。更新时将固件分块下发至 Slot B，在 Flash 内完成 CRC32 完整性校验后，由常驻 RAM 的搬移函数原子覆盖 Slot A 并复位，**全程免按键、免拆壳、免飞线、免拔插**。

#### 1. Dongle 串口免拆升级

Dongle 插入电脑枚举出虚拟串口（COM2）后，在命令行运行：

```bash
python tools/flash_dongle.py COM9 RF/RF_UartDongle/obj/RF_UartDongle.hex
```

脚本会自动解析 HEX、计算 CRC32、通过 COM2 分块写入 Slot B，校验通过后 Dongle 自动重启生效。

#### 2. Probe 2.4G 无线 OTA 升级

Probe 与 Dongle 正常建立无线连接后，直接向 Dongle 的 COM2 发送升级流，Dongle 会将固件通过 2.4G 专有射频无线下发至 Probe 的 Slot B：

```bash
python tools/flash_probe.py COM9 RF/RF_Uart/obj/RF_Uart.hex
```

无线下发完成后 Probe 校验 CRC32 并自动覆盖生效重启，全程无需连接 Probe 的物理 H101 接口。

---

### 首次烧录（空片）或救援烧录（WCHISPTool）

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

### Dongle 的 USB 固件完全无响应时

此时无法向 COM2 发送 `CMD:DFU`。已烧录的用户程序也不会因为反复拔插 USB 自动变回空片。可利用首次烧录时启用的“串口免按键下载”恢复：

1. 断开 Dongle 电源，用 USB-A 转接板或测试夹引出 Dongle 的 `PA0=D-`、`PA1=D+`、`GND` 和 `5V`。通过走线/通断确认引脚，不要仅凭 USB 插头视角或线色判断。
2. 用 **3.3V 电平** USB-TTL 连接：TTL `TX`→`PA0`，TTL `RX`→`PA1`，两端 `GND` 相连；再用约 **10 kΩ 电阻将 PA1 上拉到板上的 VDD33**，使其在上电瞬间为高电平。板子 `5V` 按原设计供电；TTL 的 `3.3V` 不接板子 `5V`。不要让同一组 PA0/PA1 同时连接 PC 的 USB 数据口。
3. WCHISPTool 选择 `CH57x / CH570 / 串口`，保留“串口免按键下载”，选择**重新编译**的 Dongle HEX。先点击“下载”进入等待，再给板子重新上电。如果无法握手，先核对 PA1 上拉、引脚和电源，再检查 TTL TX/RX 方向。
4. 下载、校验成功后断电，拆除 TTL 接线，作为 USB 设备重新插入电脑；设备管理器应出现 VID `1A86`、PID `FE0C` 的复合设备及两个 COM 口。

如果串口 ISP 仍进不去，再检查 V5、VDD33/VIO33、32 MHz 晶振以及 USB D+/D- 接线；WCH-LinkE 可经 PA0/PA1 调试接口尝试恢复，但该途径取决于烧录选项是否仍使能调试接口。请在烧录前运行上述构建脚本，确认 HEX 是本次源码生成的产物。

#### 烧录成功但仍完全不枚举

先烧录 `RF_UartDongle_diagnostic.hex` 并给板子断电重上电。每组 PA3 短闪次数依次表示：**1 次**＝USB_Init 已返回、**2 次**＝收到总线复位、**3 次**＝收到 SETUP 请求、**4 次**＝完成一次 EP0 IN。组间停顿约 1 秒；次数只增不减。

| 现象 | 下一步 |
|---|---|
| PA3 每组 4 闪，USB 也出现设备 | 启动与 USB 控制传输基本正常，再烧正式版；若正式版失败，重点查 RF/PA2 初始化。 |
| PA3 只有 1 闪，Windows 无插入提示 | 应用程序未观察到总线复位。测 USB-A 插头侧 D+ 对 GND 的电压，排查应用态上拉、USB 引脚使能及供电。 |
| PA3 只有 2 闪，Windows 报代码 43 | 主机已发总线复位但固件没收到 SETUP，检查 USB 中断及 EP0 的 ACK 状态。 |
| PA3 只有 3 闪，Windows 报代码 43 | 固件已收到 SETUP 但没完成 EP0 IN，检查设备描述符首包的 DATA1、DMA 缓冲区及端点状态。 |
| PA3 每组 4 闪，Windows 仍报代码 43 | 首个 EP0 IN 已完成，继续查后续描述符包、状态阶段及描述符内容。 |
| PA3 不闪，USB 也无插入提示 | 优先查板上 5 V、VDD33/VIO33、晶振与复位；核对烧录后是否确实运行目标程序，并确认 LED202/PA3 的焊接。 |
| Windows 出现“未知 USB 设备” | USB 接入已被检测到，重点看 D+/D- 波形、描述符与 EP0 中断，记录设备管理器具体错误代码。 |

正常正式版中，PA2 电流 LED 按最近一次电流报文调光，电流为零时保留最低亮度，超过 5 秒没有报文才熄灭；PA3 链路 LED 在未绑定时方波闪烁，绑定后按近期透传收发速率呼吸，最大亮度可用 `CMD:LINKLED` 设置。Dongle 使用 TMR 中断驱动的软件 PWM，约 250 Hz 载波，64 级瞬时调制并通过跨周期分数累加达到 12 位有效亮度分辨率，低电平点亮。Probe 的 PA2/PA3 是 INA226 的 I2C，不是链路 LED；Probe 的串口活动指示在 PA0/PA1。

COM2 还提供 2.4 GHz 链路测速命令：

```text
CMD:RFTEST       # 默认测试 1000 ms
CMD:RFTEST=3000  # 测试 3000 ms，允许 100..5000 ms
```

测试帧使用保留标记并由 Dongle 消费，不会写入 Probe 的目标 UART。返回行中的 `bytes/time/rate` 是 Dongle 实际收到的有效载荷和有效吞吐，`L平均/最小/最大 N包` 是 Probe 测得的 RF 往返延迟（微秒）及样本数。测试期间应暂停 ESP 烧录和普通透传。

PA3 通信灯按 COM1 双向实际字节数的最近 5 秒窗口调整：无数据为 4 秒完整呼吸；有少量数据从 3 秒平滑过渡；达到约 1024 字节/5 秒进入 1 秒呼吸。呼吸曲线使用非线性查表，避免线性 PWM 在低亮度区看起来突然熄灭。

### Probe 烧录 —— 经 H101 排针

Probe 板上两个 Type-C 的 `D+/D-/CC` 是**直通给下游目标板**的，**并未连到 CH570Q**；Probe 自身芯片的 `PA0/PA1` 引到 **1×6P 排针 H101**：

```text
Pin1: MCU_VCC(3.3V) | Pin2: TXD(PA0) | Pin3: RXD(PA1) | Pin4: GND | Pin5: RST# | Pin6: BOOT
```

- **USB ISP（推荐）**：剪一根 USB 线，`D-(白)`→Pin2、`D+(绿)`→Pin3、`GND(黑)`→Pin4，板子自身 Type-C 供电，WCHISPTool 选 USB 模式下载。
- **串口 ISP**：用 CH340/CP2102 等 USB-TTL，模块 `RXD`→Pin2、`TXD`→Pin3、`GND`→Pin4，WCHISPTool 选串口模式、115200 下载。
- **已烧录过的 Probe 重新进 ISP**：两端均刷入支持 `CMD:PROBE_DFU` 的固件并完成绑定后，可在 Windows 上位机“固件维护”页发送该命令；Dongle 收到 Probe 的无线确认后，Probe 擦除首扇区并重启。随后射频停摆，**必须连接 H101 的物理 USB 或 UART 并用 WCHISPTool 重新烧录**。若无线命令不可用，也可在**上电瞬间把 PA1（Pin3/RXD）持续拉高**，触发“串口免按键”下载窗口（约 40 ms，需 WCHISPTool 已处于等待）。

---

## 三、上位机（运行方法）

上位机全部功能和实现约束汇总见：[上位机总设计文档](docs/上位机总设计文档.md)。

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
| `CMD:FSC=<mA>` | 设置电流指示灯满亮对应电流量程（1~20000mA） |
| `CMD:LINKLED=<26-255>` | 设置 Dongle 短链路 LED 呼吸灯最大亮度（10%~100%） |
| `CMD:RATE=<10-1000>` | 设置遥测上报周期（ms） |
| `CMD:SHUNT=<uΩ>` | 设置采样电阻阻值 |
| `CMD:AVG=<n>` | 设置 INA226 平均次数 |
| `CMD:BLE=<1-20>` | 设置 Probe 未绑定时的 BLE 广播频率，默认 4 次/s |
| `CMD:SAVE` | 保存配置到 Flash |
| `CMD:CFG?` | 查询当前配置 |
| `CMD:VER?` | 查询 Dongle 与 Probe 固件版本（形如 `2026-09-29 r01`） |
| `CMD:FW_START=<len>,<crc32>` | Dongle A/B 分区升级：启动分块传输并擦除 Slot B (0x00020000) |
| `CMD:FW_DATA=<offset>,<hex>` | Dongle A/B 分区升级：下发 64 字节固件切片 |
| `CMD:FW_FINISH` | Dongle A/B 分区升级：校验 CRC32 并在 RAM 中覆盖 Slot A 重启生效 |
| `CMD:PROBE_FW_START=<len>,<crc32>` | Probe 无线 OTA：经 2.4G 令 Probe 擦除 Slot B 并准备接收固件 |
| `CMD:PROBE_FW_DATA=<offset>,<hex>` | Probe 无线 OTA：经 2.4G 转发 64 字节切片至 Probe Flash |
| `CMD:PROBE_FW_FINISH` | Probe 无线 OTA：Probe 校验 CRC32 并在 RAM 中覆盖 Slot A 重启上线 |
| `CMD:DFU` | 令 **Dongle 自身**擦除复位向量并进入出厂 Bootloader（二次救援烧录用） |
| `CMD:PROBE_DFU` | 通过无线链路令 **Probe 自身**擦除复位向量并进入出厂 Bootloader；随后必须通过 H101 物理接口重新烧录 |

> COM1（CH570 Wireless UART）为与目标 MCU 的透明串口，收发原样透传，不解析指令。

---

## 工作原理

- **Dongle 端**：USB 虚拟串口 ⇄ 2.4G 无线的双向透传。接收电脑发来的串口参数与数据，经 2.4G 发给 Probe；把 2.4G 收到的数据经 USB 传回电脑。
- **Probe 端**：2.4G 无线 ⇄ 有线 UART 的双向透传，并用 INA226 测量目标板供电电流，周期回传遥测。
- **配对**："一碰连"——两端靠近使信号超过阈值即自动绑定，绑定信息存 Flash，之后上电自动重连；Dongle PA3 链路灯未绑定时闪烁，绑定后按收发速率呼吸。

![](https://image.lceda.cn/pullimage/1cb1L9oXt9vOGGgrkdGKlehWILLaz5GpSBYMhWoP.png)

---

## 文档与资料

- `docs/CH572DS1.PDF` —— CH572/CH570 数据手册
- `docs/max811s_datasheet.pdf` —— MAX811S 复位芯片手册
- `docs/带有电流测量的无线串口工具设计.md` —— 硬件/固件设计说明
- `docs/立创开源硬件平台项目描述.md` —— 开源平台项目描述

**License**：硬件与固件遵循开源硬件项目页所述许可。
