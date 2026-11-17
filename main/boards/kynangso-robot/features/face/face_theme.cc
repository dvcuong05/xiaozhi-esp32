#include "features/face/face_theme.h"

#include <cJSON.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace kynangso {
bool ParseFaceTheme(const std::string& json, FaceTheme& theme, std::string& error) {
    error = "Gói theme không hợp lệ: cần JSON phẳng version 1, tên, style và bốn màu #RRGGBB.";
    if (json.empty() || json.size() > 2048 || json.find('\0') != std::string::npos)
        return false;
    // Bound parser recursion before calling cJSON, including on the worker's small stack.
    bool quoted = false, escaped = false;
    int depth = 0;
    for (char c : json) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 1)
                return false;
        } else if (c == '}' || c == ']') {
            if (--depth < 0)
                return false;
        }
    }
    if (quoted || depth != 0)
        return false;
    using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    Json root(cJSON_ParseWithOpts(json.c_str(), nullptr, true), cJSON_Delete);
    if (!root || !cJSON_IsObject(root.get()))
        return false;
    const char* keys[] = {"version", "id",     "name", "style",
                          "primary", "accent", "tear", "background"};
    for (const auto* key : keys) {
        unsigned count = 0;
        for (auto* item = root->child; item; item = item->next)
            if (item->string && std::strcmp(item->string, key) == 0)
                ++count;
        if (count != 1)
            return false;
    }
    if (cJSON_GetArraySize(root.get()) != 8)
        return false;
    const auto* version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1)
        return false;
    auto text = [&](const char* key) -> const char* {
        const auto* item = cJSON_GetObjectItemCaseSensitive(root.get(), key);
        return cJSON_IsString(item) ? item->valuestring : nullptr;
    };
    FaceTheme candidate;
    const char *id = text("id"), *name = text("name"), *style = text("style");
    if (!id || !name || !style || !*id || std::strlen(id) > 32 || !*name || std::strlen(name) > 64)
        return false;
    for (const char* p = id; *p; ++p)
        if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && *p != '-' && *p != '_')
            return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
        if (*p < 0x20 || *p == 0x7f)
            return false;
    candidate.id = id;
    candidate.name = name;
    candidate.style = style;
    if (candidate.style != "rounded" && candidate.style != "soft" && candidate.style != "pixel" &&
        candidate.style != "holo" && candidate.style != "orbital" && candidate.style != "plasma" &&
        candidate.style != "demon" && candidate.style != "capybara" && candidate.style != "shiba" &&
        candidate.style != "mochi" && candidate.style != "gif-poc")
        return false;
    if (candidate.style == "gif-poc" && candidate.id != "gif-poc")
        return false;
    auto color = [&](const char* key, uint32_t& value) {
        const char* hex = text(key);
        if (!hex || std::strlen(hex) != 7 || hex[0] != '#')
            return false;
        for (int i = 1; i < 7; ++i)
            if (!(hex[i] >= '0' && hex[i] <= '9') && !(hex[i] >= 'a' && hex[i] <= 'f') &&
                !(hex[i] >= 'A' && hex[i] <= 'F'))
                return false;
        value = static_cast<uint32_t>(std::strtoul(hex + 1, nullptr, 16));
        return true;
    };
    if (!color("primary", candidate.primary) || !color("accent", candidate.accent) ||
        !color("tear", candidate.tear) || !color("background", candidate.background))
        return false;
    auto brightness = [](uint32_t c) {
        return (299 * ((c >> 16) & 255) + 587 * ((c >> 8) & 255) + 114 * (c & 255)) / 1000;
    };
    if (std::abs(static_cast<int>(brightness(candidate.primary)) -
                 static_cast<int>(brightness(candidate.background))) < 60) {
        error = "Màu mắt và nền quá gần nhau; hãy tăng độ tương phản.";
        return false;
    }
    theme = std::move(candidate);
    error.clear();
    return true;
}

std::string EncodeFaceTheme(const FaceTheme& theme) {
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return {};
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "id", theme.id.c_str());
    cJSON_AddStringToObject(root, "name", theme.name.c_str());
    cJSON_AddStringToObject(root, "style", theme.style.c_str());
    auto color = [&](const char* key, uint32_t value) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%06lX", static_cast<unsigned long>(value & 0xFFFFFF));
        cJSON_AddStringToObject(root, key, hex);
    };
    color("primary", theme.primary);
    color("accent", theme.accent);
    color("tear", theme.tear);
    color("background", theme.background);
    char* encoded = cJSON_PrintUnformatted(root);
    std::string result = encoded ? encoded : "";
    cJSON_free(encoded);
    cJSON_Delete(root);
    return result;
}
}  // namespace kynangso
