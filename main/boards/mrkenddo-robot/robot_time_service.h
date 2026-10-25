#pragma once

#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace mrkenddo {
struct TimeView {
    bool visible = false, alert = false, paused = false;
    std::string mode, title, subtitle, digits;
    int progress = 0;
};

enum class TimeRingtone {
    None,
    Alarm,
    Reminder,
    Countdown,
    FocusStart,
    BreakStart,
    PomodoroComplete
};

// Board-local scheduler. Wall-clock appointments persist; session timers use
// monotonic time and deliberately end on power loss. All UI runs on Application.
class RobotTimeService : public std::enable_shared_from_this<RobotTimeService> {
public:
    using Present = std::function<void(const TimeView&, TimeRingtone)>;
    explicit RobotTimeService(Present present);
    ~RobotTimeService();
    bool Start();
    void Stop();
    std::string Command(const cJSON* request);
    bool DismissAlert();

private:
    struct Entry {
        int id = 0, hour = 0, minute = 0, days = 0;
        bool enabled = false, alarm = false;
        int64_t due = 0;
        std::string title;
    };
    struct Alert {
        int id;
        std::string title, mode;
        int64_t until;
        uint32_t serial;
        int duration;
        TimeRingtone ringtone = TimeRingtone::None;
    };
    std::array<Entry, 8> entries_{};
    std::deque<Alert> alerts_;
    std::mutex mutex_;
    Present present_;
    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t stopped_ = nullptr;
    std::atomic<bool> stopping_{false}, pending_{false};
    bool storage_ok_ = true, running_ = false, paused_ = false, pomodoro_ = false;
    bool rest_ = false, visible_ = true;
    int focus_ = 1500, short_break_ = 300, long_break_ = 900, rounds_ = 4, round_ = 1;
    int64_t deadline_ = 0, remaining_ = 0, total_ = 0, next_sound_ = 0;
    std::string timer_title_;
    uint32_t alert_serial_ = 0, presented_alert_ = 0;
    TimeView view_;
    void Tick();
    void Load();
    bool Save();
    cJSON* EntriesJson(const char* scope = nullptr, bool include_details = true) const;
    int64_t NextAlarm(const Entry& entry, int64_t after) const;
    void PushAlert(int id, const std::string& title, const char* mode, TimeRingtone ringtone,
                   int seconds = 120);
    std::string Status(const char* error = nullptr, const char* scope = nullptr) const;
};
}  // namespace mrkenddo
