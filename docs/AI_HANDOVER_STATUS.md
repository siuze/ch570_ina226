# CH570 无线串口 & ESP32-S3 烧录项目 AI 交接文档

**生成时间**：2026-10-06  
**项目仓库**：`d:\CH570无线串口-2v0`  
**目标工程**：`D:\SyncThing\YES-ESP-DX`（ESP32-S3 `VER003`）  
**文档目的**：供后续接手的 AI / 开发者全面了解任务目标、架构设计、排查历程、故障根因、已实施修复及后续待办事项。

---

## 一、本次任务的总体目标（要做的事情）

1. **目标板固件构建与参数配置**：
   - 检查并修改目标工程 `D:\SyncThing\YES-ESP-DX` 中的默认 WiFi SSID 为 `"Hello1"`。
   - 编译生成适用板型 `VER003` 的完整固件包（Bootloader、分区表、OTA 数据区、应用程序 `ESP-DX.bin`）。
2. **全无线链路固件烧录与验证**：
   - 通过 PC 虚拟串口 `COM7`（Dongle 端），经过 2.4G 专有无线链路透传给 Probe 端，再经由 Probe 端单引脚复位电路（MAX811S）控制 ESP32-S3 进入 Bootloader。
   - 在 115200 波特率下实现 100% 稳定可靠烧录，并探索 460800 高波特率下的烧录能力。
   - 烧录完成后启动串口监视器（`idf.py -p COM7 monitor`），确认 ESP32-S3 正常开机并连接 WiFi `"Hello1"`。
3. **无线串口高压烧录稳定性优化**：
   - 彻底根治无线串口缓冲区溢出与丢包超时问题，优化 CH570 两端（Dongle / Probe）固件架构。
4. **故障排查与救砖支持**：
   - 查明 Dongle 在 OTA 固件更新重启后丢失 USB 枚举的底层硬件与内存根因。
   - 修复固件代码缺陷，生成全新救砖固件，并指导硬件串口 ISP 救砖。

---

## 二、实际执行过程与关键发现（做了什么）

### 1. 目标固件构建与烧录测试
- **固件构建成功**：
  - `D:\SyncThing\YES-ESP-DX` 成功编译生成：
    - `bootloader.bin` (0x0)
    - `partition-table.bin` (0x8000)
    - `ota_data_initial.bin` (0x29000)
    - `ESP-DX.bin` (0x133720)
- **发现烧录阶段超时根因（下行数据包截断）**：
  - 在底层 raw 协议测试中，ESP32-S3 的 `SYNC (0x08)`、`SPI_ATTACH (0x0d)`、`SPI_SET_PARAMS (0x0b)` 和 `FLASH_BEGIN (0x02)` 握手均 100% 成功。
  - 但一旦进入 `FLASH_DATA (0x03)`（实际刷写代码阶段），ESP-IDF 总是报响应超时。
  - **核心原因**：ESP-IDF 烧录每帧发送 1024 字节数据载荷，加上 16 字节头部与 SLIP 特殊字符转义（`0xC0` 编码为 `0xDB 0xDC` 等），单包 SLIP 帧长达 **1045 ~ 1060 字节**。原固件缓冲区（`COM1_RX_FIFO_SIZE` 和 `RF_BUF_LEN`）仅为 1024 字节，导致每包末尾的帧结束符 `0xC0` 被截断丢弃，ESP32 ROM 无法判定帧结束，最终超时挂起。

### 2. Probe 端固件扩容与升级
- Probe 端源码：[RF/RF_Uart/APP/rf_uart_tx.c](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/RF/RF_Uart/APP/rf_uart_tx.c)
- 将下行射频接收缓冲区 `RF_BUF_LEN` 扩容至 **1536 字节**，上行串口接收缓冲区 `UART_BUF_LEN` 设为 **1024 字节**。
  - 编译生成固件（Flash 16332 B / 6.65%，RAM 11008 B / 89.58%，静态栈预留 1280 字节）。
- **Probe 端已通过无线 OTA 成功刷写生效并正常绑定运行**。

---

## 三、重大问题与深度根因分析（遇到什么问题）

### 问题 1：Dongle 端 OTA 升级重启后物理 USB 无法枚举（变砖）

