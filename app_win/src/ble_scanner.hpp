#pragma once

#include <memory>
#include <string>

namespace CH570App {

class BleScannerImpl;

class BleScanner {
public:
    BleScanner();
    ~BleScanner();

    bool Start();
    void Stop();
    bool IsScanning() const;

private:
    std::unique_ptr<BleScannerImpl> m_impl;
};

} // namespace CH570App
