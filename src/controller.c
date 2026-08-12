#include "controller.h"
#include "encoder.h"
#include "motor.h"
#include "console.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#ifdef LUFTFUGL_DEBUG
#include <string.h>
#define TICK_RETURN() goto tick_done
#else
#define TICK_RETURN() return
#endif
#ifdef LUFTFUGL_DEBUG
#include "debug.h"
#endif

static volatile sys_state_t state;
static volatile position_t position, target;
static direction_t last_direction;
static volatile request_kind_t mailbox;
static volatile position_t mailbox_arg;
static uint32_t deadline_ms, brake_until_ms;
static uint16_t stall_reference, best_error_magnitude;
static uint16_t motion_start_adc, motion_target_adc, previous_motion_adc;
static uint8_t passed_mask;
static bool endpoint_braking;
static uint32_t stall_check_ms, direction_check_ms;
#ifdef LUFTFUGL_DEBUG
static bool adc_move;
static uint16_t adc_target;
static uint32_t adc_arrival_since;
static volatile bool debug_pending;
static volatile dbg_request_t debug_mailbox;
static volatile tick_stats_t tick_stats;
static volatile hist_entry_t history[DEBUG_HISTORY_DEPTH];
static volatile uint8_t history_head, history_used;
static volatile dbg_counters_t counters;
static volatile fault_record_t last_fault;
static volatile adc_trace_entry_t adc_trace[DEBUG_ADC_TRACE_DEPTH];
static volatile uint16_t adc_trace_head, adc_trace_count;
static uint32_t adc_trace_tick;
static uint16_t adc_trace_current;
static volatile bool adc_trace_frozen;
static uint16_t adc_trace_dump_first, adc_trace_dump_count;
#endif

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
#ifdef LUFTFUGL_DEBUG
static void hist_push(uint32_t ms, position_t pos, uint8_t kind)
{ history[history_head] = (hist_entry_t){ms, pos, kind}; history_head = (uint8_t)((history_head + 1u) % DEBUG_HISTORY_DEPTH); if (history_used < DEBUG_HISTORY_DEPTH) ++history_used; }
static void timing_finish(uint32_t start)
{ uint32_t elapsed = time_us_32() - start; if (!tick_stats.count || elapsed < tick_stats.min_us) tick_stats.min_us = elapsed; if (elapsed > tick_stats.max_us) tick_stats.max_us = elapsed; tick_stats.sum_us += elapsed; ++tick_stats.count; if (elapsed > TICK_PERIOD_US) { ++tick_stats.overruns; ++counters.tick_overruns; } }
#endif
static bool reached(uint32_t now, uint32_t deadline) { return deadline && (int32_t)(now - deadline) >= 0; }
static bool valid(position_t p) { return p >= POS_MIN && p <= POS_MAX; }
static uint16_t error_magnitude(int16_t error)
{
    return error < 0 ? (uint16_t)-error : (uint16_t)error;
}

static uint8_t speed_for_error(int16_t error, position_t tgt)
{
    uint16_t magnitude = error_magnitude(error);
    if (magnitude <= CFG_APPROACH_COUNTS)
        return tgt == POS_MIN || tgt == POS_MAX ? CFG_DUTY_CREEP : CFG_DUTY_APPROACH;
    return CFG_DUTY_NORMAL;
}

static void arrive(position_t p, uint32_t now);

static void enter_fault(event_kind_t event)
{
    endpoint_braking = false;
#ifdef LUFTFUGL_DEBUG
    if (adc_trace_current < DEBUG_ADC_TRACE_DEPTH) adc_trace[adc_trace_current].fault = (uint8_t)event + 1u;
    adc_trace_frozen = true;
    encoder_sim_enable(false); motor_set_inhibit(false);
#endif
    motor_brake();
    motor_disable();
#ifdef LUFTFUGL_DEBUG
    uint32_t fault_deadline = deadline_ms;
#endif
    deadline_ms = 0;
    state = ST_FAULT;
#ifdef LUFTFUGL_DEBUG
    ++counters.faults;
    last_fault = (fault_record_t){event, now_ms(), state, position, target, fault_deadline};
#endif
    console_push_event(event, 0);
}

