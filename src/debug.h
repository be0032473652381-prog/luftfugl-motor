#ifndef LUFTFUGL_DEBUG_H
#define LUFTFUGL_DEBUG_H
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
void dbg_init(void); void dbg_enter(void); void dbg_exit(void); bool dbg_active(void);
void dbg_poll(void); void dbg_handle_key(char c); void dbg_render(void); void dbg_render_header(void); void dbg_abort(void);
void dbg_status_dump(void); void dbg_stream_toggle(void); void dbg_stream_set_rate(uint16_t hz); void dbg_timing_stats(void); void dbg_timing_reset(void);
void dbg_adc_read_once(void); void dbg_adc_monitor_toggle(void); void dbg_adc_capture_toggle(void); void dbg_position_table(void); void dbg_position_error(void);
bool dbg_motor_arm(void); void dbg_motor_disarm(void); bool dbg_motor_armed(void); void dbg_motor_pulse(direction_t dir, uint8_t duty, uint16_t ms); void dbg_motor_brake(void); void dbg_motor_coast(void); void dbg_motor_standby(bool on); void dbg_motor_find_min(direction_t dir);
bool dbg_coupled_confirm(void); void dbg_coupled_clear(void); bool dbg_coupled(void);
void dbg_cal_positions(void); void dbg_cal_step_time(void); void dbg_cal_travel_time(void); void dbg_cal_overshoot(void); void dbg_cal_report(void);
void dbg_cfg_list(void); bool dbg_cfg_set(const char *key, int32_t value); void dbg_cfg_reset(void); void dbg_cfg_export(void);
void dbg_fault_show(void); void dbg_history_dump(void); void dbg_counters_show(void); void dbg_counters_reset(void); void dbg_fault_clear(void);
bool dbg_selftest_static(void); bool dbg_selftest_motion(void);
void dbg_bench_pins(void); void dbg_bench_gpio_walk(void); void dbg_bench_pwm_report(void); void dbg_bench_tick_health(void); void dbg_bench_reset_reason(void); void dbg_bench_protocol_list(void); void dbg_bench_echo_toggle(void); void dbg_bench_motion_checks(void);
void dbg_sim_toggle(void); void dbg_sim_set_value(uint16_t adc); void dbg_sim_set_position(position_t pos); void dbg_sim_travel(position_t from, position_t to, uint16_t ms_per_position); void dbg_sim_park(void); void dbg_sim_overtravel(position_t limit); void dbg_sim_sweep(void);
void dbg_what_can_run(void);
#endif
