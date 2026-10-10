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
#include <fstream>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")

// SPDRP_BUSREPORTEDDEVICEDESC (0x64) is not defined in some older SDK headers.
#ifndef SPDRP_BUSREPORTEDDEVICEDESC
#define SPDRP_BUSREPORTEDDEVICEDESC 0x00000064
#endif

namespace CH570App {

SerialManager::SerialManager() {}

SerialManager::~SerialManager() {
    if (m_fwThread.joinable()) m_fwThread.join();
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
        AppState::Instance().dongleCOM2Port = detectedCom2;
        if (!IsDongleCOM2Open()) {
            OpenDongleCOM2(detectedCom2);
        } else {
            SendCommand("CMD:VER?"); // manual refresh (e.g. Probe bound after first query)
            SendCommand("CMD:CFG?");
        }
    }

    if (!detectedCom1.empty()) {
        AppState::Instance().com1PortName = detectedCom1;
    }
}

void SerialManager::ServiceAutoReconnect() {
    static ULONGLONG lastProbe = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastProbe < 1000 || m_fwUpdating.load()) return;
    lastProbe = now;

    if (!IsDongleCOM2Open()) {
        AutoDetectDongle();
    }

    if (!m_com1Running.load() && m_com1ReconnectWanted.load()) {
        auto ports = EnumeratePorts();
        std::string candidate = m_lastCom1Port;
        for (const auto& p : ports) {
            if (p.portName == candidate && p.isDongleCOM1) break;
            if (p.isDongleCOM1) { candidate = p.portName; break; }
        }
        if (!candidate.empty()) OpenCOM1(candidate, m_lastCom1Baud);
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
    AppState::Instance().dongleRssiValid = false;
    AppState::Instance().dongleCfg.config_loaded = false;
    AppState::Instance().dongleCfgRevision.fetch_add(1);
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
    if (res && written == payload.length()) {
        AppState::Instance().AddControlLog("[TX -> DONGLE] " + cmd);
        return true;
    }
    return false;
}

static int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool ParseIntelHex(const std::string& path, std::vector<uint8_t>& image) {
    std::ifstream file(path);
    if (!file) return false;
    image.assign(32768, 0xFF);
    uint32_t base = 0;
    uint32_t highest = 0;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        if (line[0] != ':' || line.size() < 11 || ((line.size() - 1) & 1u)) return false;
        const size_t bytes = (line.size() - 1) / 2;
        std::vector<uint8_t> rec(bytes);
        uint8_t sum = 0;
        for (size_t i = 0; i < bytes; ++i) {
            int hi = HexNibble(line[1 + i * 2]);
            int lo = HexNibble(line[2 + i * 2]);
            if (hi < 0 || lo < 0) return false;
            rec[i] = static_cast<uint8_t>((hi << 4) | lo);
            sum = static_cast<uint8_t>(sum + rec[i]);
        }
        if (sum != 0 || rec[0] + 5u != bytes) return false;
        uint32_t address = (static_cast<uint32_t>(rec[1]) << 8) | rec[2];
        const uint8_t type = rec[3];
        if (type == 0x00) {
            uint32_t full = base + address;
            uint32_t count = rec[0];
            if (full + count > image.size()) return false;
            for (uint32_t i = 0; i < count; ++i) image[full + i] = rec[4 + i];
            if (full + count > highest) highest = full + count;
        } else if (type == 0x01) {
            break;
        } else if (type == 0x02 && rec[0] == 2) {
            base = (static_cast<uint32_t>(rec[4]) << 8 | rec[5]) << 4;
        } else if (type == 0x04 && rec[0] == 2) {
            base = (static_cast<uint32_t>(rec[4]) << 8 | rec[5]) << 16;
        }
    }
    if (highest == 0) return false;
    image.resize(highest);
    return true;
}