static void begin_home(uint32_t now)
{
#ifdef LUFTFUGL_DEBUG
    adc_trace_frozen = false;
#endif
    uint16_t current = encoder_average();
    uint16_t target_adc = encoder_nominal(POS_MIN);
    int16_t error = (int16_t)target_adc - (int16_t)current;
    motor_brake();
    if (error_magnitude(error) <= CFG_POS_WINDOW) {
        target = POS_MIN;
        arrive(POS_MIN, now);
        return;
    }
    motor_enable();
    target = POS_MIN;
    motion_start_adc = current;
    motion_target_adc = target_adc;
    previous_motion_adc = current;
    passed_mask = 0u;
    endpoint_braking = false;
    last_direction = error > 0 ? DIR_FWD : DIR_REV;
    deadline_ms = now + CFG_TIMEOUT_HOME_MS;
    state = ST_HOMING;
    stall_reference = current;
    best_error_magnitude = error_magnitude(error);
    stall_check_ms = now + CFG_STALL_WINDOW_MS;
    direction_check_ms = now + CFG_STALL_WINDOW_MS;
    motor_drive(last_direction, speed_for_error(error, target));
    console_push_event(EV_HOMING, 0);
}

static void begin_move(position_t tgt, uint32_t now)
{
#ifdef LUFTFUGL_DEBUG
    adc_trace_frozen = false;
#endif
    uint16_t current = encoder_average();
    uint16_t target_adc = encoder_nominal(tgt);
    int16_t error = (int16_t)target_adc - (int16_t)current;
    uint8_t steps = 1u;
    for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
        uint16_t nominal = encoder_nominal(p);
        if ((error > 0 && nominal > current && nominal < target_adc) ||
            (error < 0 && nominal < current && nominal > target_adc)) ++steps;
    }
    target = tgt;
    motion_start_adc = current;
    motion_target_adc = target_adc;
    previous_motion_adc = current;
    passed_mask = 0u;
    endpoint_braking = false;
    last_direction = error > 0 ? DIR_FWD : DIR_REV;
    deadline_ms = now + (uint32_t)steps * CFG_TIMEOUT_STEP_MS;
    state = error_magnitude(error) <= CFG_APPROACH_COUNTS ? ST_APPROACH : ST_MOVING;
    stall_reference = current;
    best_error_magnitude = error_magnitude(error);
    stall_check_ms = now + CFG_STALL_WINDOW_MS;
    direction_check_ms = now + CFG_STALL_WINDOW_MS;
    motor_enable();
    motor_drive(last_direction, speed_for_error(error, tgt));
}

static void arrive(position_t p, uint32_t now)
{
#ifdef LUFTFUGL_DEBUG
    if (state == ST_MOVING || state == ST_APPROACH) ++counters.moves_ok;
    hist_push(now, p, 1);
#endif
    motor_brake();
    endpoint_braking = false;
    position = p;
    deadline_ms = 0;
    brake_until_ms = now + CFG_BRAKE_HOLD_MS;
    state = ST_IDLE;
    console_push_event(EV_ARRIVE, p);
}

void controller_init(void)
{
    state = ST_BOOT;
    position = POS_UNKNOWN;
    target = POS_UNKNOWN;
    last_direction = DIR_REV;
    mailbox = REQ_NONE;
    deadline_ms = 0;
    brake_until_ms = 0;
    stall_reference = best_error_magnitude = 0u;
    motion_start_adc = motion_target_adc = previous_motion_adc = 0u;
    passed_mask = 0u;
    endpoint_braking = false;
    stall_check_ms = direction_check_ms = 0u;
#ifdef LUFTFUGL_DEBUG
    adc_move = false; adc_target = 0u; adc_arrival_since = 0u;
    debug_pending = false; memset((void *)&tick_stats, 0, sizeof tick_stats); memset((void *)history, 0, sizeof history); history_head = history_used = 0; memset((void *)&counters, 0, sizeof counters); memset((void *)&last_fault, 0, sizeof last_fault);
    memset((void *)adc_trace, 0, sizeof adc_trace); adc_trace_head = adc_trace_count = 0u; adc_trace_tick = 0u; adc_trace_current = 0u; adc_trace_frozen = false; adc_trace_dump_first = adc_trace_dump_count = 0u;
#endif
}

