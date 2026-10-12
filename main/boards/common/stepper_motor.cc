#include "stepper_motor.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <algorithm>
#include <limits>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "StepperMotor"

// Step sequence for 28BYJ-48 stepper motor (half-step mode)
const int step_sequence[8][4] = {{1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
                                 {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}};

StepperMotor::StepperMotor(gpio_num_t int1, gpio_num_t int2, gpio_num_t int3, gpio_num_t int4)
    : int1_(int1), int2_(int2), int3_(int3), int4_(int4), step_delay_ms_(kDefaultDelayMs) {
    InitializeGpios();

    mutex_ = xSemaphoreCreateMutex();
    task_exit_semaphore_ = xSemaphoreCreateBinary();
    if (mutex_ == nullptr || task_exit_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "Khong the cap phat tai nguyen cho task dong co");
        if (task_exit_semaphore_ != nullptr) {
            vSemaphoreDelete(task_exit_semaphore_);
            task_exit_semaphore_ = nullptr;
        }
        if (mutex_ != nullptr) {
            vSemaphoreDelete(mutex_);
            mutex_ = nullptr;
        }
        return;
    }

    BaseType_t result = xTaskCreate(StepTask, "StepperMotor", 3072, this, 5, &task_handle_);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Khong the tao task dieu khien dong co");
        vSemaphoreDelete(task_exit_semaphore_);
        vSemaphoreDelete(mutex_);
        task_exit_semaphore_ = nullptr;
        mutex_ = nullptr;
    }
}

StepperMotor::~StepperMotor() {
    Stop();

    if (task_handle_ != nullptr && mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        shutting_down_ = true;
        remaining_steps_ = 0;
        stop_requested_ = true;
        xSemaphoreGive(mutex_);
        xTaskNotifyGive(task_handle_);

        if (task_exit_semaphore_ != nullptr &&
            xSemaphoreTake(task_exit_semaphore_, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Task dong co khong ket thuc kip thoi, buoc phai huy task");
            vTaskDelete(task_handle_);
        }
        task_handle_ = nullptr;
    }

    if (task_exit_semaphore_ != nullptr) {
        vSemaphoreDelete(task_exit_semaphore_);
        task_exit_semaphore_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void StepperMotor::InitializeGpios() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << int1_) | (1ULL << int2_) | (1ULL << int3_) | (1ULL << int4_);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initialize all pins to low
    gpio_set_level(int1_, 0);
    gpio_set_level(int2_, 0);
    gpio_set_level(int3_, 0);
    gpio_set_level(int4_, 0);
}

void StepperMotor::SetStep(int step) {
    int sequence_index = step % 8;
    if (sequence_index < 0) {
        sequence_index += 8;
    }

    gpio_set_level(int1_, step_sequence[sequence_index][0]);
    gpio_set_level(int2_, step_sequence[sequence_index][1]);
    gpio_set_level(int3_, step_sequence[sequence_index][2]);
    gpio_set_level(int4_, step_sequence[sequence_index][3]);
}

void StepperMotor::SetOutputsLow() {
    gpio_set_level(int1_, 0);
    gpio_set_level(int2_, 0);
    gpio_set_level(int3_, 0);
    gpio_set_level(int4_, 0);
}

void StepperMotor::Step(int steps, int delay_ms) {
    if (steps == 0) {
        Stop();
        return;
    }

    if (steps == std::numeric_limits<int>::min()) {
        steps = -kMaxSteps;
    } else {
        steps = std::clamp(steps, -kMaxSteps, kMaxSteps);
    }

    if (mutex_ == nullptr || task_handle_ == nullptr) {
        ESP_LOGE(TAG, "Task dong co chua san sang");
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (delay_ms > 0) {
        step_delay_ms_ = std::clamp(delay_ms, kMinDelayMs, kMaxDelayMs);
    }
    remaining_steps_ = steps >= 0 ? steps : -steps;
    direction_ = steps >= 0 ? 1 : -1;
    stop_requested_ = false;
    int effective_delay_ms = step_delay_ms_;
    xSemaphoreGive(mutex_);

    ESP_LOGI(TAG, "Bat dau di chuyen khong chan: steps=%d, delay_ms=%d", steps, effective_delay_ms);
    xTaskNotifyGive(task_handle_);
}

void StepperMotor::SetSpeed(int delay_ms) {
    if (mutex_ == nullptr) {
        return;
    }

    delay_ms = std::clamp(delay_ms, kMinDelayMs, kMaxDelayMs);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    step_delay_ms_ = delay_ms;
    xSemaphoreGive(mutex_);

    if (task_handle_ != nullptr) {
        xTaskNotifyGive(task_handle_);
    }
}

void StepperMotor::Stop() {
    if (mutex_ == nullptr) {
        SetOutputsLow();
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    remaining_steps_ = 0;
    stop_requested_ = true;
    SetOutputsLow();
    xSemaphoreGive(mutex_);

    if (task_handle_ != nullptr) {
        xTaskNotifyGive(task_handle_);
    }
}

void StepperMotor::StepTask(void* arg) {
    auto* motor = static_cast<StepperMotor*>(arg);

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (true) {
            xSemaphoreTake(motor->mutex_, portMAX_DELAY);
            if (motor->shutting_down_) {
                motor->SetOutputsLow();
                xSemaphoreGive(motor->mutex_);
                xSemaphoreGive(motor->task_exit_semaphore_);
                vTaskDelete(nullptr);
            }

            if (motor->remaining_steps_ <= 0 || motor->stop_requested_) {
                motor->remaining_steps_ = 0;
                motor->stop_requested_ = false;
                motor->SetOutputsLow();
                xSemaphoreGive(motor->mutex_);
                break;
            }

            motor->current_step_ += motor->direction_;
            motor->SetStep(motor->current_step_);
            --motor->remaining_steps_;
            TickType_t delay_ticks = pdMS_TO_TICKS(motor->step_delay_ms_);
            if (delay_ticks == 0) {
                delay_ticks = 1;
            }
            xSemaphoreGive(motor->mutex_);

            // A notification wakes the worker immediately when Stop(), a new
            // move, or SetSpeed() changes the command during this interval.
            ulTaskNotifyTake(pdTRUE, delay_ticks);
        }
    }
}
