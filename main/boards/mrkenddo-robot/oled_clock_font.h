#ifndef MRKENDDO_ROBOT_OLED_CLOCK_FONT_H_
#define MRKENDDO_ROBOT_OLED_CLOCK_FONT_H_

#include <cstdint>

// Compact bitmap glyphs for the secondary SSD1306. The normal Latin glyphs
// use a 5x7 bitmap. Vietnamese marks are drawn above/below that bitmap by the
// renderer, which keeps the font data small while retaining UTF-8 input.
namespace mrkenddo::oled_clock_font {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;

enum class Mark : uint8_t {
    None,
    Acute,
    Grave,
    Hook,
    Tilde,
    Dot,
    Breve,
    Circumflex,
    Horn,
    Stroke,
};

struct Glyph {
    const uint8_t* bitmap;
    uint8_t width;
    uint8_t height;
    uint8_t base_y;
    Mark shape;
    Mark tone;
};

constexpr uint8_t kDigits[10][kGlyphHeight] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},  // 5
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},  // 9
};

constexpr uint8_t kColon[kGlyphHeight] = {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00};
constexpr uint8_t kDash[kGlyphHeight] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
constexpr uint8_t kSpace[kGlyphHeight] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kPeriod[kGlyphHeight] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
constexpr uint8_t kComma[kGlyphHeight] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x04};
constexpr uint8_t kSlash[kGlyphHeight] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
constexpr uint8_t kUnderscore[kGlyphHeight] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
constexpr uint8_t kExclamation[kGlyphHeight] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
constexpr uint8_t kQuestion[kGlyphHeight] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};

constexpr uint8_t kLetters[26][kGlyphHeight] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  // C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},  // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},  // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},  // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},  // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},  // I
    {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},  // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},  // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},  // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},  // M
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},  // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},  // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},  // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},  // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},  // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},  // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},  // W
    {0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11},  // X
    {0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04},  // Y
    {0x1F, 0x02, 0x04, 0x08, 0x10, 0x10, 0x1F},  // Z
};

// Small monochrome icons commonly useful in the OLED message/status field.
constexpr uint8_t kEmojiSmile[8] = {0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C};
constexpr uint8_t kEmojiSad[8] = {0x3C, 0x42, 0xA5, 0x81, 0x99, 0xA5, 0x42, 0x3C};
constexpr uint8_t kEmojiAngry[8] = {0x3C, 0x42, 0xBD, 0x81, 0xA5, 0x99, 0x42, 0x3C};
constexpr uint8_t kEmojiThumbUp[8] = {0x08, 0x0C, 0x0E, 0x0F, 0x7E, 0x7C, 0x38, 0x10};
constexpr uint8_t kEmojiThumbDown[8] = {0x10, 0x38, 0x7C, 0x7E, 0x0F, 0x0E, 0x0C, 0x08};
constexpr uint8_t kEmojiHeart[8] = {0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00};
constexpr uint8_t kEmojiWarning[8] = {0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0x18, 0x00, 0x18};
constexpr uint8_t kEmojiCheck[8] = {0x01, 0x03, 0x06, 0x8C, 0xD8, 0x70, 0x20, 0x00};
constexpr uint8_t kEmojiStar[8] = {0x18, 0x18, 0x7E, 0xFF, 0x3C, 0x66, 0x42, 0x00};
constexpr uint8_t kEmojiRobot[8] = {0x7E, 0xA5, 0xBD, 0x81, 0xBD, 0x99, 0xFF, 0x00};

constexpr Glyph MakeGlyph(const uint8_t* bitmap, uint8_t width = kGlyphWidth,
                          uint8_t height = kGlyphHeight, uint8_t base_y = 0,
                          Mark shape = Mark::None, Mark tone = Mark::None) {
    return {bitmap, width, height, base_y, shape, tone};
}

inline Glyph GetAsciiGlyph(uint32_t codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') {
        codepoint -= 'a' - 'A';
    }
    if (codepoint >= '0' && codepoint <= '9') {
        return MakeGlyph(kDigits[codepoint - '0']);
    }
    if (codepoint == ':') {
        return MakeGlyph(kColon);
    }
    if (codepoint == '-') {
        return MakeGlyph(kDash);
    }
    if (codepoint == ' ') {
        return MakeGlyph(kSpace);
    }
    if (codepoint == '.') {
        return MakeGlyph(kPeriod);
    }
    if (codepoint == ',') {
        return MakeGlyph(kComma);
    }
    if (codepoint == '/') {
        return MakeGlyph(kSlash);
    }
    if (codepoint == '_') {
        return MakeGlyph(kUnderscore);
    }
    if (codepoint == '!') {
        return MakeGlyph(kExclamation);
    }
    if (codepoint == '?' || codepoint < 0x20 || codepoint > 0x7e) {
        return MakeGlyph(kQuestion);
    }
    if (codepoint < 'A' || codepoint > 'Z') {
        return MakeGlyph(kQuestion);
    }
    return MakeGlyph(kLetters[codepoint - 'A']);
}

