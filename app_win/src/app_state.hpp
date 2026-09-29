#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <chrono>
#include <atomic>
#include <algorithm>

namespace CH570App {

enum class DataSource {
    BLE_BEACON,
    DONGLE_COM2,
    AUTO
};

struct TelemetryPoint {
    double timestamp;   // Relative seconds since app start
    double voltage_v;   // Bus voltage (V)
    double current_ma;  // Shunt current (mA)
    double power_mw;    // Calculated power (mW)
    int16_t rssi;       // For BLE (-127..0 dBm), 0 for COM2
    DataSource source;  // BLE_BEACON or DONGLE_COM2
};

struct MetricStats {
    double current{0.0};
    double min_val{0.0};
    double max_val{0.0};
    double avg_val{0.0};
    double sum{0.0};
    uint64_t count{0};

    void Update(double val) {
        current = val;
        if (count == 0) {
            min_val = val;
            max_val = val;
            sum = val;
            count = 1;
        } else {
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
            sum += val;
            count++;
        }
        avg_val = sum / count;
    }

    void Reset() {
        current = 0.0;
        min_val = 0.0;
        max_val = 0.0;
        avg_val = 0.0;
        sum = 0.0;
        count = 0;
    }
};

// Fixed-capacity Ring Buffer for high-performance ImPlot scrolling waveforms
class TelemetryHistory {
public:
    explicit TelemetryHistory(size_t capacity = 10000)
        : m_capacity(capacity) {
        m_time.reserve(capacity);
        m_voltage.reserve(capacity);
        m_current.reserve(capacity);
        m_power.reserve(capacity);
    }

    void Push(const TelemetryPoint& pt) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_time.size() >= m_capacity) {
            m_time.erase(m_time.begin());
            m_voltage.erase(m_voltage.begin());
            m_current.erase(m_current.begin());
            m_power.erase(m_power.begin());
        }
        m_time.push_back(pt.timestamp);
        m_voltage.push_back(pt.voltage_v);
        m_current.push_back(pt.current_ma);
        m_power.push_back(pt.power_mw);

        m_vStats.Update(pt.voltage_v);
        m_iStats.Update(pt.current_ma);
        m_pStats.Update(pt.power_mw);

        // Integrate Capacity (Ah) and Energy (Wh)
        if (m_lastEnergyTimestamp >= 0.0) {
            double dt = pt.timestamp - m_lastEnergyTimestamp;
            if (dt > 0.0 && dt < 10.0) { // reject anomalous gap
                double hours = dt / 3600.0;
                m_accumulatedAh += (pt.current_ma / 1000.0) * hours;
                m_accumulatedWh += (pt.power_mw / 1000.0) * hours;
            }
        }
        m_lastEnergyTimestamp = pt.timestamp;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_time.clear();
        m_voltage.clear();
        m_current.clear();
        m_power.clear();
        m_vStats.Reset();
        m_iStats.Reset();
        m_pStats.Reset();
    }

    void ResetEnergy() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_accumulatedAh = 0.0;
        m_accumulatedWh = 0.0;
        m_lastEnergyTimestamp = -1.0;
    }

    double GetAccumulatedAh() const { return m_accumulatedAh; }
    double GetAccumulatedWh() const { return m_accumulatedWh; }

    // Fast O(K) reverse rolling window statistics matching the chart time window
    void GetWindowStats(double windowSeconds, MetricStats& v, MetricStats& i, MetricStats& p) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        v.Reset();
        i.Reset();
        p.Reset();

        if (m_time.empty()) return;

        double curTime = m_time.back();
        double cutoff = curTime - windowSeconds;

        for (int k = static_cast<int>(m_time.size()) - 1; k >= 0; --k) {
            if (m_time[k] < cutoff) break;
            v.Update(m_voltage[k]);
            i.Update(m_current[k]);
            p.Update(m_power[k]);
        }
    }

    size_t Size() const {
        return m_time.size();
    }

    std::mutex& GetMutex() { return m_mutex; }
    const std::vector<double>& GetTime() const { return m_time; }
    const std::vector<double>& GetVoltage() const { return m_voltage; }
    const std::vector<double>& GetCurrent() const { return m_current; }
    const std::vector<double>& GetPower() const { return m_power; }

    MetricStats GetVStats() const { return m_vStats; }
    MetricStats GetIStats() const { return m_iStats; }
    MetricStats GetPStats() const { return m_pStats; }

private:
    size_t m_capacity;
    std::mutex m_mutex;
    std::vector<double> m_time;
    std::vector<double> m_voltage;
    std::vector<double> m_current;
    std::vector<double> m_power;

    MetricStats m_vStats;
    MetricStats m_iStats;
    MetricStats m_pStats;

    double m_accumulatedAh{0.0};
    double m_accumulatedWh{0.0};
    double m_lastEnergyTimestamp{-1.0};
};

