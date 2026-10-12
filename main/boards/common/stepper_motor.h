#ifndef STEPPER_MOTOR_H_
#define STEPPER_MOTOR_H_

#include <driver/gpio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class StepperMotor {
public:
    static constexpr int kDefaultDelayMs = 2;
    static constexpr int kMinDelayMs = 1;
    static constexpr int kMaxDelayMs = 100;
    static constexpr int kMaxSteps = 30000;

    StepperMotor(gpio_num_t int1, gpio_num_t int2, gpio_num_t int3, gpio_num_t int4);
    ~StepperMotor();

    // Start or replace a move. The motor is driven by a worker task, so this
    // call never waits for all steps to complete. Pass 0 for delay_ms to use
    // the configured speed.
    void Step(int steps, int delay_ms = 0);
    void SetSpeed(int delay_ms);
    void Stop();

private:
    gpio_num_t int1_;
    gpio_num_t int2_;
    gpio_num_t int3_;
    gpio_num_t int4_;
    int step_delay_ms_;
    int current_step_ = 0;  // Track current step position

    SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t task_exit_semaphore_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    int remaining_steps_ = 0;
    int direction_ = 1;
    bool stop_requested_ = false;
    bool shutting_down_ = false;

    void SetStep(int step);
    void SetOutputsLow();
    void InitializeGpios();
    static void StepTask(void* arg);
};

#endif  // STEPPER_MOTOR_H_
