#include "controller.h"
#include "console.h"
#include "encoder.h"
#include "hardware/watchdog.h"
#include "motor.h"
#include "power_monitor.h"
#include "pico/time.h"
#ifdef LUFTFUGL_MONITOR
#include <string.h>
#define TICK_RETURN() goto tick_done
#else
#define TICK_RETURN() return
#endif
#ifdef LUFTFUGL_MONITOR
#include "debug.h"
#endif

static volatile sys_state_t state;
static volatile position_t position, target;
static direction_t last_direction;
static volatile request_kind_t mailbox;
static volatile bool station1_lock;
static volatile position_t mailbox_arg;
static volatile int16_t mailbox_delta;
static volatile uint16_t mailbox_value;
static uint32_t brake_until_ms, deadline_ms;
static uint16_t motion_start_adc, motion_target_adc, previous_motion_adc;
static uint16_t motion_progress_adc;
static uint16_t jog_remaining;
static uint8_t passed_mask;
static bool jog_move, target_braking, target_correcting, stiction_boost;
static uint32_t target_brake_since, motion_progress_ms;
#ifdef LUFTFUGL_MONITOR
static bool adc_move;
static uint16_t adc_target;
static volatile bool debug_pending;
static volatile dbg_request_t debug_mailbox;
static volatile tick_stats_t tick_stats;
static volatile dbg_counters_t counters;
static volatile motion_trace_entry_t motion_trace[DEBUG_HISTORY_DEPTH];
static volatile uint8_t motion_trace_head, motion_trace_used;
static uint32_t motion_trace_next_ms;
#ifdef LUFTFUGL_DEBUG
static uint32_t debug_drive_until_ms;
#endif
#endif

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
#ifdef LUFTFUGL_MONITOR
static void timing_finish(uint32_t start) {
  uint32_t elapsed = time_us_32() - start;
  if (!tick_stats.count || elapsed < tick_stats.min_us)
    tick_stats.min_us = elapsed;
  if (elapsed > tick_stats.max_us)
    tick_stats.max_us = elapsed;
  tick_stats.sum_us += elapsed;
  ++tick_stats.count;
  if (elapsed > TICK_PERIOD_US) {
    ++tick_stats.overruns;
    ++counters.tick_overruns;
  }
}
static void motion_trace_reset(uint32_t now) {
  motion_trace_head = motion_trace_used = 0u;
  motion_trace_next_ms = now;
}
static void motion_trace_sample(uint32_t now) {
  if ((int32_t)(now - motion_trace_next_ms) < 0)
    return;
  motion_trace[motion_trace_head] =
      (motion_trace_entry_t){.ms = now,
                             .adc = encoder_average(),
                             .direction = motor_direction(),
                             .duty = motor_duty()};
  motion_trace_head =
      (uint8_t)((motion_trace_head + 1u) % DEBUG_HISTORY_DEPTH);
  if (motion_trace_used < DEBUG_HISTORY_DEPTH)
    ++motion_trace_used;
  motion_trace_next_ms = now + DEBUG_MOTION_TRACE_PERIOD_MS;
}
#endif
static bool reached(uint32_t now, uint32_t deadline) {
  return deadline && (int32_t)(now - deadline) >= 0;
}
static bool valid(position_t p) { return p >= POS_MIN && p <= POS_MAX; }
static uint16_t error_magnitude(int16_t error) {
  return error < 0 ? (uint16_t)-error : (uint16_t)error;
}

static uint8_t speed_for_error(int16_t error) {
  uint16_t magnitude = error_magnitude(error);
  if (magnitude <= CFG_APPROACH_COUNTS)
    return CFG_DUTY_APPROACH;
  return CFG_DUTY_NORMAL;
}

static void arrive(position_t p, uint32_t now);

