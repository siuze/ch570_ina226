# CH570 无线串口 & 实时电流功耗监测器 Windows 上位机 (app_win)

## 1. 软件概述
本项目是针对 **CH570Q 双模无线串口透传与高精度电流功耗监测工具** 开发的专属高性能 Windows 桌面应用。
- **架构方案**：原生 C++20 + Dear ImGui + ImPlot + DirectX 11 + WinRT BLE API + Win32 Serial API
- **核心特点**：
  - 极小体积：独立单文件二进制可执行程序，体积仅 **~708 KB**（远低于 Qt/WPF/Electron 数十至数百 MB）
  - 超低资源占用：内存占用仅约 **14 MB**，VSync 同步刷新闲置 CPU 占用 **< 0.5%**
  - 高分屏原生清晰 (Per-Monitor V2 High-DPI)：完美适配 Windows 125%/150%/200% 缩放，中文字体边缘平滑锐利，彻底消除位图模糊
  - 紧凑自适应窗口：默认缩减为紧凑桌面尺寸 (920×570)，适配各种小尺寸笔记本屏幕与工作区
  - 固定静止时间轴 (Stationary Time Axis)：X 轴刻度锁定为最近固定区间（如 `0s ~ 30s`），刻度数字绝不横向滚动晃眼，曲线自右向左平滑推移
  - 现代高质感浅色日光风格 (Daylight Minimalist)：白瓷质感卡片、立体柔和微阴影、顶部彩色重音线与优雅浅灰边框，层次分明
  - 免配对蓝牙监听：基于 Windows C++/WinRT 原生底层 API，免配对直接捕获 Probe 端 `ADV_NONCONN_IND` 广播
  - 双串口穿透与控制：自动识别 USB Dongle (`VID: 0x1A86, PID: 0xFE0C`)，支持 COM2 遥测数据流高速解析与命令控制，以及 COM1 目标 MCU 数据串口全双工透传

---

## 2. 目录结构
```
app_win/
├── CMakeLists.txt              # CMake 构建配置 (MSVC 2022 / C++20 / DirectX 11)
├── build.py                    # 一键自动化编译与验证脚本
├── build.bat                   # Windows 批处理快速编译脚本
├── src/
│   ├── main.cpp                # WinMain 窗口入口、Direct3D 11 初始化、消息主循环
│   ├── app_state.hpp           # 统一环形缓冲队列、遥测数据结构、线程安全状态管理
│   ├── ble_scanner.hpp         # C++/WinRT 蓝牙无配对广播监听器头文件
│   ├── ble_scanner.cpp         # 0xFCD2 Service Data 解析、高分辨率电流电压转换实现
│   ├── serial_manager.hpp      # Win32 SetupAPI 设备枚举、COM1 透传与 COM2 控制头文件
│   ├── serial_manager.cpp      # VID/PID/MI 识别、异步读取、行解析与控制发送实现
│   └── ui/
│       ├── style.hpp           # 现代扁平暗黑主题配色与字体加载接口
│       ├── style.cpp           # 微软雅黑 / 矢量字体与调色板定义
│       ├── dashboard.hpp       # 仪表盘主界面组件声明
│       └── dashboard.cpp       # KPI 卡片、ImPlot 实时波形、控制面板与终端渲染实现
└── third_party/
    ├── imgui/                  # Dear ImGui (v1.91+)
    └── implot/                 # ImPlot (v1.0+)
```

---

## 3. 功能特性与协议解析

### 3.1 免配对 BLE 广播遥测 (Unpaired BLE Broadcast)
- **广播类型**：`ADV_NONCONN_IND` (不可连接无定向广播，信道 37/38/39)
- **服务 UUID**：`0xFCD2` (16-bit Service Data)
- **报文负载长度**：仅 **11 字节**（3B Flags + 8B Service Data），极大压缩空中信道占用与 Probe 功耗
- **高分辨率解码**：
  - 分流电压原始码值（`shunt_raw`，`int16_t`，带符号补码）：$I = \text{shunt\_raw} \times 0.125\,\text{mA}$
  - 母线电压原始码值（`bus_raw`，`uint16_t`，无符号）：$V = \frac{\text{bus\_raw} \times 1.25\,\text{mV}}{1000} = \text{bus\_raw} \times 0.00125\,\text{V}$
  - 瞬时功率计算：$P = V \times I$（单位：$\text{mW}$，精确到小数点后 2 位）

### 3.2 USB Dongle 遥测与参数遥控 (COM2 - MI_02)
- **自动识别**：通过 Windows SetupAPI 扫描 `VID_1A86&PID_FE0C` 下的复合接口，自动将 `MI_02` 绑定为遥测与控制端口。
- **高分辨率流解析**：格式 `V:5.012V, I:123.125mA, P:617.16mW\r\n`，自动解析并推入统一环形波形队列。
- **快捷控制命令**：
  - `CMD:RST`：通过 RF 反向指令控制 Probe 引脚复位目标单片机
  - `CMD:BOOT`：拉低 BOOT 引脚后复位，引导目标 MCU 进入 Bootloader
  - `CMD:SWAP`：切换 Probe 端的 TX/RX 交叉线序
  - `CMD:RATE=xxx`：设定遥测回传周期（10ms ~ 1000ms）
  - `CMD:FSC=xxx`：设定 Dongle 板载 PWM 模拟输出满量程电流（100mA ~ 3200mA）
  - `CMD:AVG=xxx`：配置 INA226 硬件滤波采样平均次数（1 ~ 1024）
  - `CMD:SHUNT=xxx`：配置采样电阻阻值（2mΩ ~ 100mΩ）
  - `CMD:SAVE`：固化所有运行参数至片上 Flash
  - `CMD:CFG?`：查询当前硬件运行参数并在交互日志中回显

### 3.3 目标 MCU 数据串口终端 (COM1 - MI_00)
- 自动识别并绑定 `MI_00`。
- 支持 9600 至 2000000 高速波特率自由切换。
- 提供独立终端视图，支持 ASCII / HEX 实时监控、自动滚动、发送输入及换行符配置。

---

## 4. 编译与运行方式
项目已提供完整源码及一键脚本，环境依赖为本地已安装的 MSVC 2022 (x64) 与 CMake。

### 命令行一键编译：
```powershell
cd app_win
python build.py
```

### 生成的可执行文件位置：
```
app_win\build\Release\CH570_Monitor.exe
```
无需安装任何外部运行时依赖，直接双击即可流畅启动运行！
