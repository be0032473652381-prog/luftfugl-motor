#include "motor.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
static direction_t cached_dir; static uint8_t cached_duty; static uint slice;
void motor_init(void){gpio_init(PIN_AIN1);gpio_init(PIN_AIN2);gpio_init(PIN_STBY);gpio_set_dir(PIN_AIN1,1);gpio_set_dir(PIN_AIN2,1);gpio_set_dir(PIN_STBY,1);gpio_put(PIN_AIN1,0);gpio_put(PIN_AIN2,0);gpio_put(PIN_STBY,0);gpio_set_function(PIN_PWMA,GPIO_FUNC_PWM);slice=pwm_gpio_to_slice_num(PIN_PWMA);pwm_set_wrap(slice,PWM_WRAP);pwm_set_clkdiv(slice,PWM_CLKDIV);pwm_set_chan_level(slice,PWM_CHAN_A,0);pwm_set_enabled(slice,true);cached_dir=DIR_STOP;cached_duty=0;}
void motor_enable(void){gpio_put(PIN_AIN1,0);gpio_put(PIN_AIN2,0);gpio_put(PIN_STBY,1);}
void motor_disable(void){gpio_put(PIN_STBY,0);cached_dir=DIR_STOP;cached_duty=0;}
void motor_drive(direction_t d,uint8_t duty){if(!d||!duty){motor_brake();return;}if(duty<DUTY_MIN)duty=DUTY_MIN;gpio_put(PIN_AIN1,d==DIR_FWD);gpio_put(PIN_AIN2,d==DIR_REV);pwm_set_chan_level(slice,PWM_CHAN_A,duty);cached_dir=d;cached_duty=duty;}
void motor_brake(void){gpio_put(PIN_AIN1,1);gpio_put(PIN_AIN2,1);pwm_set_chan_level(slice,PWM_CHAN_A,PWM_WRAP);cached_dir=DIR_STOP;cached_duty=0;}
void motor_coast(void){gpio_put(PIN_AIN1,0);gpio_put(PIN_AIN2,0);pwm_set_chan_level(slice,PWM_CHAN_A,PWM_WRAP);cached_dir=DIR_STOP;cached_duty=0;}
direction_t motor_direction(void){return cached_dir;} uint8_t motor_duty(void){return cached_duty;}
