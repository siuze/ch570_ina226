# CH570 无线串口 & 实时电流功耗监测器 Android 端 (app_android)

本项目是针对 **CH570Q Probe 端 BLE 广播遥测与高精度电流功耗监测** 开发的原生 Android 应用。
设计原则、解析算法与数据展示逻辑完全对齐 Windows 原生上位机 (`app_win`) 及相关技术设计规范。

---

## 1. 软件概述与技术栈

- **开发语言与架构**：Kotlin + Jetpack Compose + Material 3 + AndroidX ViewModel + Coroutines Flow
- **架构模式**：现代响应式单向数据流 (UDF / MVVM)
- **核心特点**：
  - **免配对原生监听**：基于 Android 原生 `BluetoothLeScanner`（低延迟模式 `SCAN_MODE_LOW_LATENCY`），无需配对即可捕获 Probe 端发出的 `ADV_NONCONN_IND` 广播包。
  - **抗抖动实时卡片滤波**：严格实现《上位机详细设计.md》第 6 节算法：
    - 最近 3 个采样点计算中值（Median of 3），过滤偶发孤立尖峰；
    - 普通波动使用 $\tau \approx 0.25\,\text{s}$ 的一阶指数滑动平均（EMA）；
    - 检测到明显的负载跳变时立即跟随最新采样，避免 0 A 到大电流需要数秒爬升；
    - 采样时间间隔 $\Delta t$ 来自设备实际采样点，与手机屏幕刷新帧率无关。
  - **单位自动切换与滞回**：
    - 电流：$\ge 1.50\,\text{A}$ 切入大单位，$< 1.35\,\text{A}$ 退回小单位（mA）；
    - 电压：$\ge 2.00\,\text{V}$ 切入大单位，$< 1.80\,\text{V}$ 退回小单位（mV）；
    - 功率：$\ge 1.00\,\text{W}$ 切入大单位，$< 0.85\,\text{W}$ 退回小单位（mW）。
  - **高质感现代设计**：
    - 瓷白卡片风格、高质感彩色边框、呼吸灯 BLE 状态指示；
    - 4 张核心 KPI 卡片：采样电流 (绿色)、实时功率 (橙色)、母线电压 (蓝色)、电量与能量累计 (紫色，支持一键清零)。
  - **高性能平滑波形**：
    - 采用 Jetpack Compose Canvas 原生硬件加速实时绘制；
    - 固定静止时间轴（Stationary Time Axis，支持 10s / 30s / 60s 窗口），曲线自右向左平滑推移；
    - 电流、功率、电压三通道独立显隐控制。
  - **轻量安装包**：
    - Release 只构建 ARMv8 和 Universal 两个版本，移除扩展图标库并启用 R8/资源压缩。

---

## 2. BLE 广播协议与解析方法

Probe 端通过不可连接广播包 (`ADV_NONCONN_IND`) 发送 16-bit Service Data（UUID `0xFCD2`）：

### 2.1 报文结构
- **AD Type**：`0x16` (Service Data - 16-bit UUID)
- **UUID**：`0xD2, 0xFC` (`0xFCD2`，小端序)
- **Payload 字段**：
  - `Byte 0..3`: 打包值低 17 位为有符号 `current_code`（`0.125mA/LSB`），高 15 位为 `bus_code`（`1.25mV/LSB`）
  - `Byte 4`: `uint8_t sequence`（滚动序号，用于丢包率统计或防重）

### 2.2 解码换算公式
1. **电流 ($I$)**：
   $$I = \text{signed\_current\_code} \times 0.125\,\text{mA}$$
2. **母线电压 ($V$)**：
   $$V = \text{bus\_code} \times 0.00125\,\text{V}$$
3. **瞬时功率 ($P$)**：
   $$P = V \times I\,\text{(mW)}$$
