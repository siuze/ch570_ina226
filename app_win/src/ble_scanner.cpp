#include "ble_scanner.hpp"
#include "app_state.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Storage::Streams;

namespace CH570App {

class BleScannerImpl {
public:
    BleScannerImpl() {
        try {
            m_watcher = BluetoothLEAdvertisementWatcher();
            m_watcher.ScanningMode(BluetoothLEScanningMode::Active);

            // Register advertisement received handler
            m_token = m_watcher.Received([this](BluetoothLEAdvertisementWatcher const&, BluetoothLEAdvertisementReceivedEventArgs const& args) {
                OnAdvertisementReceived(args);
            });
        } catch (winrt::hresult_error const& ex) {
            AppState::Instance().AddControlLog("[BLE] Init failed: " + winrt::to_string(ex.message()));
        }
    }

    ~BleScannerImpl() {
        Stop();
    }

    bool Start() {
        try {
            if (m_watcher) {
                m_watcher.Start();
                AppState::Instance().bleScanning = true;
                AppState::Instance().AddControlLog("[BLE] Active scanning started (UUID 0xFCD2)");
                return true;
            }
        } catch (winrt::hresult_error const& ex) {
            AppState::Instance().AddControlLog("[BLE] Start failed: " + winrt::to_string(ex.message()));
        }
        return false;
    }

    void Stop() {
        try {
            if (m_watcher && AppState::Instance().bleScanning) {
                m_watcher.Stop();
                AppState::Instance().bleScanning = false;
                AppState::Instance().AddControlLog("[BLE] Scanner stopped");
            }
        } catch (...) {
        }
    }

    bool IsScanning() const {
        if (!m_watcher) return false;
        return m_watcher.Status() == BluetoothLEAdvertisementWatcherStatus::Started;
    }

private:
    void OnAdvertisementReceived(BluetoothLEAdvertisementReceivedEventArgs const& args) {
        auto adv = args.Advertisement();
        if (!adv) return;

        // Check 16-bit Service Data sections (type 0x16)
        auto sections = adv.GetSectionsByType(0x16);
        for (auto const& section : sections) {
            auto dataBuffer = section.Data();
            if (!dataBuffer || dataBuffer.Length() < 6) {
                continue;
            }

            DataReader reader = DataReader::FromBuffer(dataBuffer);
            std::vector<uint8_t> bytes(reader.UnconsumedBufferLength());
            reader.ReadBytes(winrt::array_view<uint8_t>(bytes));

            // Verify Service UUID: 0xFCD2 (Little-endian: bytes[0] == 0xD2, bytes[1] == 0xFC)
            if (bytes[0] == 0xD2 && bytes[1] == 0xFC) {
                // Bytes 2..3: int16_t shunt_raw (signed little-endian, 1 LSB = 0.125 mA)
                int16_t shunt_raw = static_cast<int16_t>(bytes[2] | (bytes[3] << 8));
                // Bytes 4..5: uint16_t bus_raw (unsigned little-endian, 1 LSB = 1.25 mV)
                uint16_t bus_raw = static_cast<uint16_t>(bytes[4] | (bytes[5] << 8));

                float current_ma = shunt_raw * 0.125f;
                float voltage_v = (bus_raw * 1.25f) / 1000.0f;
                float power_mw = voltage_v * current_ma;

                auto& state = AppState::Instance();
                double now = state.GetElapsedSeconds();

                state.bleBeaconReceived = true;
                state.bleLastRssi = args.RawSignalStrengthInDBm();
                state.blePacketCount++;
                state.bleLastTimestamp = now;

                // Format Bluetooth Address
                uint64_t addr = args.BluetoothAddress();
                std::ostringstream oss;
                oss << std::uppercase << std::hex << std::setfill('0')
                    << std::setw(2) << ((addr >> 40) & 0xFF) << ":"
                    << std::setw(2) << ((addr >> 32) & 0xFF) << ":"
                    << std::setw(2) << ((addr >> 24) & 0xFF) << ":"
                    << std::setw(2) << ((addr >> 16) & 0xFF) << ":"
                    << std::setw(2) << ((addr >> 8) & 0xFF) << ":"
                    << std::setw(2) << (addr & 0xFF);
                state.bleDeviceMac = oss.str();

                // Determine whether to push to waveform
                DataSource curSrc = state.selectedSource.load();
                bool accept = false;
                if (curSrc == DataSource::BLE_BEACON) {
                    accept = true;
                    state.activeSource = DataSource::BLE_BEACON;
                } else if (curSrc == DataSource::AUTO) {
                    // In AUTO mode, use BLE if Dongle is not connected or Dongle hasn't sent data for 1.5s
                    if (!state.dongleConnected || (now - state.dongleLastTimestamp > 1.5)) {
                        accept = true;
                        state.activeSource = DataSource::BLE_BEACON;
                    }
                }

                if (accept && !state.isPaused) {
                    TelemetryPoint pt;
                    pt.timestamp = now;
                    pt.voltage_v = voltage_v;
                    pt.current_ma = current_ma;
                    pt.power_mw = power_mw;
                    pt.rssi = args.RawSignalStrengthInDBm();
                    pt.source = DataSource::BLE_BEACON;
                    state.history.Push(pt);
                }
                break;
            }
        }
    }

    BluetoothLEAdvertisementWatcher m_watcher{nullptr};
    winrt::event_token m_token{};
};

BleScanner::BleScanner() : m_impl(std::make_unique<BleScannerImpl>()) {}
BleScanner::~BleScanner() = default;

bool BleScanner::Start() {
    return m_impl->Start();
}

void BleScanner::Stop() {
    m_impl->Stop();
}

bool BleScanner::IsScanning() const {
    return m_impl->IsScanning();
}

} // namespace CH570App