static void begin_home(uint32_t now) {
  uint16_t current = encoder_average();
  uint16_t target_adc = encoder_nominal(POS_MIN);
  int16_t error = (int16_t)target_adc - (int16_t)current;
  motor_brake();
  jog_move = false;
  target_braking = false;
  target_correcting = false;
  stiction_boost = false;
  target_brake_since = 0u;
#ifdef LUFTFUGL_MONITOR
  motion_trace_reset(now);
#endif
  if (error_magnitude(error) <= CFG_ARRIVAL_WINDOW) {
    target = POS_MIN;
    arrive(POS_MIN, now);
    return;
  }
  motor_enable();
  target = POS_MIN;
  motion_start_adc = current;
  motion_target_adc = target_adc;
  previous_motion_adc = current;
  motion_progress_adc = current;
  motion_progress_ms = now;
  passed_mask = 0u;
  last_direction = error > 0 ? DIR_FWD : DIR_REV;
  state = ST_HOMING;
  deadline_ms = now + TIMEOUT_HOME_MS;
  /* Adjacent stations can begin inside APPROACH_COUNTS.  Give the motor a
   * startup pulse so the low final-approach duty cannot leave it stalled. */
  motor_drive(last_direction, error_magnitude(error) <= CFG_POS_WINDOW
                                  ? CFG_DUTY_APPROACH
                                  : CFG_DUTY_NORMAL);
  power_monitor_motion_start();
  console_push_event(EV_HOMING, 0);
}

static void begin_move(position_t tgt) {
  uint16_t current = encoder_average();
  uint16_t target_adc = encoder_nominal(tgt);
  int16_t error = (int16_t)target_adc - (int16_t)current;
#ifdef LUFTFUGL_MONITOR
  motion_trace_reset(now_ms());
#endif
  target = tgt;
  jog_move = false;
  target_braking = false;
  target_correcting = false;
  stiction_boost = false;
  target_brake_since = 0u;
  motion_start_adc = current;
  motion_target_adc = target_adc;
  previous_motion_adc = current;
  motion_progress_adc = current;
  motion_progress_ms = now_ms();
  /* The filtered ADC may settle a few counts beyond the starting station's
   * nominal value.  Mark that confirmed station as already reported so
   * reverse travel cannot misidentify its nominal crossing as PASS:start. */
  passed_mask = (uint8_t)(1u << (position - POS_MIN));
  last_direction = error > 0 ? DIR_FWD : DIR_REV;
  state =
      error_magnitude(error) <= CFG_APPROACH_COUNTS ? ST_APPROACH : ST_MOVING;
  uint8_t steps = tgt > position ? (uint8_t)(tgt - position)
                                 : (uint8_t)(position - tgt);
  deadline_ms = now_ms() + (uint32_t)steps * TIMEOUT_STEP_MS;
  motor_enable();
  motor_drive(last_direction, speed_for_error(error));
  power_monitor_motion_start();
}

static void begin_jog(int16_t delta, uint16_t target_adc) {
  uint16_t current = encoder_average();
#ifdef LUFTFUGL_MONITOR
  motion_trace_reset(now_ms());
#endif

  jog_move = true;
  target_braking = false;
  target_correcting = false;
  stiction_boost = false;
  target_brake_since = 0u;
  target = POS_BETWEEN;
  motion_start_adc = current;
  motion_target_adc = target_adc;
  previous_motion_adc = current;
  motion_progress_adc = current;
  motion_progress_ms = now_ms();
  jog_remaining = error_magnitude(delta);
  passed_mask = 0u;
  last_direction = delta > 0 ? DIR_FWD : DIR_REV;
  state = ST_MOVING;
  deadline_ms = now_ms() + JOG_TIMEOUT_MS;
  motor_enable();
  motor_drive(last_direction, CFG_DUTY_CREEP);
  power_monitor_motion_start();
}

static void arrive(position_t p, uint32_t now) {
#ifdef LUFTFUGL_MONITOR
  if (state == ST_MOVING || state == ST_APPROACH)
    ++counters.moves_ok;
#endif
  motor_brake();
  power_monitor_motion_stop();
  position = p;
  brake_until_ms = now + CFG_BRAKE_HOLD_MS;
  deadline_ms = 0u;
  state = ST_IDLE;
  console_push_event(EV_ARRIVE, p);
}

