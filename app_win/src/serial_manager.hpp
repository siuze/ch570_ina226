#pragma once

#include "app_state.hpp"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

namespace CH570App {

class SerialManager {
public:
    static SerialManager& Instance() {
        static SerialManager s_instance;
        return s_instance;
    }

    // Port enumeration
    std::vector<SerialPortInfo> EnumeratePorts();
    void AutoDetectDongle();

    // COM2 Telemetry & Control
    bool OpenDongleCOM2(const std::string& portName);
    void CloseDongleCOM2();
    bool IsDongleCOM2Open() const { return m_com2Running.load(); }
    bool SendCommand(const std::string& cmd);

    // COM1 Target MCU Passthrough
    bool OpenCOM1(const std::string& portName, uint32_t baudRate);
    void CloseCOM1();
    bool IsCOM1Open() const { return m_com1Running.load(); }
    bool SendCOM1(const uint8_t* data, size_t len);
    bool SendCOM1String(const std::string& text);

    ~SerialManager();

private:
    SerialManager();

    // COM2 reader worker
    void COM2ThreadFunc();
    std::thread m_com2Thread;
    std::atomic<bool> m_com2Running{false};
    void* m_hCom2{nullptr}; // HANDLE

    // COM1 reader worker
    void COM1ThreadFunc();
    std::thread m_com1Thread;
    std::atomic<bool> m_com1Running{false};
    void* m_hCom1{nullptr}; // HANDLE

    void ParseCOM2Line(const std::string& line);
};

} // namespace CH570App
