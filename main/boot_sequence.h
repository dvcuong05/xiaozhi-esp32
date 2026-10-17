#ifndef BOOT_SEQUENCE_H_
#define BOOT_SEQUENCE_H_

#include <cstddef>
#include <cstdint>

// Boards opt into boot audio and own any persistent settings or migration.
struct BootAudioConfig {
    bool enabled = false;
    int volume = 45;
};

enum class BootStage : uint8_t {
    SystemInitializing,
    WifiScanning,
    WifiConnecting,
    WifiConnected,
    IpAcquired,
    CheckingVersion,
    LoggingIn,
    Ready,
    Error,
};

constexpr size_t kBootEventDetailSize = 96;

struct BootEvent {
    BootStage stage = BootStage::SystemInitializing;
    uint32_t sequence = 0;
    char detail[kBootEventDetailSize] = {};
};

inline const char* BootStageName(BootStage stage) {
    switch (stage) {
        case BootStage::SystemInitializing:
            return "SYSTEM INIT";
        case BootStage::WifiScanning:
            return "WIFI SCAN";
        case BootStage::WifiConnecting:
            return "WIFI CONNECT";
        case BootStage::WifiConnected:
            return "WIFI LINK";
        case BootStage::IpAcquired:
            return "IP READY";
        case BootStage::CheckingVersion:
            return "CHECK VERSION";
        case BootStage::LoggingIn:
            return "LOGIN";
        case BootStage::Ready:
            return "READY";
        case BootStage::Error:
            return "ERROR";
    }
    return "BOOT";
}

#endif  // BOOT_SEQUENCE_H_