inline Glyph MakeMarkedGlyph(char base, Mark shape, Mark tone) {
    Glyph glyph = GetAsciiGlyph(static_cast<uint32_t>(base));
    const bool has_top_shape =
        shape == Mark::Breve || shape == Mark::Circumflex || shape == Mark::Horn;
    const bool has_tone = tone != Mark::None;
    const bool has_bottom_dot = tone == Mark::Dot;
    if (has_top_shape && has_tone) {
        glyph.base_y = 3;
        glyph.height = 11;
    } else if (has_top_shape || (has_tone && !has_bottom_dot)) {
        glyph.base_y = 2;
        glyph.height = 9;
    } else if (has_bottom_dot) {
        glyph.base_y = 0;
        glyph.height = 9;
    }
    glyph.shape = shape;
    glyph.tone = tone;
    return glyph;
}

inline Glyph GetEmojiGlyph(uint32_t codepoint) {
    switch (codepoint) {
        case 0x1f600:  // 😀
            return MakeGlyph(kEmojiSmile, 8, 8);
        case 0x1f621:  // 😡
            return MakeGlyph(kEmojiAngry, 8, 8);
        case 0x1f622:  // 😢
            return MakeGlyph(kEmojiSad, 8, 8);
        case 0x1f916:  // 🤖
            return MakeGlyph(kEmojiRobot, 8, 8);
        case 0x1f44d:  // 👍
            return MakeGlyph(kEmojiThumbUp, 8, 8);
        case 0x1f44e:  // 👎
            return MakeGlyph(kEmojiThumbDown, 8, 8);
        case 0x2764:  // ❤
            return MakeGlyph(kEmojiHeart, 8, 8);
        case 0x26a0:  // ⚠
            return MakeGlyph(kEmojiWarning, 8, 8);
        case 0x2705:  // ✅
            return MakeGlyph(kEmojiCheck, 8, 8);
        case 0x2b50:  // ⭐
            return MakeGlyph(kEmojiStar, 8, 8);
        case 0xfe0e:   // text presentation selector
        case 0xfe0f:   // emoji presentation selector
        case 0x200d:   // zero-width joiner
        case 0x1f3fb:  // emoji skin tone modifiers
        case 0x1f3fc:
        case 0x1f3fd:
        case 0x1f3fe:
        case 0x1f3ff:
            return MakeGlyph(nullptr, 0, 0);
        default:
            return MakeGlyph(kQuestion);
    }
}

