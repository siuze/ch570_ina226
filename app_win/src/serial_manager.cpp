#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>

#include "serial_manager.hpp"
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "setupapi.lib")

// SPDRP_BUSREPORTEDDEVICEDESC (0x64) is not defined in some older SDK headers.
#ifndef SPDRP_BUSREPORTEDDEVICEDESC
#define SPDRP_BUSREPORTEDDEVICEDESC 0x00000064
#endif

namespace CH570App {

SerialManager::SerialManager() {}

SerialManager::~SerialManager() {
    CloseDongleCOM2();
    CloseCOM1();
}

std::vector<SerialPortInfo> SerialManager::EnumeratePorts() {
    std::vector<SerialPortInfo> ports;

    // Enumerate COM ports class
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return ports;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
        wchar_t friendlyNameW[256] = {0};
        wchar_t busDescW[256] = {0};
        char busDesc[512] = {0};
        char hardwareId[512] = {0};
        char portName[64] = {0};

        // Friendly Name (query as Unicode UTF-16, then convert to UTF-8)
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, nullptr,
                                          reinterpret_cast<PBYTE>(friendlyNameW), sizeof(friendlyNameW), nullptr);
        char friendlyName[512] = {0};
        if (friendlyNameW[0] != L'\0') {
            WideCharToMultiByte(CP_UTF8, 0, friendlyNameW, -1, friendlyName, sizeof(friendlyName), nullptr, nullptr);
        }

        // Hardware ID
        SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr,
                                          reinterpret_cast<PBYTE>(hardwareId), sizeof(hardwareId), nullptr);

        // Bus-reported device description: the USB iProduct string, or the per-function IAD
        // iFunction string when Windows surfaces it. Used as a label / secondary distinguisher.
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_BUSREPORTEDDEVICEDESC, nullptr,
                                          reinterpret_cast<PBYTE>(busDescW), sizeof(busDescW), nullptr);
        if (busDescW[0] != L'\0') {
            WideCharToMultiByte(CP_UTF8, 0, busDescW, -1, busDesc, sizeof(busDesc), nullptr, nullptr);
        }

        // Open device registry key to query actual port name (e.g., "COM3")
        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, (DWORD)DIREG_DEV, KEY_READ);
        if (hKey != INVALID_HANDLE_VALUE) {
            DWORD size = sizeof(portName);
            DWORD type = REG_SZ;
            RegQueryValueExA(hKey, "PortName", nullptr, &type, reinterpret_cast<LPBYTE>(portName), &size);
            RegCloseKey(hKey);
        }

        if (portName[0] != '\0') {
            SerialPortInfo info;
            info.portName = portName;
            info.friendlyName = friendlyName[0] ? friendlyName : portName;
            info.hardwareId = hardwareId;
            info.busReportedDesc = busDesc;

            // Check if this is CH342 / CH570 Dongle (VID 1A86, PID FE0C)
            std::string hidUpper = info.hardwareId;
            for (auto& c : hidUpper) c = (char)toupper(c);

            bool isCh342 = (info.friendlyName.find("CH342") != std::string::npos);
            bool hasFe0c = (hidUpper.find("VID_1A86&PID_FE0C") != std::string::npos);
            bool has1a86 = (hidUpper.find("VID_1A86") != std::string::npos);

            if (hasFe0c || (has1a86 && isCh342)) {
                if (hidUpper.find("MI_00") != std::string::npos || info.friendlyName.find("COM#1") != std::string::npos) {
                    info.isDongleCOM1 = true;
                } else if (hidUpper.find("MI_02") != std::string::npos || info.friendlyName.find("COM#2") != std::string::npos) {
                    info.isDongleCOM2 = true;
                } else {
                    // Fallback: distinguish by the bus-reported / friendly description, which
                    // reflects the per-function iFunction string ("CH570 Wireless UART" vs
                    // "CH570 Sensor Control") when Windows surfaces it. Not guaranteed across
                    // Windows versions, so only used when the deterministic MI_xx tag is absent.
                    std::string desc = info.busReportedDesc + " " + info.friendlyName;
                    if (desc.find("Sensor Control") != std::string::npos) {
                        info.isDongleCOM2 = true;
                    } else if (desc.find("Wireless UART") != std::string::npos) {
                        info.isDongleCOM1 = true;
                    }
                }
            }

            ports.push_back(info);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return ports;
}

