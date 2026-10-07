#pragma once

#include "app_state.hpp"
#include "ble_scanner.hpp"
#include "serial_manager.hpp"
#include <d3d11.h>
#include <vector>
#include <fstream>
#include <string>
#include <ctime>
#include <array>

namespace CH570App {

extern ID3D11ShaderResourceView* g_pIconTextureView;

class Dashboard {
public:
    Dashboard(BleScanner& bleScanner);
    ~Dashboard();
    void Render();

private:
    void RenderStatusBar();
    void RenderKpiCards();
    void RenderWaveform();
    void RenderBottomTabs();

    void RenderControlAndLogTab();
    void RenderFirmwareTab();
    void RenderCom1TerminalTab();
    void RenderBleDiagnosticsTab();

    BleScanner& m_bleScanner;

    // Local UI states
    char m_com1SendBuffer[256]{0};
    int m_com1NewlineIndex{1}; // 0=None, 1=\r\n, 2=\n
    char m_customCmdBuffer[64]{0};

    // Reusable relative time and voltage buffers for waveform plotting
    std::vector<double> m_relTime;
    std::vector<double> m_current_plot;
    std::vector<double> m_power_plot;
    std::vector<double> m_voltage_mv;

    // CSV recording state. Records are emitted from the newest sample at the
    // selected interval and rotated when the local clock enters a new hour.
    bool m_csvEnabled{false};
    int m_csvIntervalIndex{2};
    double m_csvLastSampleTimestamp{-1.0};
    std::string m_csvHourKey;
    std::ofstream m_csvFile;
    std::string m_csvPath;
    void ExportLatestCsvIfDue();
    void CloseCsv();
    bool OpenCsvForCurrentHour(const std::tm& localTm, long long epochMs);

    // Display-only filter state. It advances once per telemetry timestamp,
    // never once per ImGui frame.
    double m_smoothCurrent{0.0};
    double m_smoothPower{0.0};
    double m_smoothVoltage{0.0};
    double m_filterLastSampleTimestamp{-1.0};
    bool m_smoothInitialized{false};
    bool m_currentInA{false};
    bool m_powerInW{false};
    bool m_voltageInV{false};
};

} // namespace CH570App
