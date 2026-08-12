#include "motor.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

static direction_t cached_direction;
static uint8_t cached_duty;
static uint pwm_slice;

void motor_init(void)
{
    gpio_init(PIN_AIN1); gpio_put(PIN_AIN1, false); gpio_set_dir(PIN_AIN1, GPIO_OUT);
    gpio_init(PIN_AIN2); gpio_put(PIN_AIN2, false); gpio_set_dir(PIN_AIN2, GPIO_OUT);
    gpio_init(PIN_STBY); gpio_put(PIN_STBY, false); gpio_set_dir(PIN_STBY, GPIO_OUT);
    gpio_set_function(PIN_PWMA, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(PIN_PWMA);
    pwm_set_wrap(pwm_slice, PWM_WRAP);
    pwm_set_clkdiv(pwm_slice, PWM_CLKDIV);
    pwm_set_chan_level(pwm_slice, PWM_CHAN_A, 0);
    pwm_set_enabled(pwm_slice, true);
    cached_direction = DIR_STOP;
    cached_duty = 0;
}

void motor_enable(void)
{
    gpio_put(PIN_AIN1, false);
    gpio_put(PIN_AIN2, false);
    gpio_put(PIN_STBY, true);
}

void motor_disable(void)
{
    gpio_put(PIN_STBY, false);
    gpio_put(PIN_AIN1, false);
    gpio_put(PIN_AIN2, false);
    pwm_set_chan_level(pwm_slice, PWM_CHAN_A, 0);
    cached_direction = DIR_STOP;
    cached_duty = 0;
}

void motor_drive(direction_t direction, uint8_t duty)
{
    if (direction == DIR_STOP || duty == 0) { motor_brake(); return; }
    if (duty < DUTY_MIN) duty = DUTY_MIN;
    gpio_put(PIN_AIN1, direction == DIR_FWD);
    gpio_put(PIN_AIN2, direction == DIR_REV);
    pwm_set_chan_level(pwm_slice, PWM_CHAN_A, duty);
    cached_direction = direction;
    cached_duty = duty;
}

void motor_brake(void)
{
    gpio_put(PIN_AIN1, true);
    gpio_put(PIN_AIN2, true);
    pwm_set_chan_level(pwm_slice, PWM_CHAN_A, PWM_WRAP);
    cached_direction = DIR_STOP;
    cached_duty = 0;
}

void motor_coast(void)
{
    gpio_put(PIN_AIN1, false);
    gpio_put(PIN_AIN2, false);
    pwm_set_chan_level(pwm_slice, PWM_CHAN_A, PWM_WRAP);
    cached_direction = DIR_STOP;
    cached_duty = 0;
}

direction_t motor_direction(void) { return cached_direction; }
uint8_t motor_duty(void) { return cached_duty; }
