#include "controller.h"
#include "console.h"
#include "encoder.h"
#include "hardware/watchdog.h"
#include "motor.h"
#include "pico/time.h"
#define TICK_RETURN() return

static volatile sys_state_t state;
static volatile position_t position, target;
static direction_t last_direction;
static volatile request_kind_t mailbox;
static volatile position_t mailbox_arg;
static volatile int16_t mailbox_delta;
static volatile uint16_t mailbox_value;
static uint32_t deadline_ms, brake_until_ms;
static uint16_t stall_reference, best_error_magnitude;
static uint16_t motion_start_adc, motion_target_adc, previous_motion_adc;
static uint8_t passed_mask;
static bool endpoint_braking;
static bool jog_move;
static bool overtravel_recovery;
static uint16_t recovery_best_adc;
static direction_t recovery_direction;
static uint32_t stall_check_ms, direction_check_ms;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static bool reached(uint32_t now, uint32_t deadline) {
  return deadline && (int32_t)(now - deadline) >= 0;
}
static bool valid(position_t p) { return p >= POS_MIN && p <= POS_MAX; }
static uint16_t error_magnitude(int16_t error) {
  return error < 0 ? (uint16_t)-error : (uint16_t)error;
}

static uint8_t speed_for_error(int16_t error, position_t tgt) {
  uint16_t magnitude = error_magnitude(error);
  if (magnitude <= APPROACH_COUNTS)
    return tgt == POS_MIN || tgt == POS_MAX ? DUTY_CREEP
                                            : DUTY_APPROACH;
  return DUTY_NORMAL;
}

static void arrive(position_t p, uint32_t now);

static void enter_fault(event_kind_t event) {
  endpoint_braking = false;
  jog_move = false;
  overtravel_recovery = false;
  motor_brake();
  motor_disable();
  deadline_ms = 0;
  state = ST_FAULT;
  console_push_event(event, 0);
}

static void begin_overtravel_recovery(uint32_t now) {
  uint16_t current = encoder_average();

  motor_enable();
  target = POS_MIN;
  recovery_best_adc = current;
  recovery_direction = current < ADC_SAFE_MIN ? DIR_FWD : DIR_REV;
  last_direction = recovery_direction;
  overtravel_recovery = true;
  deadline_ms = now + TIMEOUT_HOME_MS;
  state = ST_HOMING;
  motor_drive(recovery_direction, DUTY_CREEP);
  console_push_event(EV_HOMING, 0);
}

static void begin_home(uint32_t now) {
  uint16_t current = encoder_average();
  uint16_t target_adc = encoder_nominal(POS_MIN);
  int16_t error = (int16_t)target_adc - (int16_t)current;
  motor_brake();
  jog_move = false;
  if (error_magnitude(error) <= POS_WINDOW) {
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
  deadline_ms = now + TIMEOUT_HOME_MS;
  state = ST_HOMING;
  stall_reference = current;
  best_error_magnitude = error_magnitude(error);
  stall_check_ms = now + STALL_WINDOW_MS;
  direction_check_ms = now + STALL_WINDOW_MS;
  motor_drive(last_direction, speed_for_error(error, target));
  console_push_event(EV_HOMING, 0);
}

static void begin_move(position_t tgt, uint32_t now) {
  uint16_t current = encoder_average();
  uint16_t target_adc = encoder_nominal(tgt);
  int16_t error = (int16_t)target_adc - (int16_t)current;
  uint8_t steps = 1u;
  for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
    uint16_t nominal = encoder_nominal(p);
    if ((error > 0 && nominal > current && nominal < target_adc) ||
        (error < 0 && nominal < current && nominal > target_adc))
      ++steps;
  }
  target = tgt;
  jog_move = false;
  motion_start_adc = current;
  motion_target_adc = target_adc;
  previous_motion_adc = current;
  passed_mask = 0u;
  endpoint_braking = false;
  last_direction = error > 0 ? DIR_FWD : DIR_REV;
  deadline_ms = now + (uint32_t)steps * TIMEOUT_STEP_MS;
  state =
      error_magnitude(error) <= APPROACH_COUNTS ? ST_APPROACH : ST_MOVING;
  stall_reference = current;
  best_error_magnitude = error_magnitude(error);
  stall_check_ms = now + STALL_WINDOW_MS;
  direction_check_ms = now + STALL_WINDOW_MS;
  motor_enable();
  motor_drive(last_direction, speed_for_error(error, tgt));
}

static void begin_jog(int16_t delta, uint16_t target_adc, uint32_t now) {
  uint16_t current = encoder_average();

  jog_move = true;
  target = POS_BETWEEN;
  motion_start_adc = current;
  motion_target_adc = target_adc;
  previous_motion_adc = current;
  passed_mask = 0u;
  endpoint_braking = false;
  last_direction = delta > 0 ? DIR_FWD : DIR_REV;
  deadline_ms = now + JOG_TIMEOUT_MS;
  state = ST_MOVING;
  stall_reference = current;
  best_error_magnitude =
      error_magnitude((int16_t)target_adc - (int16_t)current);
  stall_check_ms = now + STALL_WINDOW_MS;
  direction_check_ms = now + STALL_WINDOW_MS;
  motor_enable();
  motor_drive(last_direction, DUTY_CREEP);
}

