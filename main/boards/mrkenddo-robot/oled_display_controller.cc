#include "oled_display_controller.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mrkenddo {
namespace {

constexpr int kGlyphSpacing = 1;
constexpr const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

uint32_t DecodeUtf8(const char* input, size_t remaining, size_t* consumed) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(input);
    if (remaining == 0) {
        *consumed = 0;
        return '?';
    }
    if (bytes[0] < 0x80) {
        *consumed = 1;
        return bytes[0];
    }
    if (remaining >= 2 && (bytes[0] & 0xe0) == 0xc0 && (bytes[1] & 0xc0) == 0x80) {
        *consumed = 2;
        return ((bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);
    }
    if (remaining >= 3 && (bytes[0] & 0xf0) == 0xe0 && (bytes[1] & 0xc0) == 0x80 &&
        (bytes[2] & 0xc0) == 0x80) {
        *consumed = 3;
        return ((bytes[0] & 0x0f) << 12) | ((bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
    }
    if (remaining >= 4 && (bytes[0] & 0xf8) == 0xf0 && (bytes[1] & 0xc0) == 0x80 &&
        (bytes[2] & 0xc0) == 0x80 && (bytes[3] & 0xc0) == 0x80) {
        *consumed = 4;
        return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3f) << 12) | ((bytes[2] & 0x3f) << 6) |
               (bytes[3] & 0x3f);
    }
    *consumed = 1;
    return '?';
}

bool IsWhitespace(uint32_t codepoint) {
    return codepoint == ' ' || codepoint == '\t' || codepoint == '\r' || codepoint == '\n';
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
        const size_t remaining = std::strlen(input);
        size_t consumed = 1;
        const uint32_t codepoint = DecodeUtf8(input, remaining, &consumed);
        if (consumed == 0) {
            break;
        }
        if (IsWhitespace(codepoint)) {
            output[output_length++] = ' ';
        } else if (codepoint == '?' && static_cast<uint8_t>(*input) >= 0x80) {
            output[output_length++] = '?';
        } else if (output_length + consumed < output_size) {
            std::memcpy(output + output_length, input, consumed);
            output_length += consumed;
        } else {
            break;
        }
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
    const size_t offset = static_cast<size_t>(x) + static_cast<size_t>(y / 8) * width_;
    if (offset < framebuffer_size_) {
        framebuffer_[offset] |= static_cast<uint8_t>(1U << (y % 8));
    }
}

void OledDisplayController::DrawWifiBars(int x, int y, int rssi) {
    int bars = 0;
    if (rssi != 0) {
        bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    }

    constexpr int kBarWidth = 3;
    constexpr int kBarGap = 2;
    constexpr int kBaseline = 13;
    constexpr int kBarHeights[] = {3, 6, 9, 12};
    for (int bar = 0; bar < 4; ++bar) {
        if (bar >= bars) {
            continue;
        }
        for (int row = kBaseline - kBarHeights[bar]; row < kBaseline; ++row) {
            for (int column = 0; column < kBarWidth; ++column) {
                SetPixel(x + bar * (kBarWidth + kBarGap) + column, y + row);
            }
        }
    }
}

void OledDisplayController::DrawMark(oled_clock_font::Mark mark, int x, int y, int scale) {
    if (mark == oled_clock_font::Mark::None) {
        return;
    }

    auto draw = [this, x, y, scale](int column, int row) {
        for (int y_scale = 0; y_scale < scale; ++y_scale) {
            for (int x_scale = 0; x_scale < scale; ++x_scale) {
                SetPixel(x + column * scale + x_scale, y + row * scale + y_scale);
            }
        }
    };

    switch (mark) {
        case oled_clock_font::Mark::Acute:
            draw(3, 0);
            draw(2, 1);
            break;
        case oled_clock_font::Mark::Grave:
            draw(1, 0);
            draw(2, 1);
            break;
        case oled_clock_font::Mark::Hook:
            draw(2, 0);
            draw(3, 1);
            break;
        case oled_clock_font::Mark::Tilde:
            draw(1, 0);
            draw(2, 0);
            draw(3, 1);
            draw(4, 1);
            break;
        case oled_clock_font::Mark::Dot:
            draw(2, 0);
            break;
        case oled_clock_font::Mark::Breve:
            draw(1, 1);
            draw(2, 0);
            draw(3, 1);
            break;
        case oled_clock_font::Mark::Circumflex:
            draw(1, 1);
            draw(2, 0);
            draw(3, 1);
            break;
        case oled_clock_font::Mark::Horn:
            draw(1, 0);
            draw(2, 1);
            draw(3, 1);
            break;
        case oled_clock_font::Mark::Stroke:
            draw(1, 3);
            draw(2, 3);
            draw(3, 3);
            break;
        case oled_clock_font::Mark::None:
            break;
    }
}

void OledDisplayController::DrawGlyph(const oled_clock_font::Glyph& glyph, int x, int y,
                                      int scale) {
    if (glyph.bitmap == nullptr || glyph.width == 0 || glyph.height == 0 || scale < 1) {
        return;
    }
    const int bitmap_height =
        glyph.width > oled_clock_font::kGlyphWidth ? glyph.height : oled_clock_font::kGlyphHeight;
    for (int row = 0; row < bitmap_height; ++row) {
        for (int column = 0; column < glyph.width; ++column) {
            if ((glyph.bitmap[row] & (1U << (glyph.width - 1 - column))) == 0) {
                continue;
            }
            for (int y_scale = 0; y_scale < scale; ++y_scale) {
                for (int x_scale = 0; x_scale < scale; ++x_scale) {
                    SetPixel(x + column * scale + x_scale,
                             y + (glyph.base_y + row) * scale + y_scale);
                }
            }
        }
    }

    if (glyph.shape != oled_clock_font::Mark::None) {
        DrawMark(glyph.shape, x, y, scale);
    }
    if (glyph.tone != oled_clock_font::Mark::None) {
        int tone_y = y;
        if (glyph.shape != oled_clock_font::Mark::None) {
            tone_y = y + (glyph.tone == oled_clock_font::Mark::Dot ? 10 : 2) * scale;
        } else if (glyph.tone == oled_clock_font::Mark::Dot) {
            tone_y = y + 8 * scale;
        }
        DrawMark(glyph.tone, x, tone_y, scale);
    }
}

void OledDisplayController::DrawText(const char* text, int x, int y, int scale) {
    DrawTextWithSpacing(text, x, y, scale, kGlyphSpacing);
}

void OledDisplayController::DrawTextWithSpacing(const char* text, int x, int y, int scale,
                                                int spacing) {
    if (text == nullptr || scale < 1) {
        return;
    }
    int cursor_x = x;
    const char* cursor = text;
    while (*cursor != '\0') {
        const size_t remaining = std::strlen(cursor);
        size_t consumed = 1;
        const uint32_t codepoint = DecodeUtf8(cursor, remaining, &consumed);
        const oled_clock_font::Glyph glyph = oled_clock_font::GetGlyph(codepoint);
        if (glyph.width > 0) {
            DrawGlyph(glyph, cursor_x, y, scale);
            cursor_x += glyph.width * scale + spacing * scale;
        }
        cursor += consumed;
    }
}

int OledDisplayController::TextWidth(const char* text, int scale) const {
    return TextWidthWithSpacing(text, scale, kGlyphSpacing);
}

int OledDisplayController::TextWidthWithSpacing(const char* text, int scale, int spacing) const {
    if (text == nullptr || *text == '\0' || scale < 1) {
        return 0;
    }
    int width = 0;
    int visible_glyphs = 0;
    const char* cursor = text;
    while (*cursor != '\0') {
        const size_t remaining = std::strlen(cursor);
        size_t consumed = 1;
        const uint32_t codepoint = DecodeUtf8(cursor, remaining, &consumed);
        const oled_clock_font::Glyph glyph = oled_clock_font::GetGlyph(codepoint);
        if (glyph.width > 0) {
            width += glyph.width * scale;
            ++visible_glyphs;
        }
        cursor += consumed;
    }
    if (visible_glyphs > 1) {
        width += (visible_glyphs - 1) * spacing * scale;
    }
    return width;
}

int OledDisplayController::TextHeight(const char* text, int scale) const {
    if (text == nullptr || *text == '\0' || scale < 1) {
        return 0;
    }
    int height = 0;
    const char* cursor = text;
    while (*cursor != '\0') {
        const size_t remaining = std::strlen(cursor);
        size_t consumed = 1;
        const uint32_t codepoint = DecodeUtf8(cursor, remaining, &consumed);
        const oled_clock_font::Glyph glyph = oled_clock_font::GetGlyph(codepoint);
        height = std::max(height, static_cast<int>(glyph.height) * scale);
        cursor += consumed;
    }
    return height;
}

int OledDisplayController::ChooseScale(const char* text, int max_scale) const {
    for (int scale = max_scale; scale >= 1; --scale) {
        if (TextWidth(text, scale) <= width_ && TextHeight(text, scale) <= height_) {
            return scale;
        }
    }
    return 1;
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
    const int scale = ChooseScale(clock_text, 8);
    const int text_height = TextHeight(clock_text, scale);
    DrawCenteredText(clock_text, (height_ - text_height) / 2, scale);
}

void OledDisplayController::DrawBoot(const char* title, const char* network_line) {
    char title_text[64] = {};
    char network_text[96] = {};
    NormalizeText(title, title_text, sizeof(title_text));
    NormalizeText(network_line, network_text, sizeof(network_text));

    // The 128x32 OLED has enough room for two compact, centered lines. Keep
    // this independent of the user-selected OLED mode so the network address
    // remains visible throughout the boot sequence.
    const int title_scale = ChooseScale(title_text, 1);
    const int network_scale = ChooseScale(network_text, 1);
    const int line_height = oled_clock_font::kGlyphHeight * std::max(title_scale, network_scale);
    const int line_gap = 4;
    const int first_y = std::max(0, (height_ - line_height * 2 - line_gap) / 2);
    DrawCenteredText(title_text, first_y, title_scale);
    DrawCenteredText(network_text, first_y + line_height + line_gap, network_scale);
}

void OledDisplayController::DrawStatus(const char* line_1, const char* line_2, time_t now,
                                       int wifi_rssi) {
    (void)line_1;
    char status_text[16] = {};
    NormalizeText(line_2, status_text, sizeof(status_text));
    char clock_text[32] = "--:--:--";
    char date_text[32] = "---???";
    tm local_time = {};
    if (now >= 1577836800 && localtime_r(&now, &local_time) != nullptr) {
        std::snprintf(clock_text, sizeof(clock_text), "%02d:%02d:%02d", local_time.tm_hour,
                      local_time.tm_min, local_time.tm_sec);
        const int month = std::clamp(local_time.tm_mon, 0, 11);
        std::snprintf(date_text, sizeof(date_text), "%02d-%s", local_time.tm_mday,
                      kMonthNames[month]);
    }

    // Give the two-line clock/date block enough room for 2x glyphs while
    // keeping the full SPEAKING/LISTENING labels in the compact right column.
    constexpr int kRightStatusWidth = 45;
    const int divider_x = std::min(width_ - 1, std::max(0, width_ - kRightStatusWidth - 1));
    for (int y = 2; y < height_ - 2; y += 2) {
        SetPixel(divider_x, y);
    }

    const int left_width = std::max(1, divider_x - 2);
    constexpr int kClockScale = 2;
    constexpr int kDateScale = 1;
    constexpr int kCompactSpacing = 0;
    const int clock_x = std::max(
        0, (left_width - TextWidthWithSpacing(clock_text, kClockScale, kCompactSpacing)) / 2);
    const int date_x = std::max(
        0, (left_width - TextWidthWithSpacing(date_text, kDateScale, kCompactSpacing)) / 2);
    const int clock_height = oled_clock_font::kGlyphHeight * kClockScale;
    const int date_height = oled_clock_font::kGlyphHeight * kDateScale;
    // Leave one complete glyph row between the large clock and the smaller date.
    const int line_gap = oled_clock_font::kGlyphHeight;
    const int block_height = clock_height + line_gap + date_height;
    const int first_y = std::max(0, (height_ - block_height) / 2);
    const int date_y = first_y + clock_height + line_gap;
    DrawTextWithSpacing(clock_text, clock_x, first_y, kClockScale, kCompactSpacing);
    DrawTextWithSpacing(date_text, date_x, date_y, kDateScale, kCompactSpacing);

    const int right_x = divider_x + 1;
    const int right_width = std::max(1, width_ - right_x);
    constexpr int kWifiIconWidth = 4 * 3 + 3 * 2;
    const int wifi_x = right_x + std::max(0, (right_width - kWifiIconWidth) / 2);
    DrawWifiBars(wifi_x, 1, wifi_rssi);

    const int status_width = TextWidthWithSpacing(status_text, 1, kCompactSpacing);
    const int status_x = right_x + std::max(0, (right_width - status_width) / 2);
    const int status_y = std::max(0, height_ - oled_clock_font::kGlyphHeight - 1);
    DrawTextWithSpacing(status_text, status_x, status_y, 1, kCompactSpacing);
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
    if (TextHeight(normalized, scale) > height_) {
        scale = 1;
    }
    const int text_height = TextHeight(normalized, scale);
    if (TextWidth(normalized, scale) <= width_) {
        DrawCenteredText(normalized, (height_ - text_height) / 2, scale);
        return;
    }
    const int text_width = TextWidth(normalized, scale);
    const int gap = width_ / 3;
    if (last_mode_ != OledDisplayMode::Marquee || std::strcmp(last_text_, normalized) != 0) {
        marquee_offset_ = 0;
    }
    const int x = width_ - marquee_offset_;
    DrawText(normalized, x, (height_ - text_height) / 2, scale);
    marquee_offset_ += std::clamp<int>(settings.speed, 1, 5);
    if (marquee_offset_ > text_width + gap) {
        marquee_offset_ = 0;
    }
}

bool OledDisplayController::Render(const OledDisplaySettings& settings, const char* status_line_1,
                                   const char* status_line_2, time_t now, int wifi_rssi) {
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
            DrawStatus(status_line_1, status_line_2, now, wifi_rssi);
            break;
        case OledDisplayMode::Off:
            break;
    }
    return true;
}

bool OledDisplayController::RenderBoot(const char* title, const char* network_line) {
    if (framebuffer_ == nullptr || framebuffer_size_ == 0 || width_ <= 0 || height_ <= 0) {
        return false;
    }

    Clear();
    DrawBoot(title, network_line);
    return true;
}

}  // namespace mrkenddo
