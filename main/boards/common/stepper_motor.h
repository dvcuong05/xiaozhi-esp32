#ifndef STEPPER_MOTOR_H_
#define STEPPER_MOTOR_H_

#include <driver/gpio.h>
#include <functional>

class StepperMotor {
public:
    StepperMotor(gpio_num_t int1, gpio_num_t int2, gpio_num_t int3, gpio_num_t int4);
    ~StepperMotor();

    void Step(int steps, int delay_ms = 2);
    void SetSpeed(int delay_ms);
    void Stop();

private:
    gpio_num_t int1_;
    gpio_num_t int2_;
    gpio_num_t int3_;
    gpio_num_t int4_;
    int step_delay_ms_;
    int current_step_ = 0;  // Track current step position
    
    void SetStep(int step);
    void InitializeGpios();
};

#endif // STEPPER_MOTOR_H_