void SerialManager::AutoDetectDongle() {
    auto ports = EnumeratePorts();
    std::string detectedCom1 = "";
    std::string detectedCom2 = "";

    for (const auto& p : ports) {
        if (p.isDongleCOM2) {
            detectedCom2 = p.portName;
        } else if (p.isDongleCOM1) {
            detectedCom1 = p.portName;
        }
    }

    // Fallback: If not tagged by MI_00/MI_02, but there are two ports with same VID
    if (detectedCom2.empty() && ports.size() >= 2) {
        for (const auto& p : ports) {
            if (p.hardwareId.find("1A86") != std::string::npos) {
                if (detectedCom1.empty()) detectedCom1 = p.portName;
                else if (detectedCom2.empty()) detectedCom2 = p.portName;
            }
        }
    }

    if (!detectedCom2.empty()) {
        AppState::Instance().donglePortName = detectedCom2;
        if (!IsDongleCOM2Open()) {
            OpenDongleCOM2(detectedCom2);
        } else {
            SendCommand("CMD:VER?"); // manual refresh (e.g. Probe bound after first query)
        }
    }

    if (!detectedCom1.empty()) {
        AppState::Instance().com1PortName = detectedCom1;
    }
}

bool SerialManager::OpenDongleCOM2(const std::string& portName) {
    CloseDongleCOM2();

    std::string devPath = "\\\\.\\" + portName;
    HANDLE hCom = CreateFileA(devPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom == INVALID_HANDLE_VALUE) {
        AppState::Instance().AddControlLog("[DONGLE] Failed to open " + portName);
        return false;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (GetCommState(hCom, &dcb)) {
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        SetCommState(hCom, &dcb);
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 25; // 25ms timeout
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(hCom, &timeouts);

    m_hCom2 = hCom;
    m_com2Running = true;
    AppState::Instance().dongleConnected = true;
    AppState::Instance().donglePortName = portName;
    AppState::Instance().AddControlLog("[DONGLE] Opened " + portName + " at 115200 8N1");

    m_com2Thread = std::thread(&SerialManager::COM2ThreadFunc, this);
    return true;
}

void SerialManager::CloseDongleCOM2() {
    m_com2Running = false;
    if (m_com2Thread.joinable()) {
        m_com2Thread.join();
    }
    if (m_hCom2 && m_hCom2 != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCom2);
        m_hCom2 = nullptr;
    }
    AppState::Instance().dongleConnected = false;
    AppState::Instance().ClearFwVersions();
}

bool SerialManager::SendCommand(const std::string& cmd) {
    if (!m_hCom2 || m_hCom2 == INVALID_HANDLE_VALUE) {
        AppState::Instance().AddControlLog("[DONGLE] Port not open, cannot send: " + cmd);
        return false;
    }

    std::string payload = cmd;
    if (payload.empty() || payload.back() != '\n') {
        payload += "\r\n";
    }

    DWORD written = 0;
    BOOL res = WriteFile(m_hCom2, payload.c_str(), static_cast<DWORD>(payload.length()), &written, NULL);
    if (res && written > 0) {
        AppState::Instance().AddControlLog("[TX -> DONGLE] " + cmd);
        return true;
    }
    return false;
}

void SerialManager::COM2ThreadFunc() {
    char buf[256];
    std::string lineAcc = "";

    // Auto-query firmware version after connect. Two attempts cover the case where the
    // Probe has not finished binding to the Dongle at the moment of the first query.
    auto threadStart = std::chrono::steady_clock::now();
    bool sentVerQuery1 = false, sentVerQuery2 = false;

    while (m_com2Running.load()) {
        long long elapsedMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - threadStart).count();
        if (!sentVerQuery1 && elapsedMs >= 300) { SendCommand("CMD:VER?"); sentVerQuery1 = true; }
        if (!sentVerQuery2 && elapsedMs >= 2000) { SendCommand("CMD:VER?"); sentVerQuery2 = true; }

        DWORD bytesRead = 0;
        if (ReadFile(m_hCom2, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
            for (DWORD i = 0; i < bytesRead; ++i) {
                char c = buf[i];
                if (c == '\n') {
                    if (!lineAcc.empty() && lineAcc.back() == '\r') {
                        lineAcc.pop_back();
                    }
                    if (!lineAcc.empty()) {
                        ParseCOM2Line(lineAcc);
                        lineAcc.clear();
                    }
                } else {
                    lineAcc.push_back(c);
                    if (lineAcc.size() > 512) {
                        lineAcc.clear(); // Safety cap
                    }
                }
            }
        }
    }
}

void SerialManager::ParseCOM2Line(const std::string& line) {
    auto& state = AppState::Instance();

    // Firmware version report: "[VER] Dongle: 2026-09-29 r01" / "[VER] Probe:  2026-09-29 r01"
    if (line.find("[VER]") != std::string::npos) {
        size_t colon = line.find(':');
        if (colon != std::string::npos && colon + 1 < line.size()) {
            std::string ver = line.substr(colon + 1);
            size_t b = ver.find_first_not_of(" \t");
            size_t e = ver.find_last_not_of(" \t");
            if (b != std::string::npos && e != std::string::npos && e >= b) {
                ver = ver.substr(b, e - b + 1);
                bool isProbe = (line.find("Probe") != std::string::npos);
                if (!ver.empty() && ver.find("not connected") == std::string::npos) {
                    state.SetFwVersion(isProbe, ver);
                }
            }
        }
        state.AddControlLog(line);
        return;
    }

    // Check for 2.4G RSSI report: e.g. "RSSI:-68 dBm" or "RSSI:-68" or "[RSSI] -68"
    int rssiVal = 0;
    if (sscanf_s(line.c_str(), "RSSI:%d", &rssiVal) == 1 ||
        sscanf_s(line.c_str(), "RSSI: %d", &rssiVal) == 1 ||
        sscanf_s(line.c_str(), "[RSSI] %d", &rssiVal) == 1) {
        state.dongleRssi = rssiVal;
        state.dongleRssiValid = true;
        state.dongleLastRssiTimestamp = state.GetElapsedSeconds();
        return;
    }

    // Check if line is telemetry: "V:5.012V, I:123.125mA, P:617.16mW"
    float v = 0.0f, i = 0.0f, p = 0.0f;
    int parsed = sscanf_s(line.c_str(), "V:%fV, I:%fmA, P:%fmW", &v, &i, &p);
    if (parsed == 3 || parsed == 2) {
        if (parsed == 2) {
            p = v * i;
        }

        double now = state.GetElapsedSeconds();
        state.dongleLastTimestamp = now;
        state.donglePacketCount++;

        DataSource curSrc = state.selectedSource.load();
        bool accept = false;
        if (curSrc == DataSource::DONGLE_COM2 || curSrc == DataSource::AUTO) {
            accept = true;
            state.activeSource = DataSource::DONGLE_COM2;
        }

        if (accept && !state.isPaused) {
            TelemetryPoint pt;
            pt.timestamp = now;
            pt.voltage_v = v;
            pt.current_ma = i;
            pt.power_mw = p;
            pt.rssi = 0;
            pt.source = DataSource::DONGLE_COM2;
            state.history.Push(pt);
        }
    } else {
        // Control response or log message
        state.AddControlLog(line);

        // Check for config query response: e.g. "[CFG] RATE=50, FSC=1000, AVG=16, SHUNT=10"
        if (line.find("[CFG]") != std::string::npos) {
            uint32_t rate = 0, fsc = 0, avg = 0, shunt = 0;
            if (sscanf_s(line.c_str(), "[CFG] RATE=%u, FSC=%u, AVG=%u, SHUNT=%u", &rate, &fsc, &avg, &shunt) >= 1) {
                std::lock_guard<std::mutex> lock(state.dongleCfgMutex);
                if (rate > 0) state.dongleCfg.sampling_rate_ms = rate;
                if (fsc > 0) state.dongleCfg.full_scale_ma = fsc;
                if (avg > 0) state.dongleCfg.averaging_count = avg;
                if (shunt > 0) state.dongleCfg.shunt_mohm = shunt;
                state.dongleCfg.config_loaded = true;
            }
        }
    }
}

// -------------------------------------------------------------
// COM1 Target MCU UART Passthrough
// -------------------------------------------------------------
bool SerialManager::OpenCOM1(const std::string& portName, uint32_t baudRate) {
    CloseCOM1();

    std::string devPath = "\\\\.\\" + portName;
    HANDLE hCom = CreateFileA(devPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom == INVALID_HANDLE_VALUE) {
        AppState::Instance().AddControlLog("[COM1] Failed to open " + portName);
        return false;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (GetCommState(hCom, &dcb)) {
        dcb.BaudRate = baudRate;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        SetCommState(hCom, &dcb);
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(hCom, &timeouts);

    m_hCom1 = hCom;
    m_com1Running = true;
    AppState::Instance().com1Open = true;
    AppState::Instance().com1PortName = portName;
    AppState::Instance().com1BaudRate = baudRate;
    AppState::Instance().AddControlLog("[COM1] Opened " + portName + " at " + std::to_string(baudRate) + " baud");

    m_com1Thread = std::thread(&SerialManager::COM1ThreadFunc, this);
    return true;
}

void SerialManager::CloseCOM1() {
    m_com1Running = false;
    if (m_com1Thread.joinable()) {
        m_com1Thread.join();
    }
    if (m_hCom1 && m_hCom1 != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCom1);
        m_hCom1 = nullptr;
    }
    AppState::Instance().com1Open = false;
}

bool SerialManager::SendCOM1(const uint8_t* data, size_t len) {
    if (!m_hCom1 || m_hCom1 == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL res = WriteFile(m_hCom1, data, static_cast<DWORD>(len), &written, NULL);
    if (res && written > 0) {
        AppState::Instance().com1TxBytes += written;
        AppState::Instance().NotifyCom1Activity();
        return true;
    }
    return false;
}

bool SerialManager::SendCOM1String(const std::string& text) {
    return SendCOM1(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

void SerialManager::COM1ThreadFunc() {
    uint8_t buf[1024];
    while (m_com1Running.load()) {
        DWORD bytesRead = 0;
        if (ReadFile(m_hCom1, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
            AppState::Instance().PushCom1Rx(buf, bytesRead);
        }
    }
}

} // namespace CH570App
