#include "controller.h"
#include "encoder.h"
#include "motor.h"
#include "console.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#ifdef LUFTFUGL_DEBUG
#include "debug.h"
#endif

static volatile sys_state_t state;
static volatile position_t position, target;
static position_t last_valid, last_reported;
static direction_t last_direction;
static volatile request_kind_t mailbox;
static volatile position_t mailbox_arg;
static uint32_t deadline_ms, brake_until_ms;
#ifdef LUFTFUGL_DEBUG
static volatile bool debug_pending;
static volatile dbg_request_t debug_mailbox;
#endif

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static bool reached(uint32_t now, uint32_t deadline) { return deadline && (int32_t)(now - deadline) >= 0; }
static bool valid(position_t p) { return p >= POS_MIN && p <= POS_MAX; }
static uint8_t distance(position_t a, position_t b) { return a > b ? a - b : b - a; }

static direction_t recover_direction(position_t last)
{
    /* Direction history is unsafe at a limit: it can continue outward past the harness boundary. */
    if (last == POS_MIN) return DIR_FWD;
    if (last == POS_MAX) return DIR_REV;
    return last_direction;
}

static uint8_t speed_for(position_t tgt, uint8_t remaining)
{
    if (remaining > 1) return CFG_DUTY_NORMAL;
    if (tgt == POS_MIN || tgt == POS_MAX) return CFG_DUTY_CREEP;
    return CFG_DUTY_APPROACH;
}

static void enter_fault(event_kind_t event)
{
    motor_brake();
    motor_disable();
    deadline_ms = 0;
    state = ST_FAULT;
    console_push_event(event, 0);
}

static void begin_home(uint32_t now)
{
    motor_brake();
    motor_enable();
    target = POS_MIN;
    last_direction = DIR_REV;
    deadline_ms = now + CFG_TIMEOUT_HOME_MS;
    state = ST_HOMING;
    motor_drive(DIR_REV, CFG_DUTY_CREEP);
    console_push_event(EV_HOMING, 0);
}

static void begin_move(position_t tgt, uint32_t now)
{
    uint8_t steps = distance(tgt, position);
    target = tgt;
    last_direction = tgt > position ? DIR_FWD : DIR_REV;
    deadline_ms = now + (uint32_t)steps * CFG_TIMEOUT_STEP_MS;
    state = steps == 1 ? ST_APPROACH : ST_MOVING;
    motor_drive(last_direction, speed_for(tgt, steps));
}

static void enter_recover(uint32_t now)
{
    direction_t direction = recover_direction(last_valid);
    state = ST_RECOVER;
    deadline_ms = now + CFG_TIMEOUT_RECOVER_MS;
    motor_drive(direction, CFG_DUTY_CREEP);
}

static void arrive(position_t p, uint32_t now)
{
    motor_brake();
    position = p;
    last_valid = p;
    last_reported = p;
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
    last_valid = POS_UNKNOWN;
    last_reported = POS_UNKNOWN;
    last_direction = DIR_REV;
    mailbox = REQ_NONE;
    deadline_ms = 0;
    brake_until_ms = 0;
#ifdef LUFTFUGL_DEBUG
    debug_pending = false;
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
    if (position == POS_UNKNOWN) return MOVE_POS_UNKNOWN;
    if (state == ST_MOVING || state == ST_APPROACH || state == ST_HOMING || state == ST_RECOVER || reached(now, brake_until_ms)) return MOVE_BUSY;
    if (arg == position) return MOVE_ALREADY;
    if ((position == POS_MIN && arg < position) || (position == POS_MAX && arg > position)) return MOVE_ENDSTOP;
    mailbox_arg = arg;
    mailbox = kind;
    return MOVE_OK;
}

#ifdef LUFTFUGL_DEBUG
bool controller_debug_request(const dbg_request_t *req)
{
    if (debug_pending) return false;
    if (req->op != DBG_OP_EXIT && req->op != DBG_OP_FAULT_CLEAR && !dbg_motor_armed()) return false;
    debug_mailbox = *req;
    debug_pending = true;
    return true;
}
#endif