move_result_t controller_request(request_kind_t kind, position_t arg)
{
    uint32_t now = now_ms();
    if (kind == REQ_STOP || kind == REQ_HOME) {
        mailbox_arg = arg;
        mailbox = kind;
        return MOVE_OK;
    }
    if (state == ST_FAULT) return MOVE_FAULT;
    if (arg < POS_MIN || arg > POS_MAX) return MOVE_INVALID;
    if (position == POS_UNKNOWN || !encoder_in_safe_range()) return MOVE_POS_UNKNOWN;
    if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING || reached(now, brake_until_ms)) return MOVE_BUSY;
    if (arg == position) return MOVE_ALREADY;
    int16_t error = encoder_error_to(arg);
    if ((encoder_average() <= encoder_nominal(POS_MIN) + CFG_POS_WINDOW && error < 0) ||
        (encoder_average() >= encoder_nominal(POS_MAX) - CFG_POS_WINDOW && error > 0)) return MOVE_ENDSTOP;
    mailbox_arg = arg;
    mailbox = kind;
    return MOVE_OK;
}

#ifdef LUFTFUGL_DEBUG
move_result_t controller_debug_goto_adc(uint16_t adc)
{
    uint32_t now = now_ms();
    if (state == ST_FAULT) return MOVE_FAULT;
    if (!encoder_in_safe_range()) return MOVE_POS_UNKNOWN;
    if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING || state == ST_DEBUG || reached(now, brake_until_ms)) return MOVE_BUSY;
    if (adc < CFG_ADC_SAFE_MIN || adc > CFG_ADC_SAFE_MAX) return MOVE_ENDSTOP;
    uint16_t current = encoder_average();
    uint16_t delta = current > adc ? current - adc : adc - current;
    if (delta <= CFG_POS_WINDOW) return MOVE_ALREADY;
    dbg_request_t req = {.op = DBG_OP_GOTO_ADC, .adc = adc};
    return controller_debug_request(&req) ? MOVE_OK : MOVE_BUSY;
}

bool controller_debug_request(const dbg_request_t *req)
{
    if (debug_pending && req->op != DBG_OP_EXIT && !(req->op == DBG_OP_SIM_ENABLE && !req->flag)) return false;
    if (req->op != DBG_OP_EXIT && req->op != DBG_OP_FAULT_CLEAR && req->op != DBG_OP_SIM_ENABLE && req->op != DBG_OP_GOTO_ADC &&
        !(req->op == DBG_OP_ENTER && encoder_sim_active()) && !dbg_motor_armed()) return false;
    debug_mailbox = *req;
    debug_pending = true;
    return true;
}
#endif