void controller_init(void) {
  state = ST_BOOT;
  position = POS_UNKNOWN;
  target = POS_UNKNOWN;
  last_direction = DIR_REV;
  mailbox = REQ_NONE;
  station1_lock = false;
  mailbox_delta = 0;
  mailbox_value = 0u;
  brake_until_ms = 0;
  deadline_ms = 0u;
  motion_start_adc = motion_target_adc = previous_motion_adc = 0u;
  motion_progress_adc = 0u;
  motion_progress_ms = 0u;
  jog_remaining = 0u;
  passed_mask = 0u;
  jog_move = false;
  target_braking = false;
  target_correcting = false;
  stiction_boost = false;
  target_brake_since = 0u;
#ifdef LUFTFUGL_MONITOR
  adc_move = false;
  adc_target = 0u;
  debug_pending = false;
  memset((void *)&tick_stats, 0, sizeof tick_stats);
  memset((void *)&counters, 0, sizeof counters);
  memset((void *)motion_trace, 0, sizeof motion_trace);
  motion_trace_head = motion_trace_used = 0u;
  motion_trace_next_ms = 0u;
#ifdef LUFTFUGL_DEBUG
  debug_drive_until_ms = 0u;
#endif
#endif
}

void controller_set_station1_lock(bool locked) { station1_lock = locked; }

move_result_t controller_request(request_kind_t kind, position_t arg) {
  uint32_t now = now_ms();
  if (kind == REQ_STOP || kind == REQ_HOME) {
    mailbox_arg = arg;
    mailbox = kind;
    return MOVE_OK;
  }
  if (station1_lock && (kind != REQ_MOVE || arg != POS_MIN))
    return MOVE_BUSY;
  if (arg < POS_MIN || arg > POS_MAX)
    return MOVE_INVALID;
  if (state == ST_FAULT)
    return MOVE_FAULT;
  if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING)
    return MOVE_BUSY;
  if (brake_until_ms && !reached(now, brake_until_ms))
    return MOVE_BUSY;
  if (position == POS_UNKNOWN || position == POS_BETWEEN)
    return MOVE_POS_UNKNOWN;
  if (arg == position) {
    /* The logical station can briefly lag the physical pot while a move is
     * settling.  Never reject a request as already-at-target unless the live
     * filtered ADC also confirms that station. */
    uint16_t current = encoder_average();
    uint16_t target_adc = encoder_nominal(arg);
    uint16_t distance = current > target_adc ? current - target_adc
                                             : target_adc - current;
    if (distance <= CFG_POS_WINDOW)
      return MOVE_ALREADY;
  }
  mailbox_arg = arg;
  mailbox = kind;
  return MOVE_OK;
}

jog_result_t controller_request_jog(int16_t delta, uint16_t *from_adc) {
  uint16_t current;

  if (station1_lock)
    return JOG_BUSY;
  if (delta < -(int16_t)ADC_MAX_VALUE || delta > (int16_t)ADC_MAX_VALUE)
    return JOG_INVALID;
  if (state != ST_IDLE || mailbox != REQ_NONE)
    return JOG_BUSY;
  current = encoder_average();
  if (current < CFG_LOW_ENDSTOP_ADC || current > CFG_HIGH_ENDSTOP_ADC)
    return JOG_ENDSTOP;
  int32_t endpoint = (int32_t)current + delta;
  if (endpoint < (int32_t)CFG_LOW_ENDSTOP_ADC ||
      endpoint > (int32_t)CFG_HIGH_ENDSTOP_ADC)
    return JOG_ENDSTOP;
  if (from_adc)
    *from_adc = current;
  mailbox_delta = delta;
  mailbox_value = (uint16_t)endpoint;
  mailbox = REQ_JOG;
  return JOG_OK;
}

move_result_t controller_request_setpos(position_t pos, uint16_t adc) {
  uint16_t values[POS_MAX];

  if (pos < POS_MIN || pos > POS_MAX)
    return MOVE_INVALID;
  if (state != ST_IDLE || mailbox != REQ_NONE)
    return MOVE_BUSY;
  for (position_t p = POS_MIN; p <= POS_MAX; ++p)
    values[p - POS_MIN] = encoder_nominal(p);
  values[pos - POS_MIN] = adc;
  for (position_t p = POS_MIN; p < POS_MAX; ++p) {
    uint16_t lower = values[p - POS_MIN];
    uint16_t upper = values[p];
    if (lower >= upper ||
        (uint32_t)CFG_POS_WINDOW * 4u >= (uint32_t)upper - lower)
      return MOVE_INVALID;
  }
  mailbox_arg = pos;
  mailbox_value = adc;
  mailbox = REQ_SETPOS;
  return MOVE_OK;
}