void controller_tick(void)
{
    uint32_t now = now_ms();
    position_t changed;
    watchdog_update();
#ifdef LUFTFUGL_DEBUG
    if (debug_pending) {
        dbg_request_t req = debug_mailbox;
        debug_pending = false;
        switch (req.op) {
        case DBG_OP_ENTER: if (state == ST_IDLE) { state = ST_DEBUG; motor_brake(); } break;
        case DBG_OP_EXIT: motor_brake(); deadline_ms = 0; state = ST_IDLE; break;
        case DBG_OP_DRIVE: if (state == ST_DEBUG) { motor_drive(req.dir, req.duty); deadline_ms = now + req.ms; } break;
        case DBG_OP_BRAKE: motor_brake(); deadline_ms = 0; break;
        case DBG_OP_COAST: if (state == ST_DEBUG) motor_coast(); break;
        case DBG_OP_STANDBY: if (state == ST_DEBUG) { if (req.flag) motor_enable(); else motor_disable(); } break;
        case DBG_OP_FAULT_CLEAR: if (state == ST_FAULT) { state = ST_IDLE; position = POS_UNKNOWN; last_valid = POS_UNKNOWN; last_direction = DIR_REV; motor_enable(); motor_brake(); } break;
        default: break;
        }
    }
#endif

    request_kind_t request = mailbox;
    if (request != REQ_NONE) {
        position_t arg = mailbox_arg;
        mailbox = REQ_NONE;
        if (request == REQ_STOP) {
            motor_brake(); deadline_ms = 0; target = POS_UNKNOWN;
            if (state != ST_FAULT) state = ST_IDLE;
            if (position == POS_UNKNOWN) console_push_event(EV_STOPPED_UNKNOWN, 0);
        } else if (request == REQ_HOME) {
            begin_home(now);
        } else if (request == REQ_MOVE) {
            begin_move(arg, now);
        }
    }

    if (encoder_take_change(&changed)) {
        position = changed;
        if (valid(changed)) last_valid = changed;
        else if (state == ST_MOVING || state == ST_APPROACH) enter_recover(now);
    }

    if (state == ST_BOOT) {
        position = encoder_confirmed();
        if (valid(position)) arrive(position, now); else begin_home(now);
        return;
    }

    if (reached(now, deadline_ms)) {
#ifdef LUFTFUGL_DEBUG
        if (state == ST_DEBUG) { motor_brake(); deadline_ms = 0; return; }
#endif
        if (state == ST_MOVING || state == ST_APPROACH) {
            motor_brake(); console_push_event(EV_TIMEOUT, 0); begin_home(now);
        } else if (state == ST_HOMING) enter_fault(EV_FAULT_HOME);
        else if (state == ST_RECOVER) enter_fault(EV_FAULT_RECOVER);
        return;
    }

    switch (state) {
    case ST_MOVING: {
        position_t instant = encoder_instant();
        if (valid(instant) && instant != last_reported && instant != target) {
            last_reported = instant;
            console_push_event(EV_PASS, instant);
        }
        if (valid(position) && distance(position, target) == 1) {
            state = ST_APPROACH;
            motor_drive(last_direction, speed_for(target, 1));
        }
        break;
    }
    case ST_APPROACH:
        if ((target == POS_MIN || target == POS_MAX) && encoder_instant() == target) motor_brake();
        if (encoder_confirmed() == target) arrive(target, now);
        break;
    case ST_HOMING:
        if (encoder_confirmed() == POS_MIN) arrive(POS_MIN, now);
        break;
    case ST_RECOVER:
        if (valid(encoder_confirmed())) arrive(encoder_confirmed(), now);
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
}

sys_state_t controller_state(void) { return state; }
position_t controller_position(void) { return position; }
#ifdef LUFTFUGL_DEBUG
uint32_t controller_deadline_ms(void) { return deadline_ms; }
direction_t controller_last_direction(void) { return last_direction; }
#endif
position_t controller_target(void) { return target; }
