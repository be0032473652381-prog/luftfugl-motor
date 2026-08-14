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
    cfg.debounce_ms = DEBOUNCE_MS;
    cfg.brake_hold_ms = BRAKE_HOLD_MS;
    cfg.low_endstop_adc = LOW_ENDSTOP_ADC;
    cfg.high_endstop_adc = HIGH_ENDSTOP_ADC;
}
