#pragma once

#include "app_state.hpp"
#include "ble_scanner.hpp"
#include "serial_manager.hpp"
#include <d3d11.h>
#include <vector>

namespace CH570App {

extern ID3D11ShaderResourceView* g_pIconTextureView;

class Dashboard {
public:
    Dashboard(BleScanner& bleScanner);
    void Render();

private:
    void RenderStatusBar();
    void RenderKpiCards();
    void RenderWaveform();
    void RenderBottomTabs();

    void RenderControlAndLogTab();
    void RenderCom1TerminalTab();
    void RenderBleDiagnosticsTab();

    BleScanner& m_bleScanner;

    // Local UI states
    char m_com1SendBuffer[256]{0};
    int m_com1NewlineIndex{1}; // 0=None, 1=\r\n, 2=\n
    char m_customCmdBuffer[64]{0};

    // Reusable relative time and voltage buffers for waveform plotting
    std::vector<double> m_relTime;
    std::vector<double> m_voltage_mv;
};

} // namespace CH570App
