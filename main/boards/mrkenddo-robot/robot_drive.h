#ifndef MRKENDDO_ROBOT_DRIVE_H_
#define MRKENDDO_ROBOT_DRIVE_H_

#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Two DC motors on the two H-bridges of an L298N, not a four-coil stepper.
class RobotDrive {
public:
    enum class Direction { Forward, Backward, Left, Right };
    static constexpr int kMaxSteps = 30000;
    static constexpr int kMinDelayMs = 1;
    static constexpr int kMaxDelayMs = 100;
    static constexpr int kMaxRunTimeMs = 10000;

    RobotDrive(gpio_num_t left_a, gpio_num_t left_b, gpio_num_t right_a, gpio_num_t right_b);
    ~RobotDrive();

    bool Move(Direction direction, int steps, int delay_ms = 0);
    bool Step(int steps, int delay_ms = 0);
    void Stop();
    void SetSpeed(int delay_ms);
    void SetInversion(bool left_inverted, bool right_inverted);

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

    static void StopTimer(void* context);
    void SetOutputsLocked(int left, int right);
};

#endif  // MRKENDDO_ROBOT_DRIVE_H_
