#ifndef KYNANGSO_ROBOT_DRIVE_H_
#define KYNANGSO_ROBOT_DRIVE_H_

#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

// Two DC motors on the two H-bridges of an L298N, not a four-coil stepper.
class RobotDrive {
public:
    enum class Direction { Forward, Backward, Left, Right };
    enum class Routine { Lively, Dance };
    static constexpr int kMaxSteps = 30000;
    static constexpr int kMinDelayMs = 1;
    static constexpr int kMaxDelayMs = 100;
    static constexpr int kMaxRunTimeMs = 10000;

    RobotDrive(gpio_num_t left_a, gpio_num_t left_b, gpio_num_t right_a, gpio_num_t right_b);
    ~RobotDrive();

    bool Move(Direction direction, int steps, int delay_ms = 0);
    bool Step(int steps, int delay_ms = 0);
    bool StartRoutine(Routine routine);
    bool IsRoutineRunning() const { return routine_running_.load(); }
    void Stop();
    void SetSpeed(int delay_ms);
    void SetInversion(bool left_inverted, bool right_inverted);
    void ConfigureEmotionMotion(bool enabled, int intensity);
    void SetSpeaking(bool speaking);
    bool PlayEmotion(int preset, bool preview = false);

private:
    gpio_num_t left_a_;
    gpio_num_t left_b_;
    gpio_num_t right_a_;
    gpio_num_t right_b_;
    SemaphoreHandle_t mutex_ = nullptr;
    esp_timer_handle_t stop_timer_ = nullptr;
    int delay_ms_ = 1;
    bool left_inverted_ = false;
    bool right_inverted_ = true;
    bool running_ = false;
    int64_t deadline_us_ = 0;
    esp_timer_handle_t emotion_timer_ = nullptr;
    bool emotion_enabled_ = false;
    bool speaking_ = false;
    bool emotion_active_ = false;
    bool emotion_preview_ = false;
    bool emotion_suppressed_ = false;
    int emotion_intensity_ = 60;
    int emotion_preset_ = -1;
    int emotion_phase_ = 0;
    int64_t emotion_deadline_ = 0;
    static void EmotionTimer(void* context);
    void CancelEmotionLocked();
    TaskHandle_t routine_task_ = nullptr;
    Routine routine_ = Routine::Lively;
    std::atomic<bool> routine_cancel_{false};
    std::atomic<bool> routine_running_{false};

    static void StopTimer(void* context);
    static void RoutineTask(void* context);
    void RunRoutine(Routine routine);
    void StopMotion();
    void SetOutputsLocked(int left, int right);
};

#endif  // KYNANGSO_ROBOT_DRIVE_H_
