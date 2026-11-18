#include "features/face/face_theme.h"

#include <cJSON.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace kynangso {
namespace {
constexpr const char* kGifExpressions[] = {
    "neutral",  "happy",     "sad",   "angry",   "surprised", "sleepy",
    "confused", "listening", "laugh", "love",    "dizzy",     "distracted",
    "devil",    "confident", "yawn",  "playful", "speaking",  "delicious",
    "crying",   "embarrassed", "cool"};
constexpr size_t kExpressionCount = sizeof(kGifExpressions) / sizeof(kGifExpressions[0]);

struct EmotionAlias {
    const char* alias;
    const char* canonical;
};
constexpr EmotionAlias kEmotionAliases[] = {{"idle", "neutral"},      {"smile", "happy"},
                                            {"joy", "laugh"},         {"astonished", "surprised"},
                                            {"wow", "surprised"},     {"panic", "surprised"},
                                            {"cry", "crying"},        {"disappointed", "sad"},
                                            {"anger", "angry"},
                                            {"sleep", "sleepy"},      {"thinking", "confused"},
                                            {"shocked", "surprised"}, {"relaxed", "neutral"},
                                            {"funny", "laugh"},       {"laughing", "laugh"},
                                            {"kiss", "love"},         {"silly", "devil"},
                                            {"winking", "playful"},
                                            {"loving", "love"},       {"heart", "love"},
                                            {"mischievous", "devil"},
                                            {"wink", "playful"},      {"action_yawn", "yawn"},
                                            {"talk", "speaking"},     {"talking", "speaking"},
                                            {"yummy", "delicious"},   {"shy", "embarrassed"},
                                            {"blushing", "embarrassed"}};
}  // namespace

const char* FaceGifExpressionName(size_t index) {
    return index < kExpressionCount ? kGifExpressions[index] : nullptr;
}

int FaceGifExpressionIndex(const char* name) {
    if (name == nullptr)
        return -1;
    for (size_t i = 0; i < kExpressionCount; ++i)
        if (std::strcmp(name, kGifExpressions[i]) == 0)
            return static_cast<int>(i);
    return -1;
}

int CanonicalFaceGifExpression(const char* name) {
    if (name == nullptr)
        return -1;
    char normalized[32] = {};
    size_t length = std::strlen(name);
    if (length == 0 || length >= sizeof(normalized))
        return -1;
    for (size_t i = 0; i < length; ++i)
        normalized[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    if (const int index = FaceGifExpressionIndex(normalized); index >= 0)
        return index;
    for (const auto& alias : kEmotionAliases)
        if (std::strcmp(normalized, alias.alias) == 0)
            return FaceGifExpressionIndex(alias.canonical);
    return -1;
}

bool ParseFaceTheme(const std::string& json, FaceTheme& theme, std::string& error) {
    error = "Gói theme không hợp lệ: cần manifest version 1 hoặc 2 với tên, style và bốn màu #RRGGBB.";
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
            // Version 2 contains one bounded array of expression keys inside the root object.
            if (++depth > 2)
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
    const auto* version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    if (!cJSON_IsNumber(version) || (version->valuedouble != 1 && version->valuedouble != 2))
        return false;
    const bool has_gif_expressions = version->valuedouble == 2;
    const char* keys_v1[] = {"version", "id",     "name", "style",
                             "primary", "accent", "tear", "background"};
    const char* keys_v2[] = {"version", "id",   "name",       "style",      "primary",
                             "accent",  "tear", "background", "expressions"};
    const auto* keys = has_gif_expressions ? keys_v2 : keys_v1;
    const size_t expected_keys = has_gif_expressions ? 9 : 8;
    for (size_t key_index = 0; key_index < expected_keys; ++key_index) {
        const auto* key = keys[key_index];
        unsigned count = 0;
        for (auto* item = root->child; item; item = item->next)
            if (item->string && std::strcmp(item->string, key) == 0)
                ++count;
        if (count != 1)
            return false;
    }
    if (cJSON_GetArraySize(root.get()) != static_cast<int>(expected_keys))
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
    if (has_gif_expressions) {
        const auto* expressions = cJSON_GetObjectItemCaseSensitive(root.get(), "expressions");
        if (!cJSON_IsArray(expressions) || cJSON_GetArraySize(expressions) == 0 ||
            cJSON_GetArraySize(expressions) > static_cast<int>(kExpressionCount))
            return false;
        for (auto* item = expressions->child; item; item = item->next) {
            if (!cJSON_IsString(item))
                return false;
            const int index = FaceGifExpressionIndex(item->valuestring);
            if (index < 0 || candidate.gif_expressions[index])
                return false;
            candidate.gif_expressions[index] = true;
        }
    }
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
    const bool has_gif_expressions = theme.HasGifExpressions();
    cJSON_AddNumberToObject(root, "version", has_gif_expressions ? 2 : 1);
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
    if (has_gif_expressions) {
        cJSON* expressions = cJSON_AddArrayToObject(root, "expressions");
        if (!expressions) {
            cJSON_Delete(root);
            return {};
        }
        for (size_t i = 0; i < kExpressionCount; ++i)
            if (theme.gif_expressions[i])
                cJSON_AddItemToArray(expressions, cJSON_CreateString(kGifExpressions[i]));
    }
    char* encoded = cJSON_PrintUnformatted(root);
    std::string result = encoded ? encoded : "";
    cJSON_free(encoded);
    cJSON_Delete(root);
    return result;
}
}  // namespace kynangso