move_result_t controller_request_reset_positions(void) {
  if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING ||
      mailbox != REQ_NONE)
    return MOVE_BUSY;
  mailbox = REQ_RESET_POSITIONS;
  return MOVE_OK;
}

#ifdef LUFTFUGL_MONITOR
move_result_t controller_debug_goto_adc(uint16_t adc) {
  uint32_t now = now_ms();
  if (station1_lock)
    return MOVE_BUSY;
  if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING
#ifdef LUFTFUGL_DEBUG
      || state == ST_DEBUG
#endif
  )
    return MOVE_BUSY;
  if (brake_until_ms && !reached(now, brake_until_ms))
    return MOVE_BUSY;
  if (adc < CFG_LOW_ENDSTOP_ADC || adc > CFG_HIGH_ENDSTOP_ADC)
    return MOVE_INVALID;
  uint16_t current = encoder_average();
  uint16_t delta = current > adc ? current - adc : adc - current;
  if (delta <= CFG_POS_WINDOW)
    return MOVE_ALREADY;
  dbg_request_t req = {.op = DBG_OP_GOTO_ADC, .adc = adc};
  return controller_debug_request(&req) ? MOVE_OK : MOVE_BUSY;
}

bool controller_debug_request(const dbg_request_t *req) {
  if (station1_lock && req->op != DBG_OP_EXIT)
    return false;
#ifdef LUFTFUGL_DEBUG
  if (debug_pending && req->op != DBG_OP_EXIT &&
      !(req->op == DBG_OP_SIM_ENABLE && !req->flag))
    return false;
  if (req->op != DBG_OP_EXIT && req->op != DBG_OP_SIM_ENABLE &&
      req->op != DBG_OP_SIM_SET &&
      req->op != DBG_OP_GOTO_ADC &&
      !(req->op == DBG_OP_ENTER && encoder_sim_active()) && !dbg_motor_armed())
    return false;
#else
  if (debug_pending)
    return false;
  if (req->op != DBG_OP_EXIT && req->op != DBG_OP_GOTO_ADC)
    return false;
#endif
  debug_mailbox = *req;
  debug_pending = true;
  return true;
}
#endif