inline Glyph GetGlyph(uint32_t codepoint) {
    if (codepoint <= 0x7e) {
        return GetAsciiGlyph(codepoint);
    }
    switch (codepoint) {
        case 0x00c0:
            return MakeMarkedGlyph('A', Mark::None, Mark::Grave);
        case 0x00c1:
            return MakeMarkedGlyph('A', Mark::None, Mark::Acute);
        case 0x00c2:
            return MakeMarkedGlyph('A', Mark::Circumflex, Mark::None);
        case 0x00c3:
            return MakeMarkedGlyph('A', Mark::None, Mark::Tilde);
        case 0x00c4:
        case 0x00c5:
            return GetAsciiGlyph('A');
        case 0x00c8:
            return MakeMarkedGlyph('E', Mark::None, Mark::Grave);
        case 0x00c9:
            return MakeMarkedGlyph('E', Mark::None, Mark::Acute);
        case 0x00ca:
            return MakeMarkedGlyph('E', Mark::Circumflex, Mark::None);
        case 0x00cb:
            return GetAsciiGlyph('E');
        case 0x00cc:
            return MakeMarkedGlyph('I', Mark::None, Mark::Grave);
        case 0x00cd:
            return MakeMarkedGlyph('I', Mark::None, Mark::Acute);
        case 0x00ce:
        case 0x00cf:
            return GetAsciiGlyph('I');
        case 0x00d2:
            return MakeMarkedGlyph('O', Mark::None, Mark::Grave);
        case 0x00d3:
            return MakeMarkedGlyph('O', Mark::None, Mark::Acute);
        case 0x00d4:
            return MakeMarkedGlyph('O', Mark::Circumflex, Mark::None);
        case 0x00d5:
        case 0x00d6:
            return GetAsciiGlyph('O');
        case 0x00d9:
            return MakeMarkedGlyph('U', Mark::None, Mark::Grave);
        case 0x00da:
            return MakeMarkedGlyph('U', Mark::None, Mark::Acute);
        case 0x00db:
            return GetAsciiGlyph('U');
        case 0x00dd:
            return MakeMarkedGlyph('Y', Mark::None, Mark::Acute);
        case 0x0110:
        case 0x0111:
            return MakeMarkedGlyph('D', Mark::Stroke, Mark::None);
        case 0x0102:
        case 0x0103:
            return MakeMarkedGlyph('A', Mark::Breve, Mark::None);
        case 0x0118:
        case 0x0119:
            return MakeMarkedGlyph('E', Mark::None, Mark::None);
        case 0x0128:
        case 0x0129:
            return MakeMarkedGlyph('I', Mark::None, Mark::Tilde);
        case 0x01a0:
        case 0x01a1:
            return MakeMarkedGlyph('O', Mark::Horn, Mark::None);
        case 0x01af:
        case 0x01b0:
            return MakeMarkedGlyph('U', Mark::Horn, Mark::None);
        case 0x0168:
        case 0x0169:
            return MakeMarkedGlyph('U', Mark::None, Mark::Tilde);
        case 0x00e0:
            return MakeMarkedGlyph('A', Mark::None, Mark::Grave);
        case 0x00e1:
            return MakeMarkedGlyph('A', Mark::None, Mark::Acute);
        case 0x00e2:
            return MakeMarkedGlyph('A', Mark::Circumflex, Mark::None);
        case 0x00e3:
            return MakeMarkedGlyph('A', Mark::None, Mark::Tilde);
        case 0x00e8:
            return MakeMarkedGlyph('E', Mark::None, Mark::Grave);
        case 0x00e9:
            return MakeMarkedGlyph('E', Mark::None, Mark::Acute);
        case 0x00ea:
            return MakeMarkedGlyph('E', Mark::Circumflex, Mark::None);
        case 0x00ec:
            return MakeMarkedGlyph('I', Mark::None, Mark::Grave);
        case 0x00ed:
            return MakeMarkedGlyph('I', Mark::None, Mark::Acute);
        case 0x00f2:
            return MakeMarkedGlyph('O', Mark::None, Mark::Grave);
        case 0x00f3:
            return MakeMarkedGlyph('O', Mark::None, Mark::Acute);
        case 0x00f4:
            return MakeMarkedGlyph('O', Mark::Circumflex, Mark::None);
        case 0x00f9:
            return MakeMarkedGlyph('U', Mark::None, Mark::Grave);
        case 0x00fa:
            return MakeMarkedGlyph('U', Mark::None, Mark::Acute);
        case 0x00fb:
            return GetAsciiGlyph('U');
        case 0x00fd:
            return MakeMarkedGlyph('Y', Mark::None, Mark::Acute);
        case 0x00ff:
            return GetAsciiGlyph('Y');
        case 0x1ea0:
        case 0x1ea1:
            return MakeMarkedGlyph('A', Mark::None, Mark::Dot);
        case 0x1ea2:
        case 0x1ea3:
            return MakeMarkedGlyph('A', Mark::None, Mark::Hook);
        case 0x1ea4:
        case 0x1ea5:
            return MakeMarkedGlyph('A', Mark::Circumflex, Mark::Acute);
        case 0x1ea6:
        case 0x1ea7:
            return MakeMarkedGlyph('A', Mark::Circumflex, Mark::Grave);
        case 0x1ea8:
        case 0x1ea9:
            return MakeMarkedGlyph('A', Mark::Circumflex, Mark::Hook);
        case 0x1eaa:
        case 0x1eab:
            return MakeMarkedGlyph('A', Mark::Circumflex, Mark::Tilde);
        case 0x1eac:
        case 0x1ead:
            return MakeMarkedGlyph('A', Mark::Circumflex, Mark::Dot);
        case 0x1eae:
        case 0x1eaf:
            return MakeMarkedGlyph('A', Mark::Breve, Mark::Acute);
        case 0x1eb0:
        case 0x1eb1:
            return MakeMarkedGlyph('A', Mark::Breve, Mark::Grave);
        case 0x1eb2:
        case 0x1eb3:
            return MakeMarkedGlyph('A', Mark::Breve, Mark::Hook);
        case 0x1eb4:
        case 0x1eb5:
            return MakeMarkedGlyph('A', Mark::Breve, Mark::Tilde);
        case 0x1eb6:
        case 0x1eb7:
            return MakeMarkedGlyph('A', Mark::Breve, Mark::Dot);
        case 0x1eb8:
        case 0x1eb9:
            return MakeMarkedGlyph('E', Mark::None, Mark::Dot);
        case 0x1eba:
        case 0x1ebb:
            return MakeMarkedGlyph('E', Mark::None, Mark::Hook);
        case 0x1ebc:
        case 0x1ebd:
            return MakeMarkedGlyph('E', Mark::None, Mark::Tilde);
        case 0x1ebe:
        case 0x1ebf:
            return MakeMarkedGlyph('E', Mark::Circumflex, Mark::Acute);
        case 0x1ec0:
        case 0x1ec1:
            return MakeMarkedGlyph('E', Mark::Circumflex, Mark::Grave);
        case 0x1ec2:
        case 0x1ec3:
            return MakeMarkedGlyph('E', Mark::Circumflex, Mark::Hook);
        case 0x1ec4:
        case 0x1ec5:
            return MakeMarkedGlyph('E', Mark::Circumflex, Mark::Tilde);
        case 0x1ec6:
        case 0x1ec7:
            return MakeMarkedGlyph('E', Mark::Circumflex, Mark::Dot);
        case 0x1ec8:
        case 0x1ec9:
            return MakeMarkedGlyph('I', Mark::None, Mark::Hook);
        case 0x1eca:
        case 0x1ecb:
            return MakeMarkedGlyph('I', Mark::None, Mark::Dot);
        case 0x1ecc:
        case 0x1ecd:
            return MakeMarkedGlyph('O', Mark::None, Mark::Dot);
        case 0x1ece:
        case 0x1ecf:
            return MakeMarkedGlyph('O', Mark::None, Mark::Hook);
        case 0x1ed0:
        case 0x1ed1:
            return MakeMarkedGlyph('O', Mark::Circumflex, Mark::Acute);
        case 0x1ed2:
        case 0x1ed3:
            return MakeMarkedGlyph('O', Mark::Circumflex, Mark::Grave);
        case 0x1ed4:
        case 0x1ed5:
            return MakeMarkedGlyph('O', Mark::Circumflex, Mark::Hook);
        case 0x1ed6:
        case 0x1ed7:
            return MakeMarkedGlyph('O', Mark::Circumflex, Mark::Tilde);
        case 0x1ed8:
        case 0x1ed9:
            return MakeMarkedGlyph('O', Mark::Circumflex, Mark::Dot);
        case 0x1eda:
        case 0x1edb:
            return MakeMarkedGlyph('O', Mark::Horn, Mark::Acute);
        case 0x1edc:
        case 0x1edd:
            return MakeMarkedGlyph('O', Mark::Horn, Mark::Grave);
        case 0x1ede:
        case 0x1edf:
            return MakeMarkedGlyph('O', Mark::Horn, Mark::Hook);
        case 0x1ee0:
        case 0x1ee1:
            return MakeMarkedGlyph('O', Mark::Horn, Mark::Tilde);
        case 0x1ee2:
        case 0x1ee3:
            return MakeMarkedGlyph('O', Mark::Horn, Mark::Dot);
        case 0x1ee4:
        case 0x1ee5:
            return MakeMarkedGlyph('U', Mark::None, Mark::Dot);
        case 0x1ee6:
        case 0x1ee7:
            return MakeMarkedGlyph('U', Mark::None, Mark::Hook);
        case 0x1ee8:
        case 0x1ee9:
            return MakeMarkedGlyph('U', Mark::Horn, Mark::Acute);
        case 0x1eea:
        case 0x1eeb:
            return MakeMarkedGlyph('U', Mark::Horn, Mark::Grave);
        case 0x1eec:
        case 0x1eed:
            return MakeMarkedGlyph('U', Mark::Horn, Mark::Hook);
        case 0x1eee:
        case 0x1eef:
            return MakeMarkedGlyph('U', Mark::Horn, Mark::Tilde);
        case 0x1ef0:
        case 0x1ef1:
            return MakeMarkedGlyph('U', Mark::Horn, Mark::Dot);
        case 0x1ef2:
        case 0x1ef3:
            return MakeMarkedGlyph('Y', Mark::None, Mark::Grave);
        case 0x1ef4:
        case 0x1ef5:
            return MakeMarkedGlyph('Y', Mark::None, Mark::Dot);
        case 0x1ef6:
        case 0x1ef7:
            return MakeMarkedGlyph('Y', Mark::None, Mark::Hook);
        case 0x1ef8:
        case 0x1ef9:
            return MakeMarkedGlyph('Y', Mark::None, Mark::Tilde);
        default:
            return GetEmojiGlyph(codepoint);
    }
}

}  // namespace mrkenddo::oled_clock_font

#endif  // MRKENDDO_ROBOT_OLED_CLOCK_FONT_H_
