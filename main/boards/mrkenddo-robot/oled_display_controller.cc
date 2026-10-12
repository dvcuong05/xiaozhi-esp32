#include "oled_display_controller.h"

#include "oled_clock_font.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace mrkenddo {
namespace {

constexpr int kGlyphSpacing = 1;

uint32_t DecodeUtf8(const char* input, size_t* consumed) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(input);
    if (bytes[0] < 0x80) {
        *consumed = 1;
        return bytes[0];
    }
    if ((bytes[0] & 0xe0) == 0xc0 && (bytes[1] & 0xc0) == 0x80) {
        *consumed = 2;
        return ((bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);
    }
    if ((bytes[0] & 0xf0) == 0xe0 && (bytes[1] & 0xc0) == 0x80 && (bytes[2] & 0xc0) == 0x80) {
        *consumed = 3;
        return ((bytes[0] & 0x0f) << 12) | ((bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
    }
    if ((bytes[0] & 0xf8) == 0xf0 && (bytes[1] & 0xc0) == 0x80 && (bytes[2] & 0xc0) == 0x80 &&
        (bytes[3] & 0xc0) == 0x80) {
        *consumed = 4;
        return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3f) << 12) | ((bytes[2] & 0x3f) << 6) |
               (bytes[3] & 0x3f);
    }
    *consumed = 1;
    return '?';
}

char NormalizeCodepoint(uint32_t codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') {
        return static_cast<char>(codepoint - ('a' - 'A'));
    }
    if (codepoint >= 'A' && codepoint <= 'Z') {
        return static_cast<char>(codepoint);
    }
    if (codepoint >= '0' && codepoint <= '9') {
        return static_cast<char>(codepoint);
    }
    if (codepoint == 0x0110 || codepoint == 0x0111) {
        return 'D';
    }
    if (codepoint == 0x0102 || codepoint == 0x0103 ||
        (codepoint >= 0x1ea0 && codepoint <= 0x1eb7)) {
        return 'A';
    }
    if (codepoint >= 0x00c0 && codepoint <= 0x00c5) {
        return 'A';
    }
    if (codepoint >= 0x00e0 && codepoint <= 0x00e5) {
        return 'A';
    }
    if (codepoint == 0x0118 || codepoint == 0x0119 ||
        (codepoint >= 0x1eb8 && codepoint <= 0x1ec7)) {
        return 'E';
    }
    if (codepoint >= 0x00c8 && codepoint <= 0x00cb) {
        return 'E';
    }
    if (codepoint >= 0x00e8 && codepoint <= 0x00eb) {
        return 'E';
    }
    if (codepoint == 0x0128 || codepoint == 0x0129 ||
        (codepoint >= 0x1ec8 && codepoint <= 0x1ecb)) {
        return 'I';
    }
    if (codepoint == 0x00cc || codepoint == 0x00cd || codepoint == 0x00ce || codepoint == 0x00cf ||
        codepoint == 0x00ec || codepoint == 0x00ed || codepoint == 0x00ee || codepoint == 0x00ef) {
        return 'I';
    }
    if (codepoint == 0x01a0 || codepoint == 0x01a1 ||
        (codepoint >= 0x1ecc && codepoint <= 0x1ee7)) {
        return 'O';
    }
    if (codepoint >= 0x00d2 && codepoint <= 0x00d6) {
        return 'O';
    }
    if (codepoint >= 0x00f2 && codepoint <= 0x00f6) {
        return 'O';
    }
    if (codepoint == 0x01af || codepoint == 0x01b0 ||
        (codepoint >= 0x1ee8 && codepoint <= 0x1ef9)) {
        return 'U';
    }
    if (codepoint == 0x0168 || codepoint == 0x0169 || codepoint == 0x00d9 || codepoint == 0x00da ||
        codepoint == 0x00db || codepoint == 0x00f9 || codepoint == 0x00fa || codepoint == 0x00fb) {
        return 'U';
    }
    if (codepoint == 0x00dd || codepoint == 0x00fd || codepoint == 0x00ff) {
        return 'Y';
    }
    if (codepoint == ' ' || codepoint == '\t' || codepoint == '\r' || codepoint == '\n') {
        return ' ';
    }
    if (codepoint >= 0x20 && codepoint <= 0x7e) {
        return static_cast<char>(codepoint);
    }
    return '?';
}

}  // namespace