static uint32_t Crc32(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool WaitSerialText(HANDLE port, const std::string& needle, uint32_t timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    std::string received;
    char buffer[256];
    while (GetTickCount64() < deadline) {
        DWORD got = 0;
        if (ReadFile(port, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
            received.append(buffer, buffer + got);
            if (received.find(needle) != std::string::npos) return true;
            if (received.find("[ERR]") != std::string::npos) return false;
            if (received.size() > 4096) received.erase(0, received.size() - 2048);
        } else {
            Sleep(5);
        }
    }
    return false;
}

bool SerialManager::StartFirmwareUpdate(bool probe, const std::string& hexPath) {
    if (hexPath.empty() || m_fwUpdating.load() || !IsDongleCOM2Open()) return false;
    const std::string port = AppState::Instance().dongleCOM2Port;
    if (port.empty()) return false;

    if (m_fwThread.joinable()) m_fwThread.join();
    m_fwUpdating.store(true);
    // The updater needs exclusive access to COM2. Close both virtual ports before
    // the USB device is reset by the firmware transaction.
    CloseCOM1();
    CloseDongleCOM2();
    m_fwThread = std::thread(&SerialManager::FirmwareUpdateWorker, this, probe, hexPath, port);
    return true;
}

void SerialManager::FirmwareUpdateWorker(bool probe, std::string hexPath, std::string portName) {
    auto& state = AppState::Instance();
    state.AddControlLog(std::string("[FW] 开始") + (probe ? " Probe OTA" : " Dongle OTA") + ": " + hexPath);
    std::vector<uint8_t> image;
    if (!ParseIntelHex(hexPath, image)) {
        state.AddControlLog("[FW] Intel HEX 无效、为空或超过 32 KiB");
        m_fwUpdating.store(false);
        return;
    }
    HANDLE port = CreateFileA(("\\\\.\\" + portName).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (port == INVALID_HANDLE_VALUE) {
        state.AddControlLog("[FW] 无法重新独占打开 " + portName);
        m_fwUpdating.store(false);
        return;
    }
    DCB dcb{}; dcb.DCBlength = sizeof(dcb);
    GetCommState(port, &dcb);
    dcb.BaudRate = CBR_115200; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_ENABLE; dcb.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(port, &dcb);
    COMMTIMEOUTS timeouts{}; timeouts.ReadIntervalTimeout = MAXDWORD; timeouts.ReadTotalTimeoutConstant = 25; timeouts.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(port, &timeouts); PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR); Sleep(150);
    const char* prefix = probe ? "PROBE_FW" : "FW";
    char command[128];
    auto send = [&](const std::string& text, const std::string& ack, uint32_t timeout) {
        DWORD written = 0; std::string line = text + "\r\n";
        return WriteFile(port, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) && written == line.size() && WaitSerialText(port, ack, timeout);
    };
    char crcText[16]; std::snprintf(crcText, sizeof(crcText), "%08X", Crc32(image));
    std::snprintf(command, sizeof(command), "CMD:%s_START=%zu,%s", prefix, image.size(), crcText);
    bool ok = send(command, std::string("[OK] ") + prefix + "_START", 10000);
    for (size_t offset = 0; ok && offset < image.size(); offset += 64) {
        const size_t count = (image.size() - offset > 64) ? 64 : image.size() - offset;
        std::string hex; hex.reserve(count * 2);
        char b[4]; for (size_t i = 0; i < count; ++i) { std::snprintf(b, sizeof(b), "%02X", image[offset + i]); hex += b; }
        std::snprintf(command, sizeof(command), "CMD:%s_DATA=%zu,%s", prefix, offset, hex.c_str());
        ok = send(command, "[OK] " + std::to_string(offset + count), 5000);
        if (ok && ((offset & 0x3FFu) == 0 || offset + count == image.size())) state.AddControlLog("[FW] 已发送 " + std::to_string(offset + count) + "/" + std::to_string(image.size()) + " B");
    }
    if (ok) ok = send(std::string("CMD:") + prefix + "_FINISH", std::string("[OK] ") + prefix + "_VERIFIED", 10000);
    CloseHandle(port);
    state.AddControlLog(ok ? "[FW] CRC 校验通过，设备正在切换镜像" : "[FW] 更新失败，请查看设备响应");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    AutoDetectDongle();
    m_fwUpdating.store(false);
}

void SerialManager::COM2ThreadFunc() {
    char buf[256];
    std::string lineAcc = "";

    // Auto-query firmware version after connect. Two attempts cover the case where the
    // Probe has not finished binding to the Dongle at the moment of the first query.
    auto threadStart = std::chrono::steady_clock::now();
    bool sentVerQuery1 = false, sentVerQuery2 = false;
    bool sentCfgQuery1 = false, sentCfgQuery2 = false;

    while (m_com2Running.load()) {
        long long elapsedMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - threadStart).count();
        if (!sentVerQuery1 && elapsedMs >= 300) { SendCommand("CMD:VER?"); sentVerQuery1 = true; }
        if (!sentVerQuery2 && elapsedMs >= 2000) { SendCommand("CMD:VER?"); sentVerQuery2 = true; }
        if (!sentCfgQuery1 && elapsedMs >= 450) { SendCommand("CMD:CFG?"); sentCfgQuery1 = true; }
        if (!sentCfgQuery2 && elapsedMs >= 2200) { SendCommand("CMD:CFG?"); sentCfgQuery2 = true; }

        DWORD bytesRead = 0;
        BOOL readOk = ReadFile(m_hCom2, buf, sizeof(buf) - 1, &bytesRead, NULL);
        if (readOk && bytesRead > 0) {
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
        } else if (!readOk) {
            const DWORD error = GetLastError();
            if (m_com2Running.load() && error != ERROR_OPERATION_ABORTED) {
                HANDLE failedPort = static_cast<HANDLE>(m_hCom2);
                m_hCom2 = nullptr;
                m_com2Running = false;
                if (failedPort && failedPort != INVALID_HANDLE_VALUE) CloseHandle(failedPort);
                auto& state = AppState::Instance();
                state.dongleConnected = false;
                state.dongleRssiValid = false;
                state.dongleCfg.config_loaded = false;
                state.dongleCfgRevision.fetch_add(1);
                state.ClearFwVersions();
                state.AddControlLog("[DONGLE] USB 串口断开，等待自动重连");
            }
        }
    }
}

void SerialManager::ParseCOM2Line(const std::string& line) {
    auto& state = AppState::Instance();

    if (line.find("[LINK] Disconnected") != std::string::npos ||
        line.find("Link watchdog, disconnecting") != std::string::npos) {
        state.dongleRssiValid = false;
    }

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

    // Check for the tagged 2.4G RSSI report.
    int rssiVal = 0;
    if (sscanf_s(line.c_str(), "[RSSI] %d", &rssiVal) == 1) {
        state.dongleRssi = rssiVal;
        state.dongleRssiValid = true;
        state.dongleLastRssiTimestamp = state.GetElapsedSeconds();
        return;
    }

    if (line.rfind("[OK] RFTEST started", 0) == 0) {
        {
            std::lock_guard<std::mutex> lock(state.rfTestMutex);
            state.rfTest.running = true;
            state.rfTest.valid = false;
            state.rfTest.started_at = state.GetElapsedSeconds();
        }
        state.AddControlLog(line);
        return;
    }

    // Wireless test result: throughput is measured by the Dongle and latency
    // statistics are returned by the Probe in a compact fixed-width message.
    if (line.rfind("[RFTEST]", 0) == 0) {
        unsigned bytes = 0, elapsed = 0, rate = 0;
        unsigned avg = 0, min = 0, max = 0, packets = 0;
        const int fields = sscanf_s(line.c_str(),
                                    "[RFTEST] bytes=%u time=%ums rate=%uB/s A%u M%u X%u P%u",
                                    &bytes, &elapsed, &rate, &avg, &min, &max, &packets);
        if (fields == 7) {
            std::lock_guard<std::mutex> lock(state.rfTestMutex);
            state.rfTest.bytes = bytes;
            state.rfTest.elapsed_ms = elapsed;
            state.rfTest.rate_bps = rate;
            state.rfTest.avg_us = avg;
            state.rfTest.min_us = min;
            state.rfTest.max_us = max;
            state.rfTest.packets = packets;
            state.rfTest.valid = true;
            state.rfTest.running = false;
            state.rfTest.started_at = 0.0;
        }
        state.AddControlLog(line);
        return;
    }

    // Check if line is INA telemetry: "[INA] V:5.012V, I:123.125mA, P:617.16mW"
    float v = 0.0f, i = 0.0f, p = 0.0f;
    int parsed = sscanf_s(line.c_str(), "[INA] V:%fV, I:%fmA, P:%fmW", &v, &i, &p);
    if (parsed == 3) {
        double now = state.GetElapsedSeconds();
        state.dongleLastTimestamp = now;
        state.donglePacketCount++;
        /* Telemetry is the authoritative link heartbeat.  RSSI packets are
         * intentionally sparse, so keep the connected state alive from the
         * regular telemetry stream instead of expiring it between RSSI lines. */
        state.dongleRssiValid = true;
        state.dongleLastRssiTimestamp = now;

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
        // Control response or log message - only log recognized command responses/system tags
        if (line.rfind("[INA]", 0) == 0 ||
            line.rfind("[RSSI]", 0) == 0 ||
            line.rfind("[VER]", 0) == 0 ||
            line.rfind("[CFG]", 0) == 0 ||
            line.rfind("[LINK]", 0) == 0 ||
            line.rfind("[DONGLE]", 0) == 0 ||
            line.rfind("[RFTEST]", 0) == 0 ||
            line.rfind("[OK]", 0) == 0 ||
            line.rfind("[ERR]", 0) == 0 ||
            line.rfind("CMD:", 0) == 0) {
            state.AddControlLog(line);
        }

        // Check for the current configuration response order.
        if (line.find("[CFG]") != std::string::npos) {
            uint32_t rate = 0, fsc = 0, avg = 0, shunt = 0, linkLed = 0, bleHz = 0;
            /* Firmware includes engineering units in the response, e.g.
             * "RATE=100ms, FSC=3200mA, SHUNT=20000uOhm".  The old format
             * expected a comma immediately after each number, so it parsed
             * only RATE and never updated the UI selections. */
            int parsed = sscanf_s(line.c_str(),
                "[CFG] RATE=%ums, FSC=%umA, AVG=%u, SHUNT=%uuOhm, SWAP=%*u, LINKLED=%u, BLE=%uHz",
                &rate, &fsc, &avg, &shunt, &linkLed, &bleHz);
            if (parsed != 6) {
                /* Accept responses from older firmware builds without units. */
                parsed = sscanf_s(line.c_str(),
                    "[CFG] RATE=%u, FSC=%u, AVG=%u, SHUNT=%u, SWAP=%*u, LINKLED=%u, BLE=%uHz",
                    &rate, &fsc, &avg, &shunt, &linkLed, &bleHz);
            }
            if (parsed == 6) {
                std::lock_guard<std::mutex> lock(state.dongleCfgMutex);
                if (rate > 0) state.dongleCfg.sampling_rate_ms = rate;
                if (fsc > 0) state.dongleCfg.full_scale_ma = fsc;
                if (avg > 0) state.dongleCfg.averaging_count = avg;
                if (shunt > 0) {
                    /* The wire value is micro-ohms; the UI model and combo
                     * values are milli-ohms. */
                    state.dongleCfg.shunt_mohm = (shunt > 1000u) ? ((shunt + 500u) / 1000u) : shunt;
                }
                if (linkLed >= 26 && linkLed <= 255 && line.find("LINKLED=") != std::string::npos) state.dongleCfg.link_led_max_duty = linkLed;
                if (bleHz >= 1 && bleHz <= 20) state.dongleCfg.ble_adv_hz = bleHz;
                state.dongleCfg.config_loaded = true;
                state.dongleCfgRevision.fetch_add(1);
            }
        }

        if (line.rfind("[OK]", 0) == 0 &&
            (line.find("Rate Set") != std::string::npos ||
             line.find("FSC Set") != std::string::npos ||
             line.find("Shunt Set") != std::string::npos ||
             line.find("Avg Set") != std::string::npos ||
             line.find("Link LED Set") != std::string::npos ||
             line.find("BLE Rate Set") != std::string::npos ||
             line.find("Saved to Flash") != std::string::npos)) {
            SendCommand("CMD:CFG?");
        }
    }
}

// -------------------------------------------------------------
// COM1 Target MCU UART Passthrough
// -------------------------------------------------------------
bool SerialManager::OpenCOM1(const std::string& portName, uint32_t baudRate) {
    CloseCOM1Internal();

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
    m_lastCom1Port = portName;
    m_lastCom1Baud = baudRate;
    m_com1ReconnectWanted = true;
    return true;
}

void SerialManager::CloseCOM1Internal() {
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

void SerialManager::CloseCOM1() {
    m_com1ReconnectWanted = false;
    CloseCOM1Internal();
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
        BOOL readOk = ReadFile(m_hCom1, buf, sizeof(buf), &bytesRead, NULL);
        if (readOk && bytesRead > 0) {
            AppState::Instance().PushCom1Rx(buf, bytesRead);
        } else if (!readOk && m_com1Running.load() && GetLastError() != ERROR_OPERATION_ABORTED) {
            HANDLE failedPort = static_cast<HANDLE>(m_hCom1);
            m_hCom1 = nullptr;
            m_com1Running = false;
            if (failedPort && failedPort != INVALID_HANDLE_VALUE) CloseHandle(failedPort);
            AppState::Instance().com1Open = false;
            AppState::Instance().AddControlLog("[COM1] 串口断开，等待自动重连");
        }
    }
}

} // namespace CH570App
