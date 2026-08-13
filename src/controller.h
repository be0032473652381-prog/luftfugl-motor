#ifndef LUFTFUGL_CONTROLLER_H
#define LUFTFUGL_CONTROLLER_H
#include "config.h"
void controller_init(void);
move_result_t controller_request(request_kind_t kind, position_t arg);
jog_result_t controller_request_jog(int16_t delta, uint16_t *from_adc);
move_result_t controller_request_setpos(position_t position, uint16_t adc);
move_result_t controller_request_reset_positions(void);
void controller_tick(void);
sys_state_t controller_state(void);
position_t controller_position(void);
position_t controller_target(void);
#ifdef LUFTFUGL_MONITOR
typedef struct {
  uint32_t min_us, max_us;
  uint64_t sum_us;
  uint32_t count, overruns;
} tick_stats_t;
typedef struct {
  uint32_t moves_ok, pass_events, tick_overruns;
} dbg_counters_t;
bool controller_debug_request(const dbg_request_t *req);
move_result_t controller_debug_goto_adc(uint16_t adc);
uint16_t controller_target_adc(void);
direction_t controller_last_direction(void);
void controller_timing_get(tick_stats_t *out);
void controller_timing_reset(void);
void controller_counters_get(dbg_counters_t *out);
void controller_counters_reset(void);
#endif
#endif
