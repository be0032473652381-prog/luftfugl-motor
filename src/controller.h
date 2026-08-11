#ifndef LUFTFUGL_CONTROLLER_H
#define LUFTFUGL_CONTROLLER_H
#include "config.h"
void controller_init(void);
move_result_t controller_request(request_kind_t kind, position_t arg);
void controller_tick(void);
sys_state_t controller_state(void);
position_t controller_position(void);
position_t controller_target(void);
#ifdef LUFTFUGL_DEBUG
typedef struct { uint32_t min_us, max_us; uint64_t sum_us; uint32_t count, overruns; } tick_stats_t;
typedef struct { uint32_t ms; position_t pos; uint8_t kind; } hist_entry_t;
typedef struct { uint32_t moves_ok, moves_timeout, faults, limit_rejects, pass_events, tick_overruns; } dbg_counters_t;
typedef struct { event_kind_t kind; uint32_t ms; sys_state_t state; position_t pos, target; uint32_t deadline_ms; } fault_record_t;
typedef struct { uint16_t current_delta; uint32_t window_remaining_ms; bool stall_armed, direction_armed; } motion_check_status_t;
typedef struct { uint32_t tick; uint16_t adc; uint8_t fault; } adc_trace_entry_t;
bool controller_debug_request(const dbg_request_t *req);
move_result_t controller_debug_goto_adc(uint16_t adc);
uint16_t controller_target_adc(void);
uint32_t controller_deadline_ms(void);
direction_t controller_last_direction(void);
void controller_timing_get(tick_stats_t *out);
void controller_timing_reset(void);
uint8_t controller_history_count(void);
bool controller_history_get(uint8_t index, hist_entry_t *out);
void controller_counters_get(dbg_counters_t *out);
void controller_counters_reset(void);
void controller_fault_get(fault_record_t *out);
void controller_motion_checks_get(motion_check_status_t *out);
uint16_t controller_adc_trace_snapshot(adc_trace_entry_t *out, uint16_t capacity);
#endif
#endif