4. **电量累计 ($Q$) 与能量累计 ($E$)**：
   $$\text{mAh} = \sum \frac{I \times \Delta t}{3600}, \quad \text{mWh} = \sum \frac{P \times \Delta t}{3600}$$
   （注：仅对 $0 < \Delta t < 10.0\,\text{s}$ 的采样间隔进行有效积分，丢弃断线过长异常间隔）

---

## 3. 项目目录结构

```
app_android/
├── build.gradle.kts                     # 根项目构建脚本
├── settings.gradle.kts                  # 模块设置
├── gradle.properties                   # JVM 参数与 AndroidX 属性
├── app/
│   ├── build.gradle.kts                # 应用模块配置与依赖声明 (Compose BOM 2024.02.00)
│   ├── proguard-rules.pro              # 混淆优化规则
│   └── src/main/
│       ├── AndroidManifest.xml         # 蓝牙扫描与连接权限配置 (兼容 Android 6~14+)
│       ├── res/                        # 字符串、色彩、主题与矢量图标
│       └── java/com/ch570/probe/monitor/
│           ├── MainActivity.kt         # 动态权限申请与 UI 入口
│           ├── ble/
│           │   ├── BlePacketParser.kt  # 0xFCD2 协议高兼容度双路径解码器
│           │   └── BleScanner.kt       # 原生 BluetoothLeScanner 低延迟扫描封装
│           ├── model/
│           │   ├── TelemetryPoint.kt   # 遥测单点数据模型
│           │   ├── MetricStats.kt      # 极值与均值统计计算
│           │   ├── TelemetryFilter.kt  # 3点中值 + 跳变直通 + EMA 0.25s + 滞回防抖
│           │   └── TelemetryHistory.kt # 15000 点高性能环形缓冲与能量积分
│           ├── viewmodel/
│           │   └── MainViewModel.kt    # 响应式状态管理与业务调度
│           └── ui/
│               ├── theme/ (Color.kt, Theme.kt) # Daylight 瓷白极简主题与 Accent 配色
│               ├── components/
│               │   ├── StatusBar.kt    # 呼吸灯状态栏、启停按钮与 RSSI / MAC
│               │   ├── KpiCards.kt     # 4 张核心 KPI 卡片 (电流/功率/电压/电量)
│               │   ├── WaveformView.kt # 原生 Canvas 高性能硬件加速平滑波形
│               │   └── Controls.kt     # 窗口切换 (10s/30s/60s)、通道选择与动作栏
│               └── MainScreen.kt       # 主界面响应式滚动布局整合
└── README.md
```

---

## 4. 导入与编译运行指南

### 4.1 在 Android Studio 中打开
1. 启动 **Android Studio**（推荐 Hedgehog / Iguana / Jellyfish 或更新版本）；
2. 点击 **File -> Open...**，选择本项目中的 `app_android` 文件夹；
3. 等待 Gradle 自动完成依赖同步与配置索引；
4. 将 Android 手机开启开发者模式并连接电脑（或使用支持 BLE 模拟的设备），点击右上角 **Run 'app'** 绿色运行按钮。

### 4.2 命令行构建 APK
GitHub Actions 使用 JDK 17、Gradle 8.5 构建；本地需要安装相同环境，并在 `app_android` 目录下执行：

```bash
gradle --no-daemon :app:assembleArm64Release :app:assembleUniversalRelease
```

生成的 Release APK 文件位于：
```
app/build/outputs/apk/arm64/release/
app/build/outputs/apk/universal/release/
```

---

## 5. 权限说明与适配

针对 Android 各种系统版本实现了自动适配：
- **Android 12+ (API 31+)**：动态申请 `BLUETOOTH_SCAN`（配置 `neverForLocation` 声明不获取物理定位）及 `BLUETOOTH_CONNECT`；
- **Android 6.0 ~ 11**：动态申请 `ACCESS_FINE_LOCATION`（Android 历史系统规范要求扫描 BLE 必须获得位置授权）。
- 软件已在 `AndroidManifest.xml` 中将硬件特性 `android.hardware.bluetooth_le` 标记为必需。