void OledDisplayController::Configure(uint8_t* framebuffer, size_t framebuffer_size, int width,
                                      int height, int rotation) {
    framebuffer_ = framebuffer;
    framebuffer_size_ = framebuffer_size;
    width_ = width;
    height_ = height;
    rotation_ = rotation == 180 ? 180 : 0;
    marquee_offset_ = 0;
    last_mode_ = OledDisplayMode::Clock;
    last_text_[0] = '\0';
}

void OledDisplayController::Disable() {
    framebuffer_ = nullptr;
    framebuffer_size_ = 0;
    width_ = 0;
    height_ = 0;
    marquee_offset_ = 0;
    last_text_[0] = '\0';
}

bool OledDisplayController::IsValidMode(int32_t mode) {
    return mode >= static_cast<int32_t>(OledDisplayMode::Clock) &&
           mode <= static_cast<int32_t>(OledDisplayMode::Off);
}

bool OledDisplayController::IsValidSpeed(int32_t speed) { return speed >= 1 && speed <= 5; }

bool OledDisplayController::IsValidFontSize(int32_t font_size) {
    return font_size >= 0 && font_size <= 2;
}

void OledDisplayController::NormalizeText(const char* input, char* output, size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == nullptr) {
        return;
    }
    size_t output_length = 0;
    while (*input != '\0' && output_length + 1 < output_size) {
        size_t consumed = 1;
        uint32_t codepoint = DecodeUtf8(input, &consumed);
        char character = NormalizeCodepoint(codepoint);
        if (character == '\n') {
            character = ' ';
        }
        output[output_length++] = character;
        input += consumed;
    }
    output[output_length] = '\0';
}

void OledDisplayController::Clear() {
    if (framebuffer_ != nullptr) {
        std::memset(framebuffer_, 0, framebuffer_size_);
    }
}

void OledDisplayController::SetPixel(int x, int y) {
    if (framebuffer_ == nullptr || x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }
    if (rotation_ == 180) {
        x = width_ - 1 - x;
        y = height_ - 1 - y;
    }
    size_t offset = static_cast<size_t>(x) + static_cast<size_t>(y / 8) * width_;
    if (offset < framebuffer_size_) {
        framebuffer_[offset] |= static_cast<uint8_t>(1U << (y % 8));
    }
}

int OledDisplayController::TextWidth(const char* text, int scale) const {
    if (text == nullptr || *text == '\0') {
        return 0;
    }
    size_t length = std::strlen(text);
    return static_cast<int>(length) * mrkenddo::oled_clock_font::kGlyphWidth * scale +
           static_cast<int>(length - 1) * kGlyphSpacing;
}

int OledDisplayController::ChooseScale(const char* text, int max_scale) const {
    for (int scale = max_scale; scale >= 1; --scale) {
        if (TextWidth(text, scale) <= width_ &&
            mrkenddo::oled_clock_font::kGlyphHeight * scale <= height_) {
            return scale;
        }
    }
    return 1;
}

void OledDisplayController::DrawText(const char* text, int x, int y, int scale) {
    if (text == nullptr || scale < 1) {
        return;
    }
    for (size_t index = 0; text[index] != '\0'; ++index) {
        const uint8_t* glyph = mrkenddo::oled_clock_font::GetGlyph(text[index]);
        int glyph_x = x + static_cast<int>(index) *
                              (mrkenddo::oled_clock_font::kGlyphWidth * scale + kGlyphSpacing);
        for (int row = 0; row < mrkenddo::oled_clock_font::kGlyphHeight; ++row) {
            for (int column = 0; column < mrkenddo::oled_clock_font::kGlyphWidth; ++column) {
                if ((glyph[row] & (1U << (mrkenddo::oled_clock_font::kGlyphWidth - 1 - column))) ==
                    0) {
                    continue;
                }
                for (int y_scale = 0; y_scale < scale; ++y_scale) {
                    for (int x_scale = 0; x_scale < scale; ++x_scale) {
                        SetPixel(glyph_x + column * scale + x_scale, y + row * scale + y_scale);
                    }
                }
            }
        }
    }
}

