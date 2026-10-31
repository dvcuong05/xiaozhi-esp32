#ifndef KYNANGSO_ROBOT_QR_CODE_H_
#define KYNANGSO_ROBOT_QR_CODE_H_

#include <array>
#include <cstdint>
#include <string>

namespace kynangso {

struct QrConfig {
    char payload[257] = {};
    char caption[97] = {};
    bool valid = true;  // Runtime read health; not serialized into NVS.
};

// Packed module grid, not a 240x240 RGB canvas. Payload is never logged.
struct RobotQrCode {
    static constexpr int kMaxModules = 65;  // QR version 12, medium ECC.
    std::array<uint8_t, (kMaxModules * kMaxModules + 7) / 8> bits{};
    int modules = 0;
    bool IsBlack(int row, int column) const;
    static bool Generate(const std::string& payload, RobotQrCode& output);
    static bool ValidText(const std::string& text, size_t max_bytes);
    static bool ValidateConfig(const QrConfig& config);
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_QR_CODE_H_