void controller_tick(void)
{
#ifdef LUFTFUGL_DEBUG
    uint32_t tick_start = time_us_32();
#endif
    uint32_t now = now_ms();
    position_t changed;
    watchdog_update();
#ifdef LUFTFUGL_DEBUG
    if (!adc_trace_frozen) {
        if (adc_trace_head >= DEBUG_ADC_TRACE_DEPTH) adc_trace_head = 0u;
        adc_trace_current = adc_trace_head;
        adc_trace[adc_trace_current].tick = ++adc_trace_tick;
        adc_trace[adc_trace_current].adc = encoder_average();
        adc_trace[adc_trace_current].fault = 0u;
        adc_trace_head = (uint16_t)((adc_trace_head + 1u) % DEBUG_ADC_TRACE_DEPTH);
        if (adc_trace_count < DEBUG_ADC_TRACE_DEPTH) ++adc_trace_count;
    }
    if (debug_pending) {
        dbg_request_t req = debug_mailbox;
        debug_pending = false;
        switch (req.op) {
        case DBG_OP_ENTER: if (state == ST_IDLE) { state = ST_DEBUG; motor_brake(); } break;
        case DBG_OP_EXIT:
            encoder_sim_enable(false);
            motor_set_inhibit(false);
            deadline_ms = 0;
            if (state == ST_FAULT) { motor_brake(); motor_disable(); }
            else { motor_enable(); motor_brake(); state = ST_IDLE; }
            break;
        case DBG_OP_DRIVE: if (state == ST_DEBUG) { motor_drive(req.dir, req.duty); deadline_ms = now + req.ms; } break;
        case DBG_OP_BRAKE: motor_brake(); deadline_ms = 0; break;
        case DBG_OP_COAST: if (state == ST_DEBUG) motor_coast(); break;
        case DBG_OP_STANDBY: if (state == ST_DEBUG) { if (req.flag) motor_enable(); else motor_disable(); } break;
        case DBG_OP_FAULT_CLEAR: if (state == ST_FAULT) { state = ST_IDLE; position = POS_UNKNOWN; last_direction = DIR_REV; motor_enable(); motor_brake(); } break;
        case DBG_OP_SIM_ENABLE:
            if (req.flag) {
                if (state != ST_FAULT) state = ST_IDLE;
                deadline_ms = 0;
                motor_set_inhibit(true);
                encoder_sim_set(DEBUG_SIM_DEFAULT_ADC);
                encoder_sim_enable(true);
            } else {
                encoder_sim_enable(false);
                motor_set_inhibit(false);
                if (state == ST_FAULT) { motor_brake(); motor_disable(); }
                else { motor_enable(); motor_brake(); }
            }
            break;
        case DBG_OP_GPIO_SET:
            if (req.duty == DEBUG_GPIO_OP_AIN1) { motor_set_inhibit(true); motor_drive(DIR_FWD, PWM_WRAP); }
            else if (req.duty == DEBUG_GPIO_OP_AIN2) { motor_set_inhibit(true); motor_drive(DIR_REV, PWM_WRAP); }
            else if (req.duty == DEBUG_GPIO_OP_STBY) { motor_set_inhibit(false); motor_disable(); motor_enable(); }
            else { motor_set_inhibit(false); motor_disable(); }
            break;
        case DBG_OP_GOTO_ADC: {
            uint16_t current = encoder_average();
            int16_t error = (int16_t)req.adc - (int16_t)current;
            adc_move = true; adc_target = req.adc; adc_arrival_since = 0u;
            target = POS_BETWEEN;
            motion_start_adc = current; motion_target_adc = req.adc; endpoint_braking = false;
            previous_motion_adc = current; passed_mask = 0u; adc_trace_frozen = false;
            last_direction = error > 0 ? DIR_FWD : DIR_REV;
            deadline_ms = now + CFG_TIMEOUT_HOME_MS;
            state = error_magnitude(error) <= CFG_APPROACH_COUNTS ? ST_APPROACH : ST_MOVING;
            stall_reference = current; best_error_magnitude = error_magnitude(error);
            stall_check_ms = now + CFG_STALL_WINDOW_MS;
            direction_check_ms = now + CFG_STALL_WINDOW_MS;
            motor_enable(); motor_drive(last_direction, speed_for_error(error, POS_BETWEEN));
            break;
        }
        default: break;
        }
    }
#endif

    request_kind_t request = mailbox;
    if (request != REQ_NONE) {
        position_t arg = mailbox_arg;
        mailbox = REQ_NONE;
#ifdef LUFTFUGL_DEBUG
        adc_move = false; adc_arrival_since = 0u;
#endif
        if (request == REQ_STOP) {
            motor_brake(); endpoint_braking = false; deadline_ms = 0; target = POS_UNKNOWN;
            if (state != ST_FAULT) state = ST_IDLE;
            if (position == POS_UNKNOWN || position == POS_BETWEEN) console_push_event(EV_STOPPED_UNKNOWN, 0);
        } else if (request == REQ_HOME) {
            if (encoder_in_safe_range()) begin_home(now);
            else enter_fault(EV_FAULT_OVERTRAVEL);
        } else if (request == REQ_MOVE) {
            begin_move(arg, now);
        }
    }

    if (encoder_take_change(&changed)) {
        position = changed;
    }

    if (state != ST_FAULT
#ifdef LUFTFUGL_DEBUG
        && state != ST_DEBUG
#endif
        && !encoder_in_safe_range()) {
        enter_fault(EV_FAULT_OVERTRAVEL);
        TICK_RETURN();
    }

    if (state == ST_BOOT) {
        position = encoder_confirmed();
        motor_enable();
        motor_brake();
        state = ST_IDLE;
        if (valid(position)) {
            console_push_event(EV_ARRIVE, position);
        } else {
            position = POS_BETWEEN;
            console_push_event(EV_STOPPED_UNKNOWN, 0);
        }
        TICK_RETURN();
    }

    if (reached(now, deadline_ms)) {
#ifdef LUFTFUGL_DEBUG
        if (state == ST_DEBUG) { motor_brake(); deadline_ms = 0; TICK_RETURN(); }
#endif
        if (state == ST_MOVING || state == ST_APPROACH) {
            motor_brake();
#ifdef LUFTFUGL_DEBUG
            ++counters.moves_timeout;
#endif
            console_push_event(EV_TIMEOUT, 0); begin_home(now);
        } else if (state == ST_HOMING) enter_fault(EV_FAULT_HOME);
        TICK_RETURN();
    }

    if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING) {
        uint16_t current = encoder_average();
        int16_t error = (int16_t)motion_target_adc - (int16_t)current;
        uint16_t magnitude = error_magnitude(error);
        direction_t direction = error > 0 ? DIR_FWD : DIR_REV;
        bool endpoint_target = target == POS_MIN || target == POS_MAX;

        if (endpoint_target && magnitude <= CFG_POS_WINDOW) endpoint_braking = true;

        /* Continuous position, not the last station window, prevents outward limit drive. */
        if (endpoint_braking ||
            (current <= encoder_nominal(POS_MIN) + CFG_POS_WINDOW && direction == DIR_REV) ||
            (current >= encoder_nominal(POS_MAX) - CFG_POS_WINDOW && direction == DIR_FWD)) {
            motor_brake();
        } else if (magnitude > CFG_POS_WINDOW) {
            motor_drive(direction, speed_for_error(error,
#ifdef LUFTFUGL_DEBUG
                adc_move ? POS_BETWEEN :
#endif
                target));
        }

        if (!endpoint_braking && magnitude < best_error_magnitude) best_error_magnitude = magnitude;
        else if (!endpoint_braking && reached(now, direction_check_ms) &&
                 magnitude > best_error_magnitude &&
                 magnitude - best_error_magnitude > CFG_REVERSE_DELTA) {
            enter_fault(EV_FAULT_DIRECTION);
            TICK_RETURN();
        }

        if (!endpoint_braking && motor_duty() > CFG_DUTY_MIN && reached(now, stall_check_ms)) {
            uint16_t delta = current > stall_reference ? current - stall_reference : stall_reference - current;
            if (delta < CFG_STALL_DELTA) {
                enter_fault(EV_FAULT_STALL);
                TICK_RETURN();
            }
            stall_reference = current;
            stall_check_ms = now + CFG_STALL_WINDOW_MS;
        }


#ifdef LUFTFUGL_DEBUG
        if (adc_move) {
            if (magnitude <= CFG_POS_WINDOW) {
                motor_brake();
                if (!adc_arrival_since) adc_arrival_since = now;
                else if ((uint32_t)(now - adc_arrival_since) >= CFG_DEBOUNCE_MS) {
                    deadline_ms = 0; brake_until_ms = now + CFG_BRAKE_HOLD_MS;
                    state = ST_IDLE; position = encoder_instant(); adc_move = false;
                    TICK_RETURN();
                }
            } else adc_arrival_since = 0u;
        } else {
#endif
        if (encoder_confirmed() == target) {
            arrive(target, now);
            TICK_RETURN();
        }
#ifdef LUFTFUGL_DEBUG
        }
#endif
        if (state != ST_HOMING) state = magnitude <= CFG_APPROACH_COUNTS ? ST_APPROACH : ST_MOVING;
    }

    switch (state) {
    case ST_MOVING: {
        uint16_t current = encoder_average();
        if (motion_start_adc < motion_target_adc) {
            for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
                uint16_t nominal = encoder_nominal(p);
                uint8_t bit = (uint8_t)(1u << (p - POS_MIN));
                if (!(passed_mask & bit) && nominal > motion_start_adc && nominal < motion_target_adc &&
                    previous_motion_adc < nominal && current >= nominal) {
                    passed_mask |= bit;
                    console_push_event(EV_PASS, p);
#ifdef LUFTFUGL_DEBUG
                    ++counters.pass_events; hist_push(now, p, 0);
#endif
                }
            }
        } else {
            for (position_t p = POS_MAX; p >= POS_MIN; --p) {
                uint16_t nominal = encoder_nominal(p);
                uint8_t bit = (uint8_t)(1u << (p - POS_MIN));
                if (!(passed_mask & bit) && nominal < motion_start_adc && nominal > motion_target_adc &&
                    previous_motion_adc > nominal && current <= nominal) {
                    passed_mask |= bit;
                    console_push_event(EV_PASS, p);
#ifdef LUFTFUGL_DEBUG
                    ++counters.pass_events; hist_push(now, p, 0);
#endif
                }
                if (p == POS_MIN) break;
            }
        }
        previous_motion_adc = current;
        break;
    }
    case ST_APPROACH:
    case ST_HOMING:
        break;
    case ST_IDLE:
        motor_brake();
        break;
    case ST_FAULT:
        break;
