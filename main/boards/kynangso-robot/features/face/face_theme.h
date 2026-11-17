#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace kynangso {
inline constexpr size_t kFaceGifExpressionCount = 17;
const char* FaceGifExpressionName(size_t index);
int FaceGifExpressionIndex(const char* name);
int CanonicalFaceGifExpression(const char* name);

// Compressed GIF data is shared with the display and released when the theme changes.
struct FaceGifData {
    std::shared_ptr<uint8_t> bytes;
    size_t size = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t frame_count = 0;
};
struct FaceGifPair {
    FaceGifData idle;
    FaceGifData talking;
};
// Small procedural theme: every existing emotion and audio-driven mouth is retained.
struct FaceTheme {
    std::string id = "classic";
    std::string name = "Classic Cyan";
    std::string style = "rounded";
    uint32_t primary = 0x0ABDE3;
    uint32_t accent = 0xFF70AE;
    uint32_t tear = 0x69D9FF;
    uint32_t background = 0x000000;
    std::array<bool, kFaceGifExpressionCount> gif_expressions{};
    std::shared_ptr<const FaceGifPair> gifs;
    std::shared_ptr<const FaceGifData> active_gif;
    std::string active_gif_expression;

    bool HasGifExpressions() const {
        for (bool present : gif_expressions)
            if (present)
                return true;
        return false;
    }
};
bool ParseFaceTheme(const std::string& json, FaceTheme& theme, std::string& error);
std::string EncodeFaceTheme(const FaceTheme& theme);
}  // namespace kynangso