void controller_tick(void) {
#ifdef LUFTFUGL_MONITOR
  uint32_t tick_start = time_us_32();
#endif
  uint32_t now = now_ms();
  position_t changed;
  watchdog_update();
  if (reached(now, brake_until_ms))
    brake_until_ms = 0u;
#ifdef LUFTFUGL_MONITOR
  if (debug_pending) {
    dbg_request_t req = debug_mailbox;
    debug_pending = false;
    switch (req.op) {
#ifdef LUFTFUGL_DEBUG
    case DBG_OP_ENTER:
      if (state == ST_IDLE) {
        state = ST_DEBUG;
        motor_brake();
      }
      break;
#endif
    case DBG_OP_EXIT:
#ifdef LUFTFUGL_DEBUG
      encoder_sim_enable(false);
      motor_set_inhibit(false);
#endif
      motor_enable();
      motor_brake();
      state = ST_IDLE;
      break;
#ifdef LUFTFUGL_DEBUG
    case DBG_OP_DRIVE:
      if (state == ST_DEBUG) {
        motor_drive(req.dir, req.duty);
        debug_drive_until_ms = now + req.ms;
      }
      break;
    case DBG_OP_BRAKE:
      motor_brake();
      debug_drive_until_ms = 0u;
      break;
    case DBG_OP_COAST:
      if (state == ST_DEBUG)
        motor_coast();
      break;
    case DBG_OP_STANDBY:
      if (state == ST_DEBUG) {
        if (req.flag)
          motor_enable();
        else
          motor_disable();
      }
      break;
    case DBG_OP_SIM_ENABLE:
      if (req.flag) {
        state = ST_IDLE;
        debug_drive_until_ms = 0u;
        motor_set_inhibit(true);
        encoder_sim_set(DEBUG_SIM_DEFAULT_ADC);
        encoder_sim_enable(true);
      } else {
        encoder_sim_enable(false);
        motor_set_inhibit(false);
        motor_enable();
        motor_brake();
      }
      break;
    case DBG_OP_SIM_SET:
      if (encoder_sim_active())
        encoder_sim_set(req.adc);
      break;
    case DBG_OP_GPIO_SET:
      if (req.duty == DEBUG_GPIO_OP_AIN1) {
        motor_set_inhibit(true);
        motor_drive(DIR_FWD, PWM_WRAP);
      } else if (req.duty == DEBUG_GPIO_OP_AIN2) {
        motor_set_inhibit(true);
        motor_drive(DIR_REV, PWM_WRAP);
      } else if (req.duty == DEBUG_GPIO_OP_STBY) {
        motor_set_inhibit(false);
        motor_disable();
        motor_enable();
      } else {
        motor_set_inhibit(false);
        motor_disable();
      }
      break;
#endif
    case DBG_OP_GOTO_ADC: {
      uint16_t current = encoder_average();
      int16_t error = (int16_t)req.adc - (int16_t)current;
      motion_trace_reset(now);
      adc_move = true;
      target_braking = false;
      target_correcting = false;
      stiction_boost = false;
      target_brake_since = 0u;
      adc_target = req.adc;
      target = POS_BETWEEN;
      motion_start_adc = current;
      motion_target_adc = req.adc;
      previous_motion_adc = current;
      motion_progress_adc = current;
      motion_progress_ms = now;
      passed_mask = 0u;
      last_direction = error > 0 ? DIR_FWD : DIR_REV;
      state = error_magnitude(error) <= CFG_APPROACH_COUNTS ? ST_APPROACH
                                                            : ST_MOVING;
      deadline_ms = now + JOG_TIMEOUT_MS;
      motor_enable();
      motor_drive(last_direction, speed_for_error(error));
      break;
    }
    default:
      break;
    }
  }
#endif

  request_kind_t request = mailbox;
  if (request != REQ_NONE) {
    position_t arg = mailbox_arg;
    int16_t request_delta = mailbox_delta;
    uint16_t request_value = mailbox_value;
    mailbox = REQ_NONE;
#ifdef LUFTFUGL_MONITOR
    adc_move = false;
#endif
    if (request == REQ_STOP) {
      motor_brake();
      power_monitor_motion_stop();
      deadline_ms = 0u;
      jog_move = false;
      target = POS_UNKNOWN;
      state = ST_IDLE;
      if (position == POS_UNKNOWN || position == POS_BETWEEN)
        console_push_event(EV_STOPPED_UNKNOWN, 0);
    } else if (request == REQ_HOME) {
      begin_home(now);
    } else if (request == REQ_MOVE) {
      begin_move(arg);
    } else if (request == REQ_JOG) {
      begin_jog(request_delta, request_value);
    } else if (request == REQ_SETPOS) {
      encoder_set_nominal(arg, request_value);
    } else if (request == REQ_RESET_POSITIONS) {
      encoder_reset_nominals();
    }
  }

  if (encoder_take_change(&changed)) {
    position = changed;
  }

  if (state == ST_BOOT) {
    position = encoder_confirmed();
    motor_enable();
    motor_brake();
    state = ST_IDLE;
    if (!valid(position))
      position = POS_BETWEEN;
    /* Initial position classification is not a command or motion event.
       Keep the fresh debug result area empty until something actually
       happens after boot. */
    TICK_RETURN();
  }

  if (deadline_ms && reached(now, deadline_ms)) {
    bool was_jog = jog_move;
#ifdef LUFTFUGL_MONITOR
    was_jog = was_jog || adc_move;
#endif
    deadline_ms = 0u;
    motor_brake();
    power_monitor_motion_stop();
    jog_move = false;
#ifdef LUFTFUGL_MONITOR
    adc_move = false;
#endif
    if (state == ST_HOMING) {
      motor_disable();
      target = POS_UNKNOWN;
      state = ST_FAULT;
      console_push_event(EV_FAULT_HOME, 0u);
    } else {
      target = POS_UNKNOWN;
      position = encoder_confirmed();
      console_push_event(EV_TIMEOUT, 0u);
      if (was_jog) {
        state = ST_IDLE;
      } else {
        begin_home(now);
      }
    }
    TICK_RETURN();
  }
#ifdef LUFTFUGL_DEBUG
  if (state == ST_DEBUG && reached(now, debug_drive_until_ms)) {
      motor_brake();
      debug_drive_until_ms = 0u;
      TICK_RETURN();
  }
#endif

  if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING) {
    uint16_t current = encoder_average();
    int16_t error = (int16_t)motion_target_adc - (int16_t)current;
    uint16_t magnitude = error_magnitude(error);
    /* A station command is one monotonic move.  Do not recalculate the
     * direction from every noisy ADC sample: doing so turns a small
     * overshoot/noise excursion into forward/backward hunting.  The direction
     * is latched when the request starts and the filtered target window is
     * what ends the move. */
    direction_t direction = last_direction;
    bool jog_reached = false;
    if (jog_move) {
      uint16_t progress = 0u;
      if (last_direction == DIR_FWD) {
        if (current >= previous_motion_adc)
          progress = current - previous_motion_adc;
        else if (previous_motion_adc > 3u * 1024u && current < 1024u)
          progress = (uint16_t)(4096u - previous_motion_adc + current);
      } else if (current <= previous_motion_adc) {
        progress = previous_motion_adc - current;
      } else if (previous_motion_adc < 1024u && current > 3u * 1024u) {
        progress = (uint16_t)(previous_motion_adc + 4096u - current);
      }
      if (progress >= jog_remaining) {
        jog_remaining = 0u;
        jog_reached = true;
      } else {
        jog_remaining = (uint16_t)(jog_remaining - progress);
      }
    }

    if (jog_reached) {
      motor_brake();
      jog_move = false;
      target = POS_UNKNOWN;
      position = encoder_instant();
      state = ST_IDLE;
      console_push_event(EV_JOG_COMPLETE, 0);
      TICK_RETURN();
    }

    /* Homing needs a fresh reference too, but its crossings are not protocol
     * events. */
    if (state != ST_HOMING && !jog_move) {
      if (motion_start_adc < motion_target_adc) {
        for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
          uint16_t nominal = encoder_nominal(p);
          uint8_t bit = (uint8_t)(1u << (p - POS_MIN));
          if (!(passed_mask & bit) && nominal > motion_start_adc &&
              nominal < motion_target_adc && previous_motion_adc < nominal &&
              current >= nominal) {
            passed_mask |= bit;
            console_push_event(EV_PASS, p);
#ifdef LUFTFUGL_MONITOR
            ++counters.pass_events;
#endif
          }
        }
      } else {
        for (position_t p = POS_MAX; p >= POS_MIN; --p) {
          uint16_t nominal = encoder_nominal(p);
          uint8_t bit = (uint8_t)(1u << (p - POS_MIN));
          if (!(passed_mask & bit) && nominal < motion_start_adc &&
              nominal > motion_target_adc && previous_motion_adc > nominal &&
              current <= nominal) {
            passed_mask |= bit;
            console_push_event(EV_PASS, p);
#ifdef LUFTFUGL_MONITOR
            ++counters.pass_events;
#endif
          }
          if (p == POS_MIN)
            break;
        }
      }
    }
    previous_motion_adc = current;

    if (!jog_move && !target_braking) {
      uint16_t progress = current > motion_progress_adc
                              ? current - motion_progress_adc
                              : motion_progress_adc - current;
      if (progress >= CFG_ARRIVAL_WINDOW) {
        motion_progress_adc = current;
        motion_progress_ms = now;
      } else if ((uint32_t)(now - motion_progress_ms) >=
                 CFG_BRAKE_HOLD_MS) {
        /* The loaded 3.3 V mechanism can stall at approach duty.  Once a
         * whole settle interval passes without meaningful encoder progress,
         * keep normal duty for the remainder of this move.  Target-window
         * braking still bounds the resulting travel. */
        stiction_boost = true;
      }
    }

    if (jog_move) {
      motor_drive(last_direction, CFG_DUTY_CREEP);
    } else {
      /* Once the filtered ADC enters the target window, brake for the
       * debounce interval.  Requiring the classifier to name the station
       * here can leave a real move marked busy forever when the pot settles
       * near an edge of its band; the ADC window is the authoritative target
       * test and is already bounded by the ordered station configuration. */
      bool target_window = magnitude <=
                           (target_correcting ? CFG_ARRIVAL_WINDOW
                                              : CFG_POS_WINDOW);
      if (state == ST_HOMING && !target_correcting) {
        /* Homing can cross the narrow ADC band between 1 kHz samples.  Use
         * the travel direction as the reference and stop on the first
         * directional crossing instead of driving past station 1. */
        target_window = last_direction == DIR_REV
                            ? current <= motion_target_adc + CFG_POS_WINDOW
                            : current + CFG_POS_WINDOW >= motion_target_adc;
      }
      if (!target_braking && target_window) {
        target_braking = true;
        target_brake_since = now;
      }
      if (target_braking)
        motor_brake();
      else {
        /* A correction begins after a full brake, so creep duty can leave the
         * gearbox stuck before it re-enters the target band.  Reuse the
         * measured normal duty to break stiction; the wide target window
         * still brakes before the tighter settled-arrival check. */
        uint8_t duty = (target_correcting || stiction_boost)
                           ? CFG_DUTY_NORMAL
                           : speed_for_error(error);
        if (!target_correcting && current == motion_start_adc &&
            magnitude > CFG_POS_WINDOW)
          duty = CFG_DUTY_NORMAL;
        motor_drive(direction, duty);
      }
    }

