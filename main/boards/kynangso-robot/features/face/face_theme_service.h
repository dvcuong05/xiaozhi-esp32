#pragma once

#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include "features/face/face_theme.h"

namespace kynangso {
class RobotSettings;
class SdStorage;
class FaceThemeService {
public:
    FaceThemeService(RobotSettings& settings, SdStorage& storage,
                     std::function<void(const FaceTheme&)> apply);
    ~FaceThemeService();
    void Start();
    bool Request(const std::string& action, const std::string& id, const std::string& manifest);
    void RequestEmotion(const char* emotion);
    cJSON* Snapshot() const;

private:
    enum class GifLoadResult { Ok, Busy, Error };
    struct Entry {
        FaceTheme theme;
        bool builtin;
    };
    struct GifCacheEntry {
        std::string theme_id;
        std::string expression;
        std::shared_ptr<const FaceGifData> data;
        uint32_t last_used = 0;
    };
    RobotSettings& settings_;
    SdStorage& storage_;
    std::function<void(const FaceTheme&)> apply_;
    mutable std::mutex mutex_;
    std::vector<Entry> catalog_;
    FaceTheme active_;
    std::string action_, id_, manifest_, error_, message_;
    std::string requested_expression_ = "neutral";
    uint32_t gif_request_generation_ = 0;
    std::array<GifCacheEntry, 2> gif_cache_{};
    uint32_t gif_cache_clock_ = 0;
    bool busy_ = false;
    bool sd_available_ = false;
    bool truncated_ = false;
    unsigned skipped_ = 0;
    uint32_t operation_ = 0;
    std::atomic<bool> stop_{false};
    TaskHandle_t worker_ = nullptr;
    SemaphoreHandle_t stopped_ = nullptr;
    void Run();
    std::string Refresh();
    std::string Install(const std::string& manifest);
    std::string Delete(const std::string& id);
    std::string Select(const std::string& id);
    std::string LoadGifPair(FaceTheme& theme);
    GifLoadResult LoadGifExpression(const FaceTheme& theme, const std::string& expression,
                                    std::shared_ptr<const FaceGifData>& output, std::string& error);
    GifLoadResult ValidateGifThemeFiles(const FaceTheme& theme, std::string& error);
    std::shared_ptr<const FaceGifData> FindCachedGif(const std::string& theme_id,
                                                     const std::string& expression);
    void CacheGif(const std::string& theme_id, const std::string& expression,
                  std::shared_ptr<const FaceGifData> data);
    void ClearGifCache();
    void EvictInactiveGifCache(const std::string& theme_id, const std::string& active_expression);
    bool ApplyRequestedGif(uint32_t generation, const std::string& expression);
};
}  // namespace kynangso
