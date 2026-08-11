#include "config.h"

volatile cfg_t cfg;

void cfg_reset(void)
{
    cfg.duty_normal = DUTY_NORMAL;
    cfg.duty_approach = DUTY_APPROACH;
    cfg.duty_creep = DUTY_CREEP;
    cfg.duty_min = DUTY_MIN;
    cfg.pos_1_adc = POS_1_ADC;
    cfg.pos_2_adc = POS_2_ADC;
    cfg.pos_3_adc = POS_3_ADC;
    cfg.pos_4_adc = POS_4_ADC;
    cfg.pos_5_adc = POS_5_ADC;
    cfg.pos_window = POS_WINDOW;
    cfg.approach_counts = APPROACH_COUNTS;
    cfg.adc_safe_min = ADC_SAFE_MIN;
    cfg.adc_safe_max = ADC_SAFE_MAX;
    cfg.stall_delta = STALL_DELTA;
    cfg.stall_window_ms = STALL_WINDOW_MS;
    cfg.reverse_delta = REVERSE_DELTA;
    cfg.debounce_ms = DEBOUNCE_MS;
    cfg.brake_hold_ms = BRAKE_HOLD_MS;
    cfg.timeout_step_ms = TIMEOUT_STEP_MS;
    cfg.timeout_home_ms = TIMEOUT_HOME_MS;
}