// Serial port discovery info
struct SerialPortInfo {
    std::string portName;     // e.g. "COM3"
    std::string friendlyName; // e.g. "USB-Enhanced-SERIAL CH342 (COM3)"
    std::string hardwareId;   // e.g. "USB\VID_1A86&PID_FE0C&MI_00"
    std::string busReportedDesc; // Windows "Bus reported device description" (from iProduct / IAD iFunction)
    bool isDongleCOM1{false}; // MI_00 Target MCU Passthrough
    bool isDongleCOM2{false}; // MI_02 Telemetry & Control
};

// Dongle Hardware Configuration
struct DongleConfig {
    uint32_t sampling_rate_ms{50};
    uint32_t full_scale_ma{1000};
    uint32_t averaging_count{16};
    uint32_t shunt_mohm{20}; // Hardware spec default is R104 = 20 mΩ
    bool config_loaded{false};
};

// Central Application State
class AppState {
public:
    static AppState& Instance() {
        static AppState s_instance;
        return s_instance;
    }

    // High resolution timer
    double GetElapsedSeconds() const {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> diff = now - m_startTime;
        return diff.count();
    }

    // Telemetry storage
    TelemetryHistory history{15000};

    // Active source selection
    std::atomic<DataSource> selectedSource{DataSource::AUTO};
    std::atomic<DataSource> activeSource{DataSource::AUTO};

    // BLE states
    std::atomic<bool> bleScanning{false};
    std::atomic<bool> bleBeaconReceived{false};
    std::atomic<int16_t> bleLastRssi{0};
    std::atomic<uint64_t> blePacketCount{0};
    double bleLastTimestamp{0.0};
    std::string bleDeviceMac{"57:44:33:22:11:C0"};

    // Dongle COM2 (Telemetry & Control) states
    std::atomic<bool> dongleConnected{false};
    std::string donglePortName{""};
    std::atomic<uint64_t> donglePacketCount{0};
    double dongleLastTimestamp{0.0};
    std::atomic<int> dongleRssi{0};
    std::atomic<bool> dongleRssiValid{false};
    double dongleLastRssiTimestamp{0.0};

    // Firmware versions (auto-queried via "CMD:VER?" after COM2 connects)
    std::string dongleFwVersion;
    std::string probeFwVersion;
    std::mutex fwVersionMutex;

    void SetFwVersion(bool isProbe, const std::string& ver) {
        std::lock_guard<std::mutex> lock(fwVersionMutex);
        if (isProbe) probeFwVersion = ver; else dongleFwVersion = ver;
    }
    void GetFwVersions(std::string& dongle, std::string& probe) {
        std::lock_guard<std::mutex> lock(fwVersionMutex);
        dongle = dongleFwVersion;
        probe = probeFwVersion;
    }
    void ClearFwVersions() {
        std::lock_guard<std::mutex> lock(fwVersionMutex);
        dongleFwVersion.clear();
        probeFwVersion.clear();
    }

    // Dongle Hardware Config
    DongleConfig dongleCfg;
    std::mutex dongleCfgMutex;
    std::deque<std::string> controlLog;
    std::mutex controlLogMutex;

    void AddControlLog(const std::string& msg) {
        std::lock_guard<std::mutex> lock(controlLogMutex);
        controlLog.push_back(msg);
        if (controlLog.size() > 200) {
            controlLog.pop_front();
        }
    }

    void ClearControlLog() {
        std::lock_guard<std::mutex> lock(controlLogMutex);
        controlLog.clear();
    }

    // COM1 (Target MCU Passthrough) states
    std::atomic<bool> com1Open{false};
    std::string com1PortName{""};
    uint32_t com1BaudRate{115200};
    std::atomic<uint64_t> com1RxBytes{0};
    std::atomic<uint64_t> com1TxBytes{0};
    std::atomic<uint64_t> lastCom1ActivityMs{0};

    void NotifyCom1Activity() {
        auto now = std::chrono::steady_clock::now();
        uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count();
        lastCom1ActivityMs.store(ms, std::memory_order_relaxed);
    }

    std::deque<uint8_t> com1RxBuffer;
    std::mutex com1RxMutex;
    bool com1HexMode{false};
    bool com1AutoScroll{true};

    void PushCom1Rx(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(com1RxMutex);
        for (size_t i = 0; i < len; ++i) {
            com1RxBuffer.push_back(data[i]);
        }
        if (com1RxBuffer.size() > 65536) {
            com1RxBuffer.erase(com1RxBuffer.begin(), com1RxBuffer.begin() + (com1RxBuffer.size() - 65536));
        }
        com1RxBytes += len;
        NotifyCom1Activity();
    }

    void ClearCom1Rx() {
        std::lock_guard<std::mutex> lock(com1RxMutex);
        com1RxBuffer.clear();
        com1RxBytes = 0;
        com1TxBytes = 0;
    }

    // UI waveform view settings (Priority: Current, Power, Voltage)
    bool isPaused{false};
    float timeWindowSeconds{30.0f};
    bool showCurrent{true};
    bool showPower{true};
    bool showVoltage{false}; // Default false: bus voltage barely changes

    bool resetY1{false};
    bool resetY2{false};
    bool resetY3{false};

private:
    AppState() {
        m_startTime = std::chrono::steady_clock::now();
    }
    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace CH570App