void OledDisplayController::DrawCenteredText(const char* text, int y, int scale) {
    DrawText(text, (width_ - TextWidth(text, scale)) / 2, y, scale);
}

void OledDisplayController::DrawClock(time_t now) {
    char clock_text[] = "--:--:--";
    tm local_time = {};
    if (now >= 1577836800 && localtime_r(&now, &local_time) != nullptr) {
        std::snprintf(clock_text, sizeof(clock_text), "%02d:%02d:%02d", local_time.tm_hour,
                      local_time.tm_min, local_time.tm_sec);
    }
    int scale = ChooseScale(clock_text, 8);
    int text_height = mrkenddo::oled_clock_font::kGlyphHeight * scale;
    DrawCenteredText(clock_text, (height_ - text_height) / 2, scale);
}

void OledDisplayController::DrawStatus(const char* line_1, const char* line_2) {
    char normalized_1[80] = {};
    char normalized_2[80] = {};
    NormalizeText(line_1, normalized_1, sizeof(normalized_1));
    NormalizeText(line_2, normalized_2, sizeof(normalized_2));
    int scale = height_ >= 48 ? 2 : 1;
    if (TextWidth(normalized_1, scale) > width_ || TextWidth(normalized_2, scale) > width_) {
        scale = 1;
    }
    int line_height = mrkenddo::oled_clock_font::kGlyphHeight * scale;
    int gap = height_ >= line_height * 2 + 2 ? 2 : 0;
    int first_y = (height_ - line_height * 2 - gap) / 2;
    DrawCenteredText(normalized_1, first_y, scale);
    DrawCenteredText(normalized_2, first_y + line_height + gap, scale);
}

void OledDisplayController::DrawMarquee(const OledDisplaySettings& settings) {
    char normalized[sizeof(settings.text)] = {};
    NormalizeText(settings.text, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        DrawClock(time(nullptr));
        return;
    }
    int scale = settings.font_size == 2 ? 2 : 1;
    if (settings.font_size == 0) {
        scale = ChooseScale(normalized, 2);
    }
    if (TextWidth(normalized, scale) <= width_) {
        DrawCenteredText(normalized, (height_ - oled_clock_font::kGlyphHeight * scale) / 2, scale);
        return;
    }
    int text_width = TextWidth(normalized, scale);
    int gap = width_ / 3;
    if (last_mode_ != OledDisplayMode::Marquee || std::strcmp(last_text_, normalized) != 0) {
        marquee_offset_ = 0;
    }
    int x = width_ - marquee_offset_;
    DrawText(normalized, x, (height_ - oled_clock_font::kGlyphHeight * scale) / 2, scale);
    marquee_offset_ += std::clamp<int>(settings.speed, 1, 5);
    if (marquee_offset_ > text_width + gap) {
        marquee_offset_ = 0;
    }
}

bool OledDisplayController::Render(const OledDisplaySettings& settings, const char* status_line_1,
                                   const char* status_line_2, time_t now) {
    if (framebuffer_ == nullptr || framebuffer_size_ == 0 || width_ <= 0 || height_ <= 0) {
        return false;
    }
    OledDisplaySettings normalized_settings = settings;
    NormalizeText(settings.text, normalized_settings.text, sizeof(normalized_settings.text));
    if (last_mode_ != normalized_settings.mode ||
        std::strcmp(last_text_, normalized_settings.text) != 0) {
        marquee_offset_ = 0;
    }
    last_mode_ = normalized_settings.mode;
    std::snprintf(last_text_, sizeof(last_text_), "%s", normalized_settings.text);

    Clear();
    switch (normalized_settings.mode) {
        case OledDisplayMode::Clock:
            DrawClock(now);
            break;
        case OledDisplayMode::Marquee:
            DrawMarquee(normalized_settings);
            break;
        case OledDisplayMode::Status:
            DrawStatus(status_line_1, status_line_2);
            break;
        case OledDisplayMode::Off:
            break;
    }
    return true;
}

}  // namespace mrkenddo