#### 故障现象
Dongle 接收完固件数据包并返回 `OK: FW_VERIFIED, APPLYING...` 后重启。此后 Windows 设备管理器中的 `COM7`（数据透传）和 `COM9`（遥测控制）彻底消失，重新拔插 USB 亦无法识别任何设备。

#### 底层根因分析
1. **CH570 物理硬件的极度严苛限制**：
   - 芯片 SRAM 仅 **12 KB**（`0x20000000` ~ `0x20003000`）。
   - WCH 2.4G 射频协议栈（`libCH57xRF.a`）及 Flash 擦写底层（`libISP572.a`）必须常驻 RAM 运行（`.highcode` 段），吃掉约 **7.5 KB**。
   - 剩余给所有全局变量、收发缓冲区和**调用栈（Stack）** 的空间不足 **4.5 KB**。
2. **栈空间极限压缩（仅存 432 字节）**：
   - 此前 Dongle 为了容纳大包，将 `COM1_RX_FIFO_SIZE` 扩大到 1536，RAM 占用飙升至 11856 字节（96.48%）。
   - 链接脚本 [Link.ld](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/SRC/Ld/Link.ld) 中，栈从 `0x20003000` 向下生长，`_ebss` 位于 `0x20002E50`。
   - **全芯片仅剩 432 字节栈可用空间！**
3. **`Dongle_ApplyFirmware()` 栈上开辟 256 字节大数组导致栈击穿**：
   - 源码原实现：
     ```c
     __HIGH_CODE
     static void Dongle_ApplyFirmware(uint32_t size) {
         uint32_t irqv;
         uint8_t copy_buf[256] __attribute__((aligned(4))); // 在栈上开辟 256 字节！
     ```
   - 进函数前调用链路：`main` $\to$ `process_cdc2_rx` $\to$ `handle_cdc_cmd`（占用约 100 字节）。
   - 进入后压栈 `copy_buf[256]`，栈深达到 360 字节。
   - 紧接着调用 `FLASH_ROM_ERASE` $\to$ `FLASH_EEPROM_CMD`（WCH 库例程，内部大量寄存器入栈和局部变量，开销 $\ge 120$ 字节）。
   - **总栈需求 $\ge 480$ 字节 > 可用栈 432 字节**！
   - **发生栈击穿（Stack Overflow）**：栈指针向下踩烂了 `.bss` 全局变量和返回地址。在抹除扇区 0（`0x00000000`）并开始向扇区 0 拷贝第 1 块数据时，CPU 发生 HardFault 崩溃死锁。
4. **为何重新拔插无法恢复**：
   - 上电复位时，CH570 硬件 ROM 检测 `0x00000000`。
   - 由于崩溃前第 1 块已被写入，首地址不为 `0xFFFFFFFF`，Bootloader 误认为存在有效固件，盲目跳转执行。
   - 但因为后续代码缺失损坏，MCU 瞬间陷入 HardFault 死循环，USB 从未得到初始化，因此 Windows 无法枚举出任何端口。

---

### 问题 2：`UART_BUF_LEN` 与 `RF_BUF_LEN` 的配置权衡

用户提问：“为什么要缩小 `UART_BUF_LEN`？不应该是两者都尽量扩大，RF 略大存放包头吗？”

**分析与定论**：
- 用户的通常网络工程直觉是完全正确的，但在 CH570 12KB RAM 的物理硬墙下必须服从数据流的**强非对称性**：
  - **下行链路（PC $\to$ ESP32 烧录）**：属于突发大包模式（每包 ~1060 字节）。因此下行方向上的 Dongle USB 接收区（`s_com1_rx_fifo`）和 Probe 射频接收区（`rf_buf`）**必须保证 $\ge 1280 \sim 1536$ 字节**。
  - **上行链路（ESP32 $\to$ PC 响应）**：ESP32 烧录响应固定仅为 **12 字节**，日志文本也是零散行。Probe 端的 `uart_buf[UART_BUF_LEN]` 仅负责上行方向，将其设为 1024 字节可轻松容纳 **80 多个完整的响应包**，完全不会成为瓶颈，同时为下行缓冲区和栈腾出了生死攸关的 1 KB 空间。

---

## 四、已实施的代码修复与防固化措施（怎么处理的）

已在仓库中实施全部关键修复并成功构建：

