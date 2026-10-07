#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <array>

namespace CH570App {

inline std::string MakeLogTimestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return std::string(buf);
}

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
        UpdateStatsOnly(val);
    }

    void UpdateStatsOnly(double val) {
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
            /* Keep the retention window bounded without shifting the vectors
             * for every sample.  A small batch is removed only when the
             * four-hour storage limit is reached. */
            const size_t prune = (std::min)(size_t{256}, m_time.size());
            m_time.erase(m_time.begin(), m_time.begin() + prune);
            m_voltage.erase(m_voltage.begin(), m_voltage.begin() + prune);
            m_current.erase(m_current.begin(), m_current.begin() + prune);
            m_power.erase(m_power.begin(), m_power.begin() + prune);
        }
        m_time.push_back(pt.timestamp);
        m_voltage.push_back(pt.voltage_v);
        m_current.push_back(pt.current_ma);
        m_power.push_back(pt.power_mw);

        /* Keep the recent 15 minutes lossless.  Older points are only used
         * for long-window trend display, so compact that prefix to one point
         * per second while retaining the full-rate integration below. */
        CompactOlderSamplesLocked(pt.timestamp);

        m_vStats.Update(pt.voltage_v);
        m_iStats.Update(pt.current_ma);
        m_pStats.Update(pt.power_mw);

        // Integrate capacity/energy internally in Ah/Wh; UI converts to mAh/mWh.
        // Trapezoidal integration plus long-double accumulators avoids using
        // only the newest sample and reduces long-session rounding drift.
        if (m_lastEnergyTimestamp >= 0.0) {
            double dt = pt.timestamp - m_lastEnergyTimestamp;
            if (dt > 0.0 && dt < 10.0) { // reject anomalous gap
                long double hours = static_cast<long double>(dt) / 3600.0L;
                m_accumulatedAh += ((m_lastCurrentMa + static_cast<long double>(pt.current_ma)) * 0.5L / 1000.0L) * hours;
                m_accumulatedWh += ((m_lastPowerMw + static_cast<long double>(pt.power_mw)) * 0.5L / 1000.0L) * hours;
            }
        }
        m_lastEnergyTimestamp = pt.timestamp;
        m_lastCurrentMa = static_cast<long double>(pt.current_ma);
        m_lastPowerMw = static_cast<long double>(pt.power_mw);
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
        m_accumulatedAh = 0.0L;
        m_accumulatedWh = 0.0L;
        m_lastEnergyTimestamp = -1.0;
        m_lastCurrentMa = 0.0L;
        m_lastPowerMw = 0.0L;
        m_lastCompactionTimestamp = -1.0;
    }

    void ResetEnergy() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_accumulatedAh = 0.0;
        m_accumulatedWh = 0.0;
        m_lastEnergyTimestamp = -1.0;
        m_lastCurrentMa = 0.0L;
        m_lastPowerMw = 0.0L;
        m_lastCompactionTimestamp = -1.0;
    }

    double GetAccumulatedAh() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        return static_cast<double>(m_accumulatedAh);
    }
    double GetAccumulatedWh() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        return static_cast<double>(m_accumulatedWh);
    }

    // Fast O(K) reverse rolling window statistics matching the chart time window
    void GetWindowStats(double windowSeconds, MetricStats& v, MetricStats& i, MetricStats& p) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        v.Reset();
        i.Reset();
        p.Reset();

        if (m_time.empty()) return;

        double curTime = m_time.back();
        double cutoff = curTime - windowSeconds;

        // Real-time instantaneous metric must always be the newest sample!
        v.current = m_voltage.back();
        i.current = m_current.back();
        p.current = m_power.back();

        for (int k = static_cast<int>(m_time.size()) - 1; k >= 0; --k) {
            if (m_time[k] < cutoff) break;
            v.UpdateStatsOnly(m_voltage[k]);
            i.UpdateStatsOnly(m_current[k]);
            p.UpdateStatsOnly(m_power[k]);
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

    bool GetLatest(TelemetryPoint& pt, double& accumulatedAh, double& accumulatedWh) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        if (m_time.empty()) return false;
        pt.timestamp = m_time.back();
        pt.voltage_v = m_voltage.back();
        pt.current_ma = m_current.back();
        pt.power_mw = m_power.back();
        pt.rssi = 0;
        pt.source = DataSource::DONGLE_COM2;
        accumulatedAh = m_accumulatedAh;
        accumulatedWh = m_accumulatedWh;
        return true;
    }

    // Copy only the newest samples for display-side filtering.  Keeping this
    // separate from the raw vectors ensures plots, CSV and statistics remain
    // lossless while the KPI cards can reject isolated spikes.
    size_t GetRecent(std::array<TelemetryPoint, 3>& out) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        const size_t count = (std::min)(static_cast<size_t>(3), m_time.size());
        for (size_t i = 0; i < count; ++i) {
            const size_t k = m_time.size() - count + i;
            out[i].timestamp = m_time[k];
            out[i].voltage_v = m_voltage[k];
            out[i].current_ma = m_current[k];
            out[i].power_mw = m_power[k];
            out[i].rssi = 0;
            out[i].source = DataSource::DONGLE_COM2;
        }
        return count;
    }