static void arrive(position_t p, uint32_t now) {
  motor_brake();
  endpoint_braking = false;
  position = p;
  deadline_ms = 0;
  brake_until_ms = now + BRAKE_HOLD_MS;
  state = ST_IDLE;
  console_push_event(EV_ARRIVE, p);
}

void controller_init(void) {
  state = ST_BOOT;
  position = POS_UNKNOWN;
  target = POS_UNKNOWN;
  last_direction = DIR_REV;
  mailbox = REQ_NONE;
  mailbox_delta = 0;
  mailbox_value = 0u;
  deadline_ms = 0;
  brake_until_ms = 0;
  stall_reference = best_error_magnitude = 0u;
  motion_start_adc = motion_target_adc = previous_motion_adc = 0u;
  passed_mask = 0u;
  endpoint_braking = false;
  jog_move = false;
  overtravel_recovery = false;
  stall_check_ms = direction_check_ms = 0u;
}

move_result_t controller_request(request_kind_t kind, position_t arg) {
  uint32_t now = now_ms();
  if (kind == REQ_STOP || kind == REQ_HOME) {
    mailbox_arg = arg;
    mailbox = kind;
    return MOVE_OK;
  }
  if (state == ST_FAULT)
    return MOVE_FAULT;
  if (arg < POS_MIN || arg > POS_MAX)
    return MOVE_INVALID;
  if (position == POS_UNKNOWN || !encoder_in_safe_range())
    return MOVE_POS_UNKNOWN;
  if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING)
    return MOVE_BUSY;
  if (brake_until_ms && !reached(now, brake_until_ms))
    return MOVE_BUSY;
  if (arg == position)
    return MOVE_ALREADY;
  int16_t error = encoder_error_to(arg);
  if ((encoder_average() <= encoder_nominal(POS_MIN) + POS_WINDOW &&
       error < 0) ||
      (encoder_average() >= encoder_nominal(POS_MAX) - POS_WINDOW &&
       error > 0))
    return MOVE_ENDSTOP;
  mailbox_arg = arg;
  mailbox = kind;
  return MOVE_OK;
}

jog_result_t controller_request_jog(int16_t delta, uint16_t *from_adc) {
  uint16_t magnitude = error_magnitude(delta);
  uint16_t current;
  int32_t endpoint;

  if (magnitude < JOG_MIN_COUNTS || magnitude > JOG_MAX_COUNTS)
    return JOG_INVALID;
  if (state == ST_FAULT)
    return JOG_FAULT;
  if (!encoder_in_safe_range())
    return JOG_OVERTRAVEL;
  if (state != ST_IDLE || mailbox != REQ_NONE)
    return JOG_BUSY;
  current = encoder_average();
  if (from_adc)
    *from_adc = current;
  endpoint = (int32_t)current + delta;
  if (endpoint < ADC_SAFE_MIN || endpoint > ADC_SAFE_MAX)
    return JOG_ENDSTOP;
  mailbox_delta = delta;
  mailbox_value = (uint16_t)endpoint;
  mailbox = REQ_JOG;
  return JOG_OK;
}

move_result_t controller_request_setpos(position_t pos, uint16_t adc) {
  uint16_t values[POS_MAX];

  if (state == ST_FAULT)
    return MOVE_FAULT;
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
        (uint32_t)POS_WINDOW * 4u >= (uint32_t)upper - lower)
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