#ifdef LUFTFUGL_MONITOR
    motion_trace_sample(now);
#endif

#ifdef LUFTFUGL_MONITOR
    if (adc_move) {
      if (target_braking &&
          (uint32_t)(now - target_brake_since) >= CFG_DEBOUNCE_MS) {
        brake_until_ms = now + CFG_BRAKE_HOLD_MS;
        state = ST_IDLE;
        position = encoder_instant();
        adc_move = false;
        console_push_event(EV_JOG_COMPLETE, 0);
        TICK_RETURN();
      }
    } else {
#endif
      if (!jog_move && target_braking &&
          (uint32_t)(now - target_brake_since) >= CFG_BRAKE_HOLD_MS) {
        /* Do not report arrival merely because the first sample entered the
         * band.  Mechanical inertia can carry the pot well past the target
         * during the brake interval.  Settle inside the tighter arrival
         * window so normal ADC drift cannot immediately make the station
         * unknown again.  Homing uses the same check: its directional
         * crossing rule brakes early for harness safety, but is not evidence
         * that the mechanism actually stopped at station 1. */
        if (error_magnitude((int16_t)motion_target_adc -
                            (int16_t)encoder_average()) <=
            CFG_ARRIVAL_WINDOW) {
          arrive(target, now);
          TICK_RETURN();
        }
        target_braking = false;
        target_correcting = true;
        stiction_boost = true;
        last_direction = error > 0 ? DIR_FWD : DIR_REV;
      }
#ifdef LUFTFUGL_MONITOR
    }
