#include "debug.h"
void dbg_init(void) { cfg_reset(); } void dbg_enter(void) {} void dbg_exit(void) {} bool dbg_active(void) { return false; }
void dbg_poll(void) {} void dbg_handle_key(char c) { (void)c; } void dbg_render(void) {} void dbg_render_header(void) {} void dbg_abort(void) {}
void dbg_status_dump(void) {} void dbg_stream_toggle(void) {} void dbg_stream_set_rate(uint16_t hz) { (void)hz; } void dbg_timing_stats(void) {} void dbg_timing_reset(void) {}
void dbg_adc_read_once(void) {} void dbg_adc_monitor_toggle(void) {} void dbg_adc_capture_toggle(void) {} void dbg_band_table(void) {} void dbg_band_margin(void) {}
bool dbg_motor_arm(void) { return false; } void dbg_motor_disarm(void) {} bool dbg_motor_armed(void) { return false; } void dbg_motor_pulse(direction_t dir, uint8_t duty, uint16_t ms) { (void)dir; (void)duty; (void)ms; } void dbg_motor_brake(void) {} void dbg_motor_coast(void) {} void dbg_motor_standby(bool on) { (void)on; } void dbg_motor_find_min(direction_t dir) { (void)dir; }
bool dbg_coupled_confirm(void) { return false; } void dbg_coupled_clear(void) {} bool dbg_coupled(void) { return false; }
void dbg_cal_positions(void) {} void dbg_cal_step_time(void) {} void dbg_cal_travel_time(void) {} void dbg_cal_overshoot(void) {} void dbg_cal_report(void) {}
void dbg_cfg_list(void) {} bool dbg_cfg_set(const char *key, int32_t value) { (void)key; (void)value; return false; } void dbg_cfg_reset(void) {} void dbg_cfg_export(void) {}
void dbg_fault_show(void) {} void dbg_history_dump(void) {} void dbg_counters_show(void) {} void dbg_counters_reset(void) {} void dbg_fault_clear(void) {}
bool dbg_selftest_static(void) { return false; } bool dbg_selftest_motion(void) { return false; }
