#pragma once

#include <cstdint>
#include <string>

namespace kynangso {
// Small procedural theme: every existing emotion and audio-driven mouth is retained.
struct FaceTheme {
    std::string id = "classic";
    std::string name = "Classic Cyan";
    std::string style = "rounded";
    uint32_t primary = 0x0ABDE3;
    uint32_t accent = 0xFF70AE;
    uint32_t tear = 0x69D9FF;
    uint32_t background = 0x000000;
};
bool ParseFaceTheme(const std::string& json, FaceTheme& theme, std::string& error);
std::string EncodeFaceTheme(const FaceTheme& theme);
}  // namespace kynangso