#endif
    if (state != ST_HOMING)
      state = magnitude <= CFG_APPROACH_COUNTS ? ST_APPROACH : ST_MOVING;
  }

  switch (state) {
  case ST_MOVING:
  case ST_APPROACH:
  case ST_HOMING:
    break;
  case ST_FAULT:
    motor_disable();
    break;
  case ST_IDLE:
    motor_brake();
    break;
#ifdef LUFTFUGL_DEBUG
  case ST_DEBUG:
    break;
#endif
  default:
    break;
  }
#ifdef LUFTFUGL_MONITOR
tick_done:
  timing_finish(tick_start);
#endif
}

sys_state_t controller_state(void) { return state; }
position_t controller_position(void) { return position; }
#ifdef LUFTFUGL_MONITOR
uint16_t controller_target_adc(void) {
  return adc_move ? adc_target : (valid(target) ? encoder_nominal(target) : 0u);
}
direction_t controller_last_direction(void) { return last_direction; }
void controller_timing_get(tick_stats_t *out) { *out = tick_stats; }
void controller_timing_reset(void) {
  memset((void *)&tick_stats, 0, sizeof tick_stats);
}
void controller_counters_get(dbg_counters_t *out) { *out = counters; }
void controller_counters_reset(void) {
  memset((void *)&counters, 0, sizeof counters);
}
uint8_t controller_motion_trace_count(void) { return motion_trace_used; }
bool controller_motion_trace_get(uint8_t index, motion_trace_entry_t *out) {
  uint8_t used = motion_trace_used;
  uint8_t head = motion_trace_head;
  if (index >= used)
    return false;
  uint8_t first =
      (uint8_t)((head + DEBUG_HISTORY_DEPTH - used) % DEBUG_HISTORY_DEPTH);
  *out = motion_trace[(first + index) % DEBUG_HISTORY_DEPTH];
  return true;
}
#endif
position_t controller_target(void) { return target; }
