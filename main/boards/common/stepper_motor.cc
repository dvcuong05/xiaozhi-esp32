#include "stepper_motor.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "StepperMotor"

// Step sequence for 28BYJ-48 stepper motor (half-step mode)
const int step_sequence[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1}
};

StepperMotor::StepperMotor(gpio_num_t int1, gpio_num_t int2, gpio_num_t int3, gpio_num_t int4)
    : int1_(int1), int2_(int2), int3_(int3), int4_(int4), step_delay_ms_(2) {
    InitializeGpios();
}

StepperMotor::~StepperMotor() {
    Stop();
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

void StepperMotor::Step(int steps, int delay_ms) {
    ESP_LOGI(TAG, "Step() duoc goi: steps=%d, delay_ms=%d", steps, delay_ms);
    int direction = (steps >= 0) ? 1 : -1;
    int abs_steps = (steps >= 0) ? steps : -steps;
    
    ESP_LOGI(TAG, "Bat dau di %d buoc theo huong %d", abs_steps, direction);
    for (int i = 0; i < abs_steps; i++) {
        current_step_ += direction;
        SetStep(current_step_);
        // Ensure minimum delay of 1 tick
        TickType_t delay_ticks = (delay_ms / portTICK_PERIOD_MS) >= 1 ? (delay_ms / portTICK_PERIOD_MS) : 1;
        vTaskDelay(delay_ticks);
    }
    
    Stop();
    ESP_LOGI(TAG, "Step() hoan tat");
}

void StepperMotor::SetSpeed(int delay_ms) {
    step_delay_ms_ = delay_ms;
}

void StepperMotor::Stop() {
    gpio_set_level(int1_, 0);
    gpio_set_level(int2_, 0);
    gpio_set_level(int3_, 0);
    gpio_set_level(int4_, 0);
}
