#pragma once

#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
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
    cJSON* Snapshot() const;

private:
    struct Entry {
        FaceTheme theme;
        bool builtin;
    };
    RobotSettings& settings_;
    SdStorage& storage_;
    std::function<void(const FaceTheme&)> apply_;
    mutable std::mutex mutex_;
    std::vector<Entry> catalog_;
    FaceTheme active_;
    std::string action_, id_, manifest_, error_, message_;
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
    std::string Select(const std::string& id);
    std::string LoadGifPair(FaceTheme& theme);
};
}  // namespace kynangso
