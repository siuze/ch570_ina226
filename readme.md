# CH570 无线串口与电流测量工具

本项目由 Probe、Dongle 和配套上位机组成：Probe 连接目标 MCU，Dongle 连接电脑，两端通过 2.4 GHz 链路实现串口透传；Probe 同时使用 INA226 采集电流、电压和功率。Windows、Linux 和 Android 上位机源码分别位于 `app_win`、`app_linux` 和 `app_android`。

## 环境要求

固件编译：

- Windows 10/11、PowerShell；
- 仓库内 RISC-V GCC：`tools/riscv-gcc/riscv-none-elf-gcc-12-win-1.92`；
- WCH 专有静态库 `RF/LIB/libCH57xRF.a` 和 `SRC/StdPeriphDriver/libISP572.a`。这两个库来自 CH57x 官方 SDK，不纳入仓库。

Windows 上位机：

- Windows 10/11；
- Visual Studio 2022，安装“使用 C++ 的桌面开发”；
- CMake；DirectX 11、Windows SDK、C++/WinRT 随 Visual Studio 提供。

Linux 上位机：

- GCC、Make、pkg-config；
- Debian/Ubuntu：`sudo apt install build-essential pkg-config libncursesw5-dev`。

Android 上位机：

- Android Studio 或 JDK 17；
- Android SDK 34、Gradle 8.5。Android 构建由 GitHub Actions 自动执行，本地不要求提交生成的 APK。

## 编译

### 固件（Windows）

在项目根目录执行：

```powershell
.\tools\build_firmware.ps1 -Target Dongle
.\tools\build_firmware.ps1 -Target Probe
```

产物：

```text
RF/RF_UartDongle/obj/RF_UartDongle.hex
RF/RF_Uart/obj/RF_Uart.hex
```

仅进行 Linux 翻译单元编译校验时，准备 xPack `riscv-none-elf-gcc` 到 `tools/xpack-riscv-none-elf-gcc-*/`，然后执行：

```bash
bash tools/build_linux.sh
```

该校验不进行最终链接，也不生成可烧录 HEX。

### Windows 上位机

```powershell
cmake -S app_win -B app_win/build -G "Visual Studio 17 2022" -A x64
cmake --build app_win/build --config Release
```

产物：`app_win/build/Release/CH570_Monitor.exe`。

### Linux 上位机

```bash
make -C app_linux
```

产物：`app_linux/ch570mon`。

### Android 上位机

GitHub Actions 使用 JDK 17 和 Gradle 8.5 执行：

```bash
gradle --no-daemon :app:assembleDebug :app:assembleRelease
```

提交 `v*` 标签后，Actions 会构建 Windows、Linux、Android 和固件，并创建 GitHub Release。Release 包含：

```text
CH570Q-Probe.hex
CH570Q-Dongle.hex
CH570_Monitor.exe
app-debug.apk
app-release.apk
ch570mon-linux-x64
ch570mon-linux-aarch64
```

## 详细文档

- [上位机详细设计](docs/上位机详细设计.md)
- [固件详细设计](docs/固件详细设计.md)
- [控制与遥测串口交互协议](docs/控制与遥测串口交互协议.md)
- [复位与烧录引脚原理](docs/复位与烧录引脚原理.md)
- [立创开源硬件平台项目描述](docs/立创开源硬件平台项目描述.md)
- `docs/CH572DS1.PDF`：CH570/CH572 芯片手册
- `docs/max811s_datasheet.pdf`：MAX811S 复位芯片手册