void controller_tick(void) {
  uint32_t now = now_ms();
  position_t changed;
  watchdog_update();
  if (reached(now, brake_until_ms))
    brake_until_ms = 0u;

  request_kind_t request = mailbox;
  if (request != REQ_NONE) {
    position_t arg = mailbox_arg;
    int16_t request_delta = mailbox_delta;
    uint16_t request_value = mailbox_value;
    mailbox = REQ_NONE;
    if (request == REQ_STOP) {
      motor_brake();
      endpoint_braking = false;
      jog_move = false;
      deadline_ms = 0;
      target = POS_UNKNOWN;
      if (state != ST_FAULT)
        state = ST_IDLE;
      if (position == POS_UNKNOWN || position == POS_BETWEEN)
        console_push_event(EV_STOPPED_UNKNOWN, 0);
    } else if (request == REQ_HOME) {
      if (encoder_in_safe_range())
        begin_home(now);
      else if (state == ST_FAULT)
        begin_overtravel_recovery(now);
      else
        enter_fault(EV_FAULT_OVERTRAVEL);
    } else if (request == REQ_MOVE) {
      begin_move(arg, now);
    } else if (request == REQ_JOG) {
      if (!encoder_in_safe_range())
        enter_fault(EV_FAULT_OVERTRAVEL);
      else
        begin_jog(request_delta, request_value, now);
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
    if (!encoder_in_safe_range())
      enter_fault(EV_FAULT_OVERTRAVEL);
    TICK_RETURN();
  }

  if (overtravel_recovery) {
    uint16_t current = encoder_average();
    bool moved_outward;
    /* Compare with the best inward progress so a reversal cannot hide behind
     * the start point. */
    if (recovery_direction == DIR_FWD) {
      moved_outward = current < recovery_best_adc &&
                      recovery_best_adc - current > REVERSE_DELTA;
      if (current > recovery_best_adc)
        recovery_best_adc = current;
    } else {
      moved_outward = current > recovery_best_adc &&
                      current - recovery_best_adc > REVERSE_DELTA;
      if (current < recovery_best_adc)
        recovery_best_adc = current;
    }
    if (moved_outward || reached(now, deadline_ms)) {
      enter_fault(EV_FAULT_OVERTRAVEL);
    } else if (encoder_in_safe_range()) {
      overtravel_recovery = false;
      begin_home(now);
    } else {
      motor_drive(recovery_direction, DUTY_CREEP);
    }
    TICK_RETURN();
  }

  if (state != ST_FAULT
      && !encoder_in_safe_range()) {
    enter_fault(EV_FAULT_OVERTRAVEL);
    TICK_RETURN();
  }

  if (reached(now, deadline_ms)) {
    if (state == ST_MOVING || state == ST_APPROACH) {
      motor_brake();
      if (jog_move) {
        jog_move = false;
        deadline_ms = 0;
        target = POS_UNKNOWN;
        state = ST_IDLE;
        console_push_event(EV_TIMEOUT, 0);
        TICK_RETURN();
      }
      console_push_event(EV_TIMEOUT, 0);
      begin_home(now);
    } else if (state == ST_HOMING)
      enter_fault(EV_FAULT_HOME);
    TICK_RETURN();
  }

  if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING) {
    uint16_t current = encoder_average();
    int16_t error = (int16_t)motion_target_adc - (int16_t)current;
    uint16_t magnitude = error_magnitude(error);
    direction_t direction = error > 0 ? DIR_FWD : DIR_REV;
    bool endpoint_target = target == POS_MIN || target == POS_MAX;
    bool jog_reached =
        jog_move && (last_direction == DIR_FWD ? current >= motion_target_adc
                                               : current <= motion_target_adc);

    if (jog_reached) {
      motor_brake();
      jog_move = false;
      deadline_ms = 0;
      target = POS_UNKNOWN;
      position = encoder_instant();
      state = ST_IDLE;
      console_push_event(EV_JOG_COMPLETE, 0);
      TICK_RETURN();
    }

    if (endpoint_target && magnitude <= POS_WINDOW)
      endpoint_braking = true;
    else if (magnitude > POS_WINDOW + POS_WINDOW / 2u)
      endpoint_braking = false;

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
          }
          if (p == POS_MIN)
            break;
        }
      }
    }
    previous_motion_adc = current;

    /* Continuous position, not the last station window, prevents outward limit
     * drive. */
    if (jog_move) {
      motor_drive(last_direction, DUTY_CREEP);
    } else if (endpoint_braking ||
               (current <= encoder_nominal(POS_MIN) + POS_WINDOW &&
                direction == DIR_REV) ||
               (current >= encoder_nominal(POS_MAX) - POS_WINDOW &&
                direction == DIR_FWD)) {
      motor_brake();
    } else if (magnitude > POS_WINDOW) {
      motor_drive(direction, speed_for_error(error,
                                                      target));
    }

    if (!endpoint_braking && magnitude < best_error_magnitude)
      best_error_magnitude = magnitude;
    else if (!endpoint_braking && reached(now, direction_check_ms) &&
             magnitude > best_error_magnitude &&
             magnitude - best_error_magnitude > REVERSE_DELTA) {
      enter_fault(EV_FAULT_DIRECTION);
      TICK_RETURN();
    }

    if (reached(now, stall_check_ms)) {
      uint16_t delta = current > stall_reference ? current - stall_reference
                                                 : stall_reference - current;
      if (delta < STALL_DELTA) {
        enter_fault(EV_FAULT_STALL);
        TICK_RETURN();
      }
      stall_reference = current;
      stall_check_ms = now + STALL_WINDOW_MS;
    }

      if (!jog_move && encoder_confirmed() == target) {
        arrive(target, now);
        TICK_RETURN();
      }
    if (state != ST_HOMING)
      state = magnitude <= APPROACH_COUNTS ? ST_APPROACH : ST_MOVING;
  }

  switch (state) {
  case ST_MOVING:
  case ST_APPROACH:
  case ST_HOMING:
    break;
  case ST_IDLE:
    motor_brake();
    break;
  case ST_FAULT:
    break;
  default:
    break;
  }
}

sys_state_t controller_state(void) { return state; }
position_t controller_position(void) { return position; }
position_t controller_target(void) { return target; }
