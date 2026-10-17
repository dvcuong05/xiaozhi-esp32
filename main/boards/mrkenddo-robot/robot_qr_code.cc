#include "robot_qr_code.h"

// The Nayuki encoder is already distributed with this pinned LVGL component.
// Enable LV_USE_QRCODE in the board variant; no vendor sources are copied/edited.
#include <src/libs/qrcode/qrcodegen.h>

#include <cstring>

namespace mrkenddo {

bool RobotQrCode::ValidText(const std::string& text, size_t max_bytes) {
    if (text.size() > max_bytes || text.find('\0') != std::string::npos) {
        return false;
    }
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<uint8_t>(text[i++]);
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f)
                return false;
            continue;
        }
        int remaining = c >= 0xc2 && c <= 0xdf   ? 1
                        : c >= 0xe0 && c <= 0xef ? 2
                        : c >= 0xf0 && c <= 0xf4 ? 3
                                                 : -1;
        if (remaining < 0 || i + remaining > text.size())
            return false;
        const auto first = static_cast<uint8_t>(text[i]);
        if ((c == 0xe0 && first < 0xa0) || (c == 0xed && first >= 0xa0) ||
            (c == 0xf0 && first < 0x90) || (c == 0xf4 && first >= 0x90))
            return false;
        while (remaining-- > 0) {
            if ((static_cast<uint8_t>(text[i++]) & 0xc0) != 0x80)
                return false;
        }
    }
    return true;
}

bool RobotQrCode::ValidateConfig(const QrConfig& config) {
    if (!config.valid || config.payload[sizeof(config.payload) - 1] != '\0' ||
        config.caption[sizeof(config.caption) - 1] != '\0')
        return false;
    return ValidText(config.payload, sizeof(config.payload) - 1) &&
           ValidText(config.caption, sizeof(config.caption) - 1);
}

bool RobotQrCode::Generate(const std::string& payload, RobotQrCode& output) {
    if (payload.empty() || !ValidText(payload, 256))
        return false;
    constexpr int kVersion = 12;
    uint8_t work[qrcodegen_BUFFER_LEN_FOR_VERSION(kVersion)]{};
    uint8_t encoded[qrcodegen_BUFFER_LEN_FOR_VERSION(kVersion)]{};
    if (!qrcodegen_encodeText(payload.c_str(), work, encoded, qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN, kVersion, qrcodegen_Mask_AUTO, false)) {
        return false;
    }
    output.bits.fill(0);
    output.modules = qrcodegen_getSize(encoded);
    for (int row = 0; row < output.modules; ++row) {
        for (int column = 0; column < output.modules; ++column) {
            if (qrcodegen_getModule(encoded, column, row)) {
                const int bit = row * kMaxModules + column;
                output.bits[bit / 8] |= 1u << (bit % 8);
            }
        }
    }
    return true;
}

bool RobotQrCode::IsBlack(int row, int column) const {
    if (row < 0 || column < 0 || row >= modules || column >= modules)
        return false;
    const int bit = row * kMaxModules + column;
    return (bits[bit / 8] & (1u << (bit % 8))) != 0;
}

}  // namespace mrkenddo
