#ifndef KYNANGSO_ROBOT_LIVE_TV_H_
#define KYNANGSO_ROBOT_LIVE_TV_H_

#include <atomic>
#include <mutex>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class Application;
class KyNangSoRobotDisplay;

namespace kynangso {

// Board-local live TV player. The server supplies bounded
// MJPEG frames while the existing radio pipeline plays the paired audio URL.
class RobotLiveTv {
public:
    RobotLiveTv(Application& application, KyNangSoRobotDisplay& display);
    ~RobotLiveTv();

    bool Start(std::string title, std::string video_url, std::string audio_url, std::string* error,
               std::string session_id = {}, std::string channel_id = {});
    std::string StatusJson();
    bool StartDisplayBenchmark(std::string* error);
    bool Stop();
    void StopVideoOnly();
    bool IsActive() const { return active_.load(); }

private:
    static void TaskEntry(void* context);
    void Run();
    void RunDisplayBenchmark();
    void StopAfterFailure(const char* reason);
    void FinishAfterRadioStopped();

    Application& application_;
    KyNangSoRobotDisplay& display_;
    mutable std::mutex mutex_;
    std::string title_;
    std::string video_url_;
    std::string session_id_, channel_id_, last_error_;
    std::atomic<bool> reconnecting_{false};
    std::atomic<uint32_t> frames_{0}, fps_x100_{0};
    bool display_benchmark_ = false;
    std::atomic<bool> active_{false};
    std::atomic<TaskHandle_t> task_{nullptr};
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_LIVE_TV_H_