private:
    void CompactOlderSamplesLocked(double now) {
        constexpr double kDetailedSeconds = 15.0 * 60.0;
        if (m_time.size() < 2 ||
            (m_lastCompactionTimestamp >= 0.0 &&
             now - m_lastCompactionTimestamp < 1.0)) {
            return;
        }
        m_lastCompactionTimestamp = now;

        const double cutoff = now - kDetailedSeconds;
        size_t boundary = 0;
        while (boundary < m_time.size() && m_time[boundary] < cutoff) ++boundary;
        if (boundary < 2) return;

        std::vector<double> time;
        std::vector<double> voltage;
        std::vector<double> current;
        std::vector<double> power;
        time.reserve(boundary + (m_time.size() - boundary));
        voltage.reserve(time.capacity());
        current.reserve(time.capacity());
        power.reserve(time.capacity());

        size_t i = 0;
        while (i < boundary) {
            const double second = std::floor(m_time[i]);
            size_t j = i + 1;
            while (j < boundary && std::floor(m_time[j]) == second) ++j;
            const size_t selected = j - 1; // newest sample in this second
            time.push_back(m_time[selected]);
            voltage.push_back(m_voltage[selected]);
            current.push_back(m_current[selected]);
            power.push_back(m_power[selected]);
            i = j;
        }

        for (i = boundary; i < m_time.size(); ++i) {
            time.push_back(m_time[i]);
            voltage.push_back(m_voltage[i]);
            current.push_back(m_current[i]);
            power.push_back(m_power[i]);
        }
        m_time.swap(time);
        m_voltage.swap(voltage);
        m_current.swap(current);
        m_power.swap(power);
    }

    size_t m_capacity;
    std::mutex m_mutex;
    std::vector<double> m_time;
    std::vector<double> m_voltage;
    std::vector<double> m_current;
    std::vector<double> m_power;

    MetricStats m_vStats;
    MetricStats m_iStats;
    MetricStats m_pStats;

    long double m_accumulatedAh{0.0L};
    long double m_accumulatedWh{0.0L};
    double m_lastEnergyTimestamp{-1.0};
    long double m_lastCurrentMa{0.0L};
    long double m_lastPowerMw{0.0L};
    double m_lastCompactionTimestamp{-1.0};
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
    uint32_t full_scale_ma{20000};
    uint32_t link_led_max_duty{255};
    uint32_t averaging_count{64};
    uint32_t shunt_mohm{20}; // Hardware spec default is R104 = 20 mΩ
    uint32_t ble_adv_hz{4};
    bool config_loaded{false};
};

struct RfTestResult {
    bool valid{false};
    bool running{false};
    uint32_t bytes{0};
    uint32_t elapsed_ms{0};
    uint32_t rate_bps{0};
    uint32_t avg_us{0};
    uint32_t min_us{0};
    uint32_t max_us{0};
    uint32_t packets{0};
    double started_at{0.0};
    uint32_t requested_ms{0};
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
    /* After the 15-minute full-rate region is compacted to 1 Hz, 200k slots
     * cover four hours with generous margin while keeping startup memory low. */
    TelemetryHistory history{200000};

    // Active source selection
    std::atomic<DataSource> selectedSource{DataSource::AUTO};
    std::atomic<DataSource> activeSource{DataSource::AUTO};

    // BLE states
    std::atomic<bool> bleScanning{false};
    std::atomic<bool> bleBeaconReceived{false};
    std::atomic<int16_t> bleLastRssi{0};
    std::atomic<uint64_t> blePacketCount{0};
    double bleLastTimestamp{0.0};
    std::string bleDeviceMac{"CA:57:09:1E:A2:26"};

    // Dongle COM2 (Telemetry & Control) states
    std::atomic<bool> dongleConnected{false};
    std::string donglePortName{""};
    std::string dongleCOM2Port{""};
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
    std::atomic<uint64_t> dongleCfgRevision{0};
    std::mutex dongleCfgMutex;
    std::deque<std::string> controlLog;
    std::mutex controlLogMutex;

    // Latest on-demand 2.4 GHz throughput/latency test result.
    RfTestResult rfTest;
    std::mutex rfTestMutex;

    void AddControlLog(const std::string& msg) {
        std::lock_guard<std::mutex> lock(controlLogMutex);
        controlLog.push_back("[" + MakeLogTimestamp() + "] " + msg);
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
    std::deque<std::string> com1LogLines;
    std::string com1LogAccumulator;
    std::mutex com1RxMutex;
    bool com1HexMode{false};
    bool com1AutoScroll{true};

    void PushCom1Rx(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(com1RxMutex);
        for (size_t i = 0; i < len; ++i) {
            com1RxBuffer.push_back(data[i]);
            com1LogAccumulator.push_back(static_cast<char>(data[i]));
        }
        size_t newline = std::string::npos;
        while ((newline = com1LogAccumulator.find('\n')) != std::string::npos) {
            std::string line = com1LogAccumulator.substr(0, newline);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            com1LogLines.push_back("[" + MakeLogTimestamp() + "] " + line);
            com1LogAccumulator.erase(0, newline + 1);
        }
        if (com1LogAccumulator.size() >= 256) {
            com1LogLines.push_back("[" + MakeLogTimestamp() + "] " + com1LogAccumulator);
            com1LogAccumulator.clear();
        }
        while (com1LogLines.size() > 400) com1LogLines.pop_front();
        if (com1RxBuffer.size() > 65536) {
            com1RxBuffer.erase(com1RxBuffer.begin(), com1RxBuffer.begin() + (com1RxBuffer.size() - 65536));
        }
        com1RxBytes += len;
        NotifyCom1Activity();
    }

    void ClearCom1Rx() {
        std::lock_guard<std::mutex> lock(com1RxMutex);
        com1RxBuffer.clear();
        com1LogLines.clear();
        com1LogAccumulator.clear();
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
