#include "config.h"

volatile cfg_t cfg;

void cfg_reset(void)
{
    cfg.duty_normal = DUTY_NORMAL;
    cfg.duty_approach = DUTY_APPROACH;
    cfg.duty_creep = DUTY_CREEP;
    cfg.duty_min = DUTY_MIN;
    cfg.band_p1_max = BAND_P1_MAX;
    cfg.band_p2_max = BAND_P2_MAX;
    cfg.band_p3_max = BAND_P3_MAX;
    cfg.band_p4_max = BAND_P4_MAX;
    cfg.band_p5_max = BAND_P5_MAX;
    cfg.debounce_ms = DEBOUNCE_MS;
    cfg.brake_hold_ms = BRAKE_HOLD_MS;
    cfg.timeout_step_ms = TIMEOUT_STEP_MS;
    cfg.timeout_home_ms = TIMEOUT_HOME_MS;
    cfg.timeout_recover_ms = TIMEOUT_RECOVER_MS;
}