#ifdef LUFTFUGL_DEBUG
    case ST_DEBUG:
        break;
#endif
    default:
        break;
    }
#ifdef LUFTFUGL_DEBUG
tick_done:
    timing_finish(tick_start);
#endif
}

sys_state_t controller_state(void) { return state; }
position_t controller_position(void) { return position; }
#ifdef LUFTFUGL_DEBUG
uint16_t controller_target_adc(void) { return adc_move ? adc_target : (valid(target) ? encoder_nominal(target) : 0u); }
uint32_t controller_deadline_ms(void) { return deadline_ms; }
direction_t controller_last_direction(void) { return last_direction; }
void controller_timing_get(tick_stats_t *out) { *out = tick_stats; }
void controller_timing_reset(void) { memset((void *)&tick_stats, 0, sizeof tick_stats); }
uint8_t controller_history_count(void) { return history_used; }
bool controller_history_get(uint8_t index, hist_entry_t *out) { if (index >= history_used) return false; uint8_t first = (uint8_t)((history_head + DEBUG_HISTORY_DEPTH - history_used) % DEBUG_HISTORY_DEPTH); *out = history[(first + index) % DEBUG_HISTORY_DEPTH]; return true; }
void controller_counters_get(dbg_counters_t *out) { *out = counters; }
void controller_counters_reset(void) { memset((void *)&counters, 0, sizeof counters); }
void controller_fault_get(fault_record_t *out) { *out = last_fault; }
void controller_motion_checks_get(motion_check_status_t *out)
{
    uint16_t current = encoder_average();
    uint32_t now = now_ms();
    out->current_delta = current > stall_reference ? current - stall_reference : stall_reference - current;
    out->window_remaining_ms = reached(now, stall_check_ms) ? 0u : stall_check_ms - now;
    out->stall_armed = (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING) && motor_duty() > CFG_DUTY_MIN;
    out->direction_armed = (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING) && reached(now, direction_check_ms);
}
adc_trace_status_t controller_adc_trace_begin_dump(void)
{
    adc_trace_status_t status;
    status.count = adc_trace_count <= DEBUG_ADC_TRACE_DEPTH ? adc_trace_count : DEBUG_ADC_TRACE_DEPTH;
    status.head = adc_trace_head < DEBUG_ADC_TRACE_DEPTH ? adc_trace_head : 0u;
    status.frozen = adc_trace_frozen;
    adc_trace_frozen = true;
    adc_trace_dump_count = status.count;
    adc_trace_dump_first = (uint16_t)((status.head + DEBUG_ADC_TRACE_DEPTH - adc_trace_dump_count) % DEBUG_ADC_TRACE_DEPTH);
    return status;
}
bool controller_adc_trace_get(uint16_t index, adc_trace_entry_t *out)
{
    if (out == NULL || index >= adc_trace_dump_count || adc_trace_dump_first >= DEBUG_ADC_TRACE_DEPTH) return false;
    uint16_t physical = (uint16_t)(adc_trace_dump_first + index);
    if (physical >= DEBUG_ADC_TRACE_DEPTH) physical = (uint16_t)(physical - DEBUG_ADC_TRACE_DEPTH);
    if (physical >= DEBUG_ADC_TRACE_DEPTH) return false;
    *out = adc_trace[physical];
    return true;
}
#endif
position_t controller_target(void) { return target; }
