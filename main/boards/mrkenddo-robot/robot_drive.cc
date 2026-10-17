#include "robot_drive.h"

#include <algorithm>
#include <cstdlib>

#include <esp_log.h>

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
}

RobotDrive::~RobotDrive() {
    Stop();
    for (int wait_ms = 0; routine_running_.load() && wait_ms < 500; wait_ms += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (routine_task_ != nullptr) {
        vTaskDelete(routine_task_);
        routine_task_ = nullptr;
    }
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
    const bool from_routine = routine_task_ == xTaskGetCurrentTaskHandle();
    if (from_routine && routine_cancel_.load()) {
        xSemaphoreGive(mutex_);
        return false;
    }
    if (!from_routine) {
        routine_cancel_.store(true);
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

bool RobotDrive::StartRoutine(Routine routine) {
    if (mutex_ == nullptr || stop_timer_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (routine_running_.load()) {
        xSemaphoreGive(mutex_);
        return false;
    }
    routine_ = routine;
    routine_cancel_.store(false);
    routine_running_.store(true);
    if (xTaskCreate(RoutineTask, "robot_routine", 3072, this, 4, &routine_task_) != pdPASS) {
        routine_running_.store(false);
        routine_task_ = nullptr;
        xSemaphoreGive(mutex_);
        return false;
    }
    xSemaphoreGive(mutex_);
    return true;
}

void RobotDrive::RoutineTask(void* context) {
    auto* drive = static_cast<RobotDrive*>(context);
    drive->RunRoutine(drive->routine_);
    drive->StopMotion();
    xSemaphoreTake(drive->mutex_, portMAX_DELAY);
    drive->routine_task_ = nullptr;
    drive->routine_running_.store(false);
    xSemaphoreGive(drive->mutex_);
    vTaskDelete(nullptr);
}

void RobotDrive::RunRoutine(Routine routine) {
    struct Segment {
        Direction direction;
        int duration_ms;
        int pause_ms;
    };

    const Segment lively[] = {
        {Direction::Forward, 1200, 180}, {Direction::Backward, 1200, 180},
        {Direction::Left, 550, 180},     {Direction::Right, 550, 180},
        {Direction::Left, 400, 180},     {Direction::Right, 400, 300},
    };
    const Segment dance[] = {
        {Direction::Forward, 650, 80}, {Direction::Backward, 650, 80},  {Direction::Left, 300, 80},
        {Direction::Right, 300, 80},   {Direction::Left, 450, 80},      {Direction::Right, 450, 80},
        {Direction::Forward, 450, 80}, {Direction::Backward, 450, 250},
    };
    const Segment* segments = routine == Routine::Dance ? dance : lively;
    const size_t segment_count = routine == Routine::Dance ? sizeof(dance) / sizeof(dance[0])
                                                           : sizeof(lively) / sizeof(lively[0]);
    const bool repeat_forever = routine == Routine::Lively;
    const int cycles = 2;

    for (int cycle = 0; (repeat_forever || cycle < cycles) && !routine_cancel_.load(); ++cycle) {
        for (size_t index = 0; index < segment_count && !routine_cancel_.load(); ++index) {
            const auto& segment = segments[index];
            if (!Move(segment.direction, segment.duration_ms, 1)) {
                routine_cancel_.store(true);
                break;
            }
            int elapsed_ms = 0;
            while (elapsed_ms < segment.duration_ms && !routine_cancel_.load()) {
                const int wait_ms = std::min(20, segment.duration_ms - elapsed_ms);
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
                elapsed_ms += wait_ms;
            }
            StopMotion();
            if (segment.pause_ms > 0 && !routine_cancel_.load()) {
                vTaskDelay(pdMS_TO_TICKS(segment.pause_ms));
            }
        }
    }
}

void RobotDrive::Stop() {
    routine_cancel_.store(true);
    StopMotion();
}

void RobotDrive::StopMotion() {
    if (mutex_ == nullptr)
        return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    // Serialize external cancellation with StartRoutine() under the same mutex.
    const bool from_routine = routine_task_ == xTaskGetCurrentTaskHandle();
    if (!from_routine) {
        routine_cancel_.store(true);
    } else if (routine_cancel_.load()) {
        // A cancelled routine cannot stop the manual command that superseded it.
        xSemaphoreGive(mutex_);
        return;
    }
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
    routine_cancel_.store(true);
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
