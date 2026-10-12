#include "robot_drive.h"

#include <algorithm>
#include <cstdlib>

#include <esp_log.h>
#include <nvs.h>

namespace {
constexpr char kTag[] = "RobotDrive";
}

RobotDrive::RobotDrive(gpio_num_t left_a, gpio_num_t left_b, gpio_num_t right_a, gpio_num_t right_b)
    : left_a_(left_a), left_b_(left_b), right_a_(right_a), right_b_(right_b) {
    gpio_config_t pins = {};
    pins.pin_bit_mask =
        (1ULL << left_a_) | (1ULL << left_b_) | (1ULL << right_a_) | (1ULL << right_b_);
    pins.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&pins) != ESP_OK) {
        ESP_LOGE(kTag, "Cannot configure motor GPIOs");
        return;
    }
    gpio_set_level(left_a_, 0);
    gpio_set_level(left_b_, 0);
    gpio_set_level(right_a_, 0);
    gpio_set_level(right_b_, 0);
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        ESP_LOGE(kTag, "Cannot create motor mutex");
        return;
    }
    const esp_timer_create_args_t timer_args = {
        .callback = StopTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "robot_drive_stop",
    };
    if (esp_timer_create(&timer_args, &stop_timer_) != ESP_OK) {
        ESP_LOGE(kTag, "Cannot create motor stop timer");
    }

    nvs_handle_t handle;
    if (nvs_open("motor_config", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t left = 0;
        uint8_t right = 1;
        int32_t saved_delay = 1;
        if (nvs_get_i32(handle, "delay_ms", &saved_delay) == ESP_OK && saved_delay >= kMinDelayMs &&
            saved_delay <= kMaxDelayMs) {
            delay_ms_ = saved_delay;
        }
        if (nvs_get_u8(handle, "left_inv", &left) == ESP_OK && left <= 1) {
            left_inverted_ = left != 0;
        }
        if (nvs_get_u8(handle, "right_inv", &right) == ESP_OK && right <= 1) {
            right_inverted_ = right != 0;
        }
        nvs_close(handle);
    }
}

RobotDrive::~RobotDrive() {
    Stop();
    if (stop_timer_ != nullptr) {
        esp_timer_delete(stop_timer_);
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

void RobotDrive::SetOutputsLocked(int left, int right) {
    if (left_inverted_)
        left = -left;
    if (right_inverted_)
        right = -right;
    gpio_set_level(left_a_, left > 0);
    gpio_set_level(left_b_, left < 0);
    gpio_set_level(right_a_, right > 0);
    gpio_set_level(right_b_, right < 0);
}

bool RobotDrive::Move(Direction direction, int steps, int delay_ms) {
    if (steps <= 0 || steps > kMaxSteps || delay_ms < 0 || delay_ms > kMaxDelayMs ||
        mutex_ == nullptr || stop_timer_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    int effective_delay = delay_ms == 0 ? delay_ms_ : delay_ms;
    int64_t duration_ms = static_cast<int64_t>(steps) * effective_delay;
    if (duration_ms > kMaxRunTimeMs) {
        xSemaphoreGive(mutex_);
        return false;
    }
    esp_timer_stop(stop_timer_);  // A new command replaces the previous timeout.
    int left = 0;
    int right = 0;
    switch (direction) {
        case Direction::Forward:
            left = 1;
            right = 1;
            break;
        case Direction::Backward:
            left = -1;
            right = -1;
            break;
        case Direction::Left:
            left = -1;
            right = 1;
            break;
        case Direction::Right:
            left = 1;
            right = -1;
            break;
    }
    // The old stepper API used steps * delay_ms; retain that duration contract.
    int64_t duration_us = duration_ms * 1000;
    esp_err_t result = esp_timer_start_once(stop_timer_, duration_us);
    if (result == ESP_OK) {
        deadline_us_ = esp_timer_get_time() + duration_us;
        SetOutputsLocked(left, right);
        running_ = true;
    } else {
        SetOutputsLocked(0, 0);
        running_ = false;
        ESP_LOGE(kTag, "Cannot start motor timeout: %s", esp_err_to_name(result));
    }
    xSemaphoreGive(mutex_);
    return result == ESP_OK;
}

bool RobotDrive::Step(int steps, int delay_ms) {
    if (steps == 0) {
        Stop();
        return true;
    }
    if (steps < -kMaxSteps || steps > kMaxSteps)
        return false;
    return Move(steps > 0 ? Direction::Forward : Direction::Backward, std::abs(steps), delay_ms);
}

void RobotDrive::Stop() {
    if (mutex_ == nullptr)
        return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stop_timer_ != nullptr)
        esp_timer_stop(stop_timer_);
    SetOutputsLocked(0, 0);
    running_ = false;
    deadline_us_ = 0;
    xSemaphoreGive(mutex_);
}

void RobotDrive::StopTimer(void* context) {
    auto* drive = static_cast<RobotDrive*>(context);
    xSemaphoreTake(drive->mutex_, portMAX_DELAY);
    // A callback queued for an old command must not stop a newer move.
    if (drive->running_ && esp_timer_get_time() >= drive->deadline_us_) {
        drive->SetOutputsLocked(0, 0);
        drive->running_ = false;
        drive->deadline_us_ = 0;
    }
    xSemaphoreGive(drive->mutex_);
}

void RobotDrive::SetSpeed(int delay_ms) {
    if (delay_ms < kMinDelayMs || delay_ms > kMaxDelayMs || mutex_ == nullptr)
        return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    delay_ms_ = delay_ms;
    xSemaphoreGive(mutex_);
}

void RobotDrive::SetInversion(bool left_inverted, bool right_inverted) {
    if (mutex_ == nullptr)
        return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    // Stop before changing polarity to avoid reversing under power.
    if (stop_timer_ != nullptr)
        esp_timer_stop(stop_timer_);
    SetOutputsLocked(0, 0);
    running_ = false;
    deadline_us_ = 0;
    left_inverted_ = left_inverted;
    right_inverted_ = right_inverted;
    xSemaphoreGive(mutex_);
}