### 1. 废除栈缓存，零栈开销复用静态内存
- **Dongle 端** [usb_uart.c](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/RF/RF_UartDongle/APP/usb_uart.c#L465)：
  - 彻底移除栈上的 `copy_buf[256]`。
  - 改为直接复用已关中断、停用通信的静态全局数组：`uint8_t *copy_buf = (uint8_t *)s_com1_rx_fifo;`。
- **Probe 端** [rf_uart_tx.c](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/RF/RF_Uart/APP/rf_uart_tx.c#L93)：
  - 同步重构，复用静态全局 `rf_buf`，消除 Probe 端 OTA 栈溢出隐患。

### 2. 科学重构 FIFO 尺寸，保证 1.2 KB 栈安全空间
在 [usb_uart.c](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/RF/RF_UartDongle/APP/usb_uart.c#L148) 中精细调整：
- `COM1_RX_FIFO_SIZE`：设为 **1280 字节**（4 字节对齐，完整容纳 1060 字节 SLIP 帧 + 220 字节冗余）。
- `COM1_TX_FIFO_SIZE`：设为 **384 字节**（可缓存 ~30 个上行响应包）。
- `COM2_TX_FIFO_SIZE`：保持 **192 字节**（遥测和 AT 指令回显共用，不能通过缩减 COM1 TX FIFO 来换取空间）。
- **Dongle 编译内存报告**：
  - Flash：25768 B / 240 KB（10.49%）
  - RAM：11152 B / 12 KB（90.76%）
  - **静态栈预留 1136 字节**（仍通过 1 KiB 链接断言）。

### 3. 链接脚本强断言永久防御
在 [SRC/Ld/Link.ld](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/SRC/Ld/Link.ld#L175) 尾部启用强断言：
```ld
/* Enforce a build-time floor of 1 KiB for interrupt and main-loop stack reserve. */
ASSERT(_ebss + 1024 <= ORIGIN(RAM) + LENGTH(RAM), "RAM stack reserve below 1 KiB")
```
此后任何代码修改若导致栈空间小于 1024 字节，编译器链接阶段将直接抛出错误中断构建，彻底杜绝变砖固件生成。

---

## 五、当前系统状态与文件产物（现状如何）

1. **固件就绪情况**：
   - Dongle 修复版固件：[RF_UartDongle.hex](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/RF/RF_UartDongle/obj/RF_UartDongle.hex)（编译通过，已就绪供救砖烧录）。
   - Probe 修复版固件：[RF_Uart.hex](file:///d:/CH570%E6%97%A0%E7%BA%BF%E4%B8%B2%E5%8F%A3-2v0/RF/RF_Uart/obj/RF_Uart.hex)（编译通过，Probe 当前在线运行 `r08-buf4k` 亦属正常）。
   - 目标板 ESP32-S3 固件：位于 `D:\SyncThing\YES-ESP-DX\build`，WiFi SSID 已更新为 `Hello1` 并通过编译。
2. **硬件物理现状**：
   - **Probe 端**：已完成升级，正常处于 2.4G 监听与透传准备就绪状态。
    - **Dongle 端**：已重新烧录并恢复 USB 枚举，当前出现 COM7（透传）和 COM9（控制/遥测）。

---

## 六、本轮代码复核与修复（2026-10-06）

1. Dongle 本机 `CMD:FW_START` / `CMD:FW_DATA` 的 Flash 擦除和写入现在检查返回值，并在每次 Flash 操作期间屏蔽 USB 中断；Probe 和 Dongle 的暂存镜像复制失败时会转入出厂 ISP，避免继续启动残缺镜像。
2. Probe OTA 数据块、Dongle 本机升级块和两端 RF 包增加长度、偏移、4 字节对齐及溢出检查；无效 RF 包会丢弃并恢复接收。
3. INA226 位操作 I2C 延时按 `GetSysClock()` 缩放，适配 Probe 当前 100 MHz 主频。
4. `SRC/Ld/Link_test.ld` 启用 1 KiB 栈预留断言；两个 HEX 升级脚本增加 Intel HEX 长度、校验和、地址范围、重叠数据校验，并严格检查块 ACK。
5. `tools/test_packet_length.py` 增加 `0xC0` / `0xDB` 最坏 SLIP 转义负载测试。两端固件、Windows 上位机和 Python 脚本语法检查已通过；Dongle ISP 恢复和 Probe 无线 OTA 已完成，ESP32 完整烧录、460800 压力测试及断电测试仍待执行。应用区仍是直接覆盖，真正的断电回滚需要独立引导程序和元数据设计。
6. 实板回归已完成：`CMD:VER?` 返回 Dongle 与 Probe 均为 `2026-10-06 r08-buf4k`；Probe 无线升级发送 16,332 字节、256 个 64 字节块，全部收到正确偏移 ACK，设备返回 `OK: PROBE_FW_VERIFIED, APPLYING...`。重启后 `CMD:VER?`、`CMD:CFG?`、RSSI 遥测均正常，当前 RSSI 约 `-54 dBm`。

## 七、交接后后续 AI 需执行的步骤清单

接手的 AI 请按以下顺序协助用户完成剩余任务：

### 第一步：协助用户完成 Dongle 串口 ISP 救砖
- **接线规范**（使用 3.3V USB-TTL）：
  - TTL `TXD` $\to$ Dongle USB 插头 `D-`（芯片 Pin 3 / PA0）
  - TTL `RXD` $\to$ Dongle USB 插头 `D+`（芯片 Pin 4 / PA1）
  - TTL `GND` $\to$ Dongle USB `GND`（外壳或 Pin 4）
  - Dongle 插 5V 供电，**PA1 (D+) 在上电瞬间需维持高电平（3.3V）**（通常 TTL 的 RX 自带上拉；若未进入可临时串 10k 电阻上拉到板上 3.3V）。
- **WCHISPTool 操作**：
  - 芯片系列：`CH57x 系列`
  - 芯片型号：`CH570`（或 `CH570Q`）
  - 下载方式：`串口`（选择对应 TTL 的 COM 口，波特率 115200）
  - 文件路径：`d:\CH570无线串口-2v0\RF\RF_UartDongle\obj\RF_UartDongle.hex`
  - 上电并点击【下载】完成刷写。

### 第二步：确认 USB 重新枚举与无线配对
1. 拔掉 TTL 连线，将 Dongle 插入 PC 的 USB 接口。
2. 检查设备管理器中是否重新出现：
   - `COM7`（CDC-ACM 主透传串口）
   - `COM9`（CDC-ACM 辅助遥测与控制串口）
3. 在 `COM9` 发送 `CMD:VER?`，验证固件版本信息及双端绑定状态（`RF_bound_Flag = 1`）。

### 第三步：执行 ESP32-S3 目标固件烧录
运行目标烧录脚本（115200 波特率）：
```powershell
powershell -ExecutionPolicy Bypass -File D:\SyncThing\YES-ESP-DX\flash_target.ps1 -Port COM7 -Baud 115200
```
或调用原生 esptool 命令：
```bash
python -m esptool --chip esp32s3 -p COM7 -b 115200 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 D:\SyncThing\YES-ESP-DX\build\bootloader\bootloader.bin 0x8000 D:\SyncThing\YES-ESP-DX\build\partition_table\partition-table.bin 0x29000 D:\SyncThing\YES-ESP-DX\build\ota_data_initial.bin 0x133720 D:\SyncThing\YES-ESP-DX\build\ESP-DX.bin
```
- 确认全部 4 个分区完整写入（特别是单包 1024 字节载荷的 `ESP-DX.bin` 写入无卡死、无截断）。

### 第四步：检查监视器输出与 WiFi 连接
- 打开串口监视器：
  ```powershell
  python -m serial.tools.miniterm COM7 115200
  # 或使用 esp-idf monitor:
  idf.py -p COM7 monitor
  ```
- 确认目标板正常启动，日志中显示 WiFi 成功连接至 `"Hello1"`。

### 第五步：460800 高波特率压力烧录测试
- 验证高波特率参数 `-Baud 460800`，评估无线流控与重传稳定性。

## 八、2026-10-06 ESP 实板回归结果与当前补丁

### ESP32-S3 烧录实测
- 已用 `D:\SyncThing\YES-ESP-DX\flash_target.ps1 -Port COM7 -Baud 115200` 和原生 esptool 逐包跟踪测试。
- DTR/RTS 复位后，ESP32-S3 ROM 可以稳定响应 SYNC，识别为 `ESP32-S3 (QFN56) rev v0.2`，并完成擦除。
- 失败固定发生在持续 `FLASH_DATA` 写入后的响应阶段，尚未完成四个镜像的全量写入；因此当前不能声称 ESP 已烧录成功。
- 失败期间抓到 Dongle 接收序号错位，说明 RF 重传会重复交付串口数据。重复下发 SLIP 字节会破坏 ESP ROM 帧边界，这是当前主排查方向。

### DTR/RTS 处理结论
- COM1 的 USB CDC `SET_CONTROL_LINE_STATE` 已被捕获，代码把线路事件转成内部 `CTRL_OP_RESET` / `CTRL_OP_BOOT` 控制包，经 RF 下发到 Probe；它们不是直接发送字符串 `CMD:RST` / `CMD:BOOT`。
- COM2 文本命令 `CMD:RST` 和 `CMD:BOOT` 仍分别映射为同一两个内部操作码。
- 已实测默认 esptool 复位流程能进入 ESP ROM，证明 DTR/RTS 事件链路至少在该流程下有效。当前状态机已放宽为从任意初始线路状态进入 RTS asserted 都可开始 reset 阶段，并以 `3 -> 1` 作为 boot 阶段。
- 最新修复尚未写回 Dongle：OTA 传输中途设备掉枚举，Windows 当前报 `VID_0000&PID_0002` 未知 USB 设备。恢复 USB 枚举后需重新执行 `CMD:VER?`、DTR/RTS 捕获和 ESP 烧录回归。

### 最新代码修复
- `RF/RF_UartDongle/APP/usb_uart.c`：DTR/RTS 状态机不再要求端口初始状态必须为 `0`，兼容 pyserial/esptool 打开串口时的初始电平。
- `RF/RF_UartDongle/APP/rf_uart_rx.c`：只对重复的 `PKT_DATA_FLAG` 做幂等 ACK；重复 `PKT_CMD_GET_STATUS` 继续走原状态响应逻辑，避免丢失烧录流控和控制响应。
- 最新 Dongle 构建产物：`RF/RF_UartDongle/obj/RF_UartDongle.hex`，Flash 25,952 B，RAM 11,224 B，静态栈预留约 1,064 B。

### 2026-10-06 后续验证与结论
- 原理图上 PA7 通过 D101 单向钳住 BOOT，通过 C106 耦合到 MAX811S 的低有效 MR#。按静态电路分析，释放 PA7 应使 D101 截止；但实板对照测试显示，约 1.5 s 自动释放后确实会再次启动 ESP：串口序列为 `boot:0x0 (DOWNLOAD)`、`waiting for download`，随后出现 `boot:0x8 (SPI_FAST_FLASH_BOOT)` 和 `invalid header: 0xffffffff`。示波器接入改变负载后现象消失，说明存在板级 RC/寄生边沿耦合，不能只按理想二极管模型判断。
- `RF/RF_Uart/APP/uart.c` 已改为 `CMD:BOOT` 后保持 PA7 低电平，直到显式 `CMD:RST`；重复 `chip-id` 测试在 0.3~3.5 s 均能保持 ROM 响应。
- `RF/RF_Uart/APP/rf_uart_tx.c` 删除了把 `PKT_DATA_RSP_ACK` 当作重复包丢弃的条件，避免合法确认被丢弃后重发串口 SLIP 数据；同时将 RF 忙状态恢复看门狗从 50 ms 放宽到 500 ms，避免一个无线重传窗口内启动第二个 GET_STATUS 请求。
- Probe 修复版已成功编译并通过无线 OTA 校验；但在连续原始 ROM 测试后 Probe 的控制响应暂未恢复，ESP 全量烧录仍未完成。下一步需先对 Probe 断电重插，确认 `CMD:VER?` 能同时返回两端版本，再重试完整烧录。

### 2026-10-06 MAX811S 复位时序重新设计
- 原始固件的 `CMD:RST` 为 PA7 推挽低 20 ms 后直接输入上拉；`CMD:BOOT` 为推挽低 1100 ms 后直接输入上拉。原理图中的 C106=100 nF、R101=10 kΩ 与 MAX811S 的 MR 内部上拉共同形成约 0.67 ms 的恢复时间常数，但 MAX811S 的 RESET 保持时间由 `Trp` 主导（MSKSEMI 数据表：85 ms 最小、500 ms 典型、900 ms 最大）。
- 当前实验版修正了“BOOT 保持低电平后再次 CMD:RST 没有新的下降沿”的问题：`CMD:RST` 执行高 5 ms、低 20 ms、高 5 ms，再切换输入上拉；`CMD:BOOT` 先高 5 ms建立确定起点，再低电平保持 1500 ms，随后推挽高 200 ms，最后切换为浮空输入。
- 该版本已编译成功（Flash 16644 B，HEX CRC32 `0xFCC599A9`），尚未完成实板烧录。高电平到浮空的两阶段释放仍需用 BOOT、MR#、EN 三通道示波器复测，不能仅凭规格书推断板级寄生瞬态。

### 2026-10-06 芯片特性与传输性能审计
- `RF/RF_Uart/APP/rf.c` 与 `RF/RF_UartDongle/APP/rf.c` 均把 `gTxParam.txDMA`、`gRxParam.rxDMA` 指向 4 字节对齐的内部 RAM 缓冲区；`TEST_PHY_MODE` 固定为 `PHY_MODE_PHY_2M`，Dongle 绑定参数 `CONN_PHY_TYPE` 也为 2M，RF 芯片的 DMA 和 2 Mbps 特性已经实际启用。
- `RF/RF_UartDongle/APP/usb_uart.c` 已使用 USB 端点 DMA（`R16_UEP*_DMA` 与 `RB_UC_DMA_EN`）。CH572 当前 UART 头文件和寄存器没有可用的 UART DMA 通道，UART 数据路径采用硬件 4 字节 FIFO + 中断，这是该芯片上可用的实现。
- Probe 的 RF 串流缓冲保持 1536 字节，新增了环形指针到 `end` 的规范化，避免刚好写到尾地址后下一包从非法边界读取；RF 接收窗口放宽到 20 ms，单次丢包重传次数提高到 10，且在串口 FIFO 尚未排空时不再请求下一包。
- 经实测，Probe `r13-perf` 无线 OTA 传输 16,428 字节成功，平均约 8.7 KB/s；`r14-perf128` 在 115200 bps ESP ROM 写入中推进到压缩序号 5，明显优于 64 字节窗口，但仍会因主机突发发送超过 RF 转发速率而丢帧。当前已将源码信用窗口和 Dongle 上限提升到 RF 单包最大 251 字节，Probe 已 OTA 刷入 `r15-perf251`（CRC `0x80F3389F`），但实板 Dongle 仍是 `r08-buf4k`，其运行中的上限仍为 128 字节。
- Dongle 自更新路径发现并修复 `sizeof(data_buf)` 指针大小错误；该错误会把 `CMD:FW_DATA` 每块截断成 4 字节，导致 `tools/flash_dongle.py` 在 offset 0 超时。修复后的 Dongle 构建为 Flash 25,968 B、RAM 11,224 B（91.34%），需要一次 USB/UART ISP 烧录后才能实测 251 字节 RF 窗口。
- 当前硬件验证结论：RF/USB DMA 已启用，2 Mbps 已启用；有效吞吐受现有单事务停等协议和主机 115200 bps 突发发送限制，尚未完成多包滑动窗口协议改造。要继续逼近 RF 理论速率，需要在 Probe/Dongle 两端增加带序号、CRC、重组和确认窗口的多包流水线，同时保留 USB NAK 背压。
- 之后将 Probe 信用窗口保持 251 字节并尝试把 RF 接收窗口从 20 ms 降至 8 ms；8 ms 版本在 ESP bootloader 首个大块末尾出现串口流中断，已回退并重新 OTA 烧入 `r17-perf251-20ms`。当前实板 `CMD:VER?` 返回 Probe `r17-perf251-20ms`，Dongle 自更新块返回 `OK: 64`，证明 Dongle 修复版已实际生效。
- 新版 Dongle + `r17` 在 115200 bps 完整 ESP 写入中已推进到 bootloader 压缩数据序号 55，仍因 USB 主机突发速率高于单事务 RF 转发速率而出现 `0105`；ESP 仍留在 ROM，未观察到再次复位。下一步应优先实现主机发送节流或多包滑动窗口，不能继续单纯缩短 RF 接收窗口。
- 9600 bps 对照测试：ESP ROM 初始同步仍要求 115200；通过 115200 建链后下发 `CHANGE_BAUDRATE=9600`，下一条 SPI Flash 命令无响应，说明当前 Probe/Dongle 远端 UART 换速未与 ROM 同步，9600 不能作为现有固件的可靠烧录速率。测试后已恢复到 115200，`chip-id` 正常。
