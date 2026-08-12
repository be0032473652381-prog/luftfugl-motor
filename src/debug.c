#include "debug.h"

#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "hardware/uart.h"
#include "motor.h"
#include "pico/time.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_COMMAND_MAX 48u
#define DEBUG_REFRESH_MS 200u

typedef enum {
  PENDING_NONE,
  PENDING_JOG,
  PENDING_MOVE,
  PENDING_HOME
} pending_t;

static bool active, plain_mode, echo_enabled, input_overflow, swallow_lf;
static bool armed;
static position_t selected_station;
static uint16_t jog_step;
static char input[DEBUG_COMMAND_MAX + 1u];
static uint8_t input_len;
static char out_buf[DEBUG_OUT_BUFFER];
static volatile uint16_t out_head, out_tail;
static uint32_t auto_enter_deadline, next_refresh;
static pending_t pending;
static char pending_text[DEBUG_COMMAND_MAX + 1u];
static char status_shadow[8][81];

static uint32_t ms_now(void) { return to_ms_since_boot(get_absolute_time()); }
static const char *state_text(sys_state_t state) {
  static const char *const names[] = {"BOOT",   "IDLE",  "MOVING", "APPROACH",
                                      "HOMING", "FAULT", "DEBUG"};
  return (unsigned)state < sizeof names / sizeof names[0] ? names[state] : "?";
}
static const char *dir_text(direction_t direction) {
  return direction == DIR_FWD ? "FWD" : direction == DIR_REV ? "REV" : "STP";
}

static uint16_t out_free(void) {
  uint16_t used = out_head >= out_tail
                      ? (uint16_t)(out_head - out_tail)
                      : (uint16_t)(DEBUG_OUT_BUFFER - out_tail + out_head);
  return (uint16_t)(DEBUG_OUT_BUFFER - 1u - used);
}

void dbg_out_push(const char *text) {
  while (*text) {
    uint16_t next = (uint16_t)((out_head + 1u) % DEBUG_OUT_BUFFER);
    if (next == out_tail)
      return;
    out_buf[out_head] = *text++;
    out_head = next;
  }
}

void dbg_out_drain(void) {
  while (out_tail != out_head && uart_is_writable(uart0)) {
    uart_putc_raw(uart0, out_buf[out_tail]);
    out_tail = (uint16_t)((out_tail + 1u) % DEBUG_OUT_BUFFER);
  }
}

static void result(const char *command, const char *outcome,
                   const char *detail) {
  char line[256];
  snprintf(line, sizeof line, "%-18s %-10s %s", command, outcome, detail);
  if (plain_mode) {
    dbg_out_push(line);
    dbg_out_push("\r\n");
  } else {
    dbg_out_push("\033[s\033[24;1H");
    dbg_out_push(line);
    dbg_out_push("\033[K\r\n\033[u");
  }
}

void dbg_log_push(const char *text) {
  char line[128];
  uint32_t seconds = ms_now() / 1000u;
  snprintf(line, sizeof line, "%02lu:%02lu:%02lu %s",
           (unsigned long)(seconds / 3600u),
           (unsigned long)((seconds / 60u) % 60u),
           (unsigned long)(seconds % 60u), text);
  if (plain_mode) {
    dbg_out_push(line);
    dbg_out_push("\r\n");
  } else {
    dbg_out_push("\033[s\033[24;1H");
    dbg_out_push(line);
    dbg_out_push("\033[K\r\n\033[u");
  }
}

static void field(uint8_t row, const char *text) {
  char esc[20];
  if (!strcmp(status_shadow[row - 1u], text))
    return;
  snprintf(esc, sizeof esc, "\033[s\033[%u;1H", row);
  /* Keep an update atomic; a skipped field is retried because its shadow is
     changed only after the complete update fits. Reserve space for the frame's
     final scrolling-region sequence during the initial draw. */
  if (out_free() < strlen(esc) + strlen(text) + 8u + 32u)
    return;
  dbg_out_push(esc);
  dbg_out_push(text);
  dbg_out_push("\033[K\033[u");
  strncpy(status_shadow[row - 1u], text, 80u);
  status_shadow[row - 1u][80] = '\0';
}

void dbg_fields_refresh(void) {
  char line[81];
  uint32_t seconds = ms_now() / 1000u;
  position_t position = controller_position();
  uint16_t target_adc = controller_target_adc();
  snprintf(line, sizeof line,
           " luftfugl " FW_VERSION
           "                                      up %02lu:%02lu:%02lu",
           (unsigned long)(seconds / 3600u),
           (unsigned long)((seconds / 60u) % 60u),
           (unsigned long)(seconds % 60u));
  field(1, line);
  snprintf(line, sizeof line,
           "  STATE   %-12s    POSITION  %-5s          ADC   %u",
           state_text(controller_state()),
           position >= POS_MIN && position <= POS_MAX ? (position == 1   ? "1"
                                                         : position == 2 ? "2"
                                                         : position == 3 ? "3"
                                                         : position == 4 ? "4"
                                                                         : "5")
                                                      : "?",
           encoder_average());
  field(3, line);
  snprintf(
      line, sizeof line, "  TARGET  %-12s    ERROR     %-6d         STEP  %u",
      controller_target() >= POS_MIN && controller_target() <= POS_MAX ? "set"
                                                                       : "--",
      target_adc ? (int16_t)target_adc - (int16_t)encoder_average() : 0,
      jog_step);
  field(4, line);
  snprintf(line, sizeof line,
           "  ARMED   %-12s    COUPLED   no             SIM   %s",
           armed ? "yes" : "no", encoder_sim_active() ? "on" : "off");
  field(5, line);
  snprintf(line, sizeof line,
           "  FAULTS  %-12s    DUTY      %-5u          DIR   %s",
           controller_state() == ST_FAULT ? "1" : "0", motor_duty(),
           dir_text(motor_direction()));
  field(6, line);
  snprintf(line, sizeof line,
           "  POSITIONS   1:%u%s  2:%u%s  3:%u%s  4:%u%s  5:%u%s",
           encoder_nominal(1), selected_station == 1 ? " *" : "",
           encoder_nominal(2), selected_station == 2 ? " *" : "",
           encoder_nominal(3), selected_station == 3 ? " *" : "",
           encoder_nominal(4), selected_station == 4 ? " *" : "",
           encoder_nominal(5), selected_station == 5 ? " *" : "");
  field(8, line);
}

static void command_line_draw(void) {
  char line[DEBUG_COMMAND_MAX + 4u];
  if (plain_mode)
    return;
  snprintf(line, sizeof line, "> %.*s", input_len, input);
  dbg_out_push("\033[s\033[11;1H");
  dbg_out_push(line);
  dbg_out_push("\033[K\033[u");
}

void dbg_render(void) {
  if (plain_mode)
    return;
  out_head = out_tail = 0u;
  dbg_out_push("\033[2J\033[H\033[?25l");
  memset(status_shadow, 0, sizeof status_shadow);
  dbg_out_push("\033[9;1H------------------------------------------------------"
               "-------------------------");
  dbg_out_push("\033[10;1H COMMAND");
  command_line_draw();
  dbg_out_push("\033[12;1H-----------------------------------------------------"
               "--------------------------");
  dbg_out_push("\033[13;1H RESULTS");
  dbg_fields_refresh();
  /* This is deliberately the last sequence in the one-time frame draw. */
  dbg_out_push("\033[14;24r\033[24;1H");
}

static bool parse_long(const char *text, long *value) {
  char *end;
  if (!text || !*text)
    return false;
  *value = strtol(text, &end, 10);
  while (isspace((unsigned char)*end))
    ++end;
  return *end == '\0';
}

static void remember_pending(pending_t kind, const char *command) {
  pending = kind;
  strncpy(pending_text, command, sizeof pending_text - 1u);
  pending_text[sizeof pending_text - 1u] = '\0';
}

static bool save_position(position_t station, const char *command) {
  uint16_t values[POS_MAX], adc = encoder_average();
  for (position_t p = POS_MIN; p <= POS_MAX; ++p)
    values[p - 1u] = encoder_nominal(p);
  values[station - 1u] = adc;
  for (position_t p = POS_MIN; p < POS_MAX; ++p) {
    uint16_t gap;
    if (values[p - 1u] >= values[p]) {
      result(command, "rejected", "station values must be strictly ascending");
      return false;
    }
    gap = (uint16_t)(values[p] - values[p - 1u]);
    if ((uint32_t)CFG_POS_WINDOW * 4u >= gap) {
      result(command, "rejected", "POS_WINDOW must be below quarter gap");
      return false;
    }
  }
  move_result_t request = controller_request_setpos(station, adc);
  if (request != MOVE_OK) {
    result(command, "rejected",
           request == MOVE_BUSY ? "controller busy" : "controller fault");
    return false;
  }
  char detail[48];
  snprintf(detail, sizeof detail, "saved, pos %u = %u", station, adc);
  result(command, "complete", detail);
  return true;
}

static bool cfg_update(const char *key, long value) {
  cfg_t next;
  if (!key || value < 0)
    return false;
  if (!strncmp(key, "DUTY_", 5) && value > PWM_WRAP)
    return false;
  if ((!strncmp(key, "ADC_", 4) || !strcmp(key, "POS_WINDOW") ||
       !strcmp(key, "APPROACH_COUNTS")) &&
      value > (long)ADC_MAX_VALUE)
    return false;
  if (strcmp(key, "TIMEOUT_STEP_MS") && strcmp(key, "TIMEOUT_HOME_MS") &&
      value > UINT16_MAX)
    return false;
  memcpy(&next, (const void *)&cfg, sizeof next);
  if (!strcmp(key, "DUTY_NORMAL"))
    next.duty_normal = (uint8_t)value;
  else if (!strcmp(key, "DUTY_APPROACH"))
    next.duty_approach = (uint8_t)value;
  else if (!strcmp(key, "DUTY_CREEP"))
    next.duty_creep = (uint8_t)value;
  else if (!strcmp(key, "DUTY_MIN"))
    next.duty_min = (uint8_t)value;
  else if (!strcmp(key, "POS_WINDOW"))
    next.pos_window = (uint16_t)value;
  else if (!strcmp(key, "APPROACH_COUNTS"))
    next.approach_counts = (uint16_t)value;
  else if (!strcmp(key, "ADC_SAFE_MIN"))
    next.adc_safe_min = (uint16_t)value;
  else if (!strcmp(key, "ADC_SAFE_MAX"))
    next.adc_safe_max = (uint16_t)value;
  else if (!strcmp(key, "STALL_DELTA"))
    next.stall_delta = (uint16_t)value;
  else if (!strcmp(key, "STALL_WINDOW_MS"))
    next.stall_window_ms = (uint16_t)value;
  else if (!strcmp(key, "REVERSE_DELTA"))
    next.reverse_delta = (uint16_t)value;
  else if (!strcmp(key, "DEBOUNCE_MS"))
    next.debounce_ms = (uint16_t)value;
  else if (!strcmp(key, "BRAKE_HOLD_MS"))
    next.brake_hold_ms = (uint16_t)value;
  else if (!strcmp(key, "TIMEOUT_STEP_MS"))
    next.timeout_step_ms = (uint32_t)value;
  else if (!strcmp(key, "TIMEOUT_HOME_MS"))
    next.timeout_home_ms = (uint32_t)value;
  else
    return false;
  if (next.duty_min > next.duty_creep || next.duty_creep > next.duty_approach ||
      next.duty_approach > next.duty_normal ||
      next.adc_safe_min >= next.adc_safe_max ||
      next.adc_safe_max > ADC_MAX_VALUE || next.debounce_ms == 0u ||
      next.brake_hold_ms == 0u || next.stall_window_ms == 0u)
    return false;
  memcpy((void *)&cfg, &next, sizeof next);
  return true;
}

static void export_positions(const char *command) {
  char detail[128];
  snprintf(detail, sizeof detail,
           "#define POS_1_ADC %u; #define POS_2_ADC %u; #define POS_3_ADC %u; "
           "#define POS_4_ADC %u; #define POS_5_ADC %u",
           encoder_nominal(1), encoder_nominal(2), encoder_nominal(3),
           encoder_nominal(4), encoder_nominal(5));
  result(command, "complete", detail);
}

static const char *resolve(const char *word) {
  static const char *const commands[] = {
      "help",   "status", "adc",        "jog",    "step",
      "sel",    "setpos", "positions",  "export", "move",
      "home",   "stop",   "clearfault", "sim",    "arm",
      "disarm", "drive",  "selftest",   "cfg",    "exit"};
  const char *match = NULL;
  size_t n = strlen(word);
  if (!strcmp(word, "st"))
    return "status";
  for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
    if (!strncmp(commands[i], word, n)) {
      if (match)
        return NULL;
      match = commands[i];
    }
  }
  return match;
}

static const char *help_detail(const char *word) {
  const char *command = resolve(word);
  if (!command)
    return NULL;
  if (!strcmp(command, "help"))
    return "help [command]: list commands or show command limits";
  if (!strcmp(command, "status"))
    return "status: report controller state, position, target, motor and ADC";
  if (!strcmp(command, "adc"))
    return "adc: report raw ADC, filtered ADC and classified position";
  if (!strcmp(command, "jog"))
    return "jog +n|-n: bounded motion, 10-500 ADC counts";
  if (!strcmp(command, "step"))
    return "step n: select 10, 25, 100, 250 or 500 counts";
  if (!strcmp(command, "sel"))
    return "sel 1-5: select the setup station";
  if (!strcmp(command, "setpos"))
    return "setpos [1-5]: save filtered ADC for selected station";
  if (!strcmp(command, "positions"))
    return "positions: show station ADC table and measured error";
  if (!strcmp(command, "export"))
    return "export: print paste-ready POS_n_ADC definitions";
  if (!strcmp(command, "move"))
    return "move 1-5: closed-loop move to a station";
  if (!strcmp(command, "home"))
    return "home: run the bounded home sequence";
  if (!strcmp(command, "stop"))
    return "stop: brake immediately; '.' does the same without Enter";
  if (!strcmp(command, "clearfault"))
    return "clearfault: clear fault and make position unknown";
  if (!strcmp(command, "sim"))
    return "sim on|off: enable or disable simulated ADC";
  if (!strcmp(command, "arm"))
    return "arm: enable the manual-drive interlock";
  if (!strcmp(command, "disarm"))
    return "disarm: brake and disable manual drive";
  if (!strcmp(command, "drive"))
    return "drive fwd|rev duty ms: armed pulse, duty 0-255, 10-2000 ms";
  if (!strcmp(command, "selftest"))
    return "selftest: run static, non-motion checks";
  if (!strcmp(command, "cfg"))
    return "cfg [key value]: list or update volatile runtime constants";
  return "exit: leave debug mode and restore the terminal";
}

static void submit(char *typed) {
  char original[DEBUG_COMMAND_MAX + 1u], *arg, *word, *save;
  long value;
  char *end = typed + strlen(typed);
  while (end > typed && isspace((unsigned char)end[-1]))
    *--end = '\0';
  strncpy(original, typed, sizeof original);
  original[sizeof original - 1u] = '\0';
  for (char *p = typed; *p; ++p)
    *p = (char)tolower((unsigned char)*p);
  word = strtok_r(typed, " \t", &save);
  if (!word)
    return;
  const char *command = resolve(word);
  arg = strtok_r(NULL, "", &save);
  while (arg && isspace((unsigned char)*arg))
    ++arg;
  if (!command) {
    result(original, "rejected", "unknown command, try help");
    return;
  }
  if (arg && (!strcmp(command, "status") || !strcmp(command, "adc") ||
              !strcmp(command, "positions") || !strcmp(command, "export") ||
              !strcmp(command, "home") || !strcmp(command, "stop") ||
              !strcmp(command, "clearfault") || !strcmp(command, "arm") ||
              !strcmp(command, "disarm") || !strcmp(command, "selftest") ||
              !strcmp(command, "exit"))) {
    result(original, "rejected", "unexpected argument; try help <command>");
    return;
  }
  if (!strcmp(command, "help")) {
    if (arg) {
      const char *detail = help_detail(arg);
      if (detail)
        result(original, "complete", detail);
      else
        result(original, "rejected", "unknown command, try help");
    } else
      result(original, "complete",
             "help:list; status:state; adc:reading; jog:bounded move; step:set "
             "step; sel:select; setpos:save; positions:table; export:defines; "
             "move:position; home:home; stop:brake; clearfault:clear; sim:ADC "
             "simulation; arm/disarm:interlock; drive:pulse; selftest:checks; "
             "cfg:runtime config; exit:leave");
  } else if (!strcmp(command, "status")) {
    char d[96];
    snprintf(d, sizeof d, "state %s pos %u target %u dir %s duty %u adc %u",
             state_text(controller_state()), controller_position(),
             controller_target(), dir_text(motor_direction()), motor_duty(),
             encoder_average());
    result(original, "complete", d);
  } else if (!strcmp(command, "adc")) {
    char d[64];
    position_t p = encoder_instant();
    snprintf(d, sizeof d, "raw %u avg %u pos %s", encoder_raw(),
             encoder_average(),
             p >= 1 && p <= 5 ? (p == 1   ? "1"
                                 : p == 2 ? "2"
                                 : p == 3 ? "3"
                                 : p == 4 ? "4"
                                          : "5")
                              : "?");
    result(original, "complete", d);
  } else if (!strcmp(command, "jog")) {
    if (!parse_long(arg, &value) || labs(value) < JOG_MIN_COUNTS) {
      result(original, "rejected", "below minimum 10 counts");
      return;
    }
    if (labs(value) > JOG_MAX_COUNTS) {
      result(original, "rejected", "above maximum 500 counts");
      return;
    }
    uint16_t from;
    jog_result_t r = controller_request_jog((int16_t)value, &from);
    if (r == JOG_OK) {
      char d[48];
      snprintf(d, sizeof d, "moving from adc %u", from);
      result(original, "accepted", d);
      remember_pending(PENDING_JOG, original);
    } else
      result(original, "rejected",
             r == JOG_ENDSTOP      ? "endpoint outside safe range"
             : r == JOG_OVERTRAVEL ? "current ADC outside safe range"
             : r == JOG_BUSY       ? "controller busy"
             : r == JOG_FAULT      ? "controller fault"
                                   : "invalid jog");
  } else if (!strcmp(command, "step")) {
    if (!parse_long(arg, &value) ||
        (value != 10 && value != 25 && value != 100 && value != 250 &&
         value != 500)) {
      result(original, "rejected", "step must be 10, 25, 100, 250, or 500");
      return;
    }
    jog_step = (uint16_t)value;
    result(original, "complete", "default jog step updated");
  } else if (!strcmp(command, "sel")) {
    if (!parse_long(arg, &value) || value < 1 || value > 5) {
      result(original, "rejected", "station out of range 1-5");
      return;
    }
    selected_station = (position_t)value;
    result(original, "complete", "station selected");
  } else if (!strcmp(command, "setpos")) {
    position_t station = selected_station;
    if (arg) {
      if (!parse_long(arg, &value) || value < 1 || value > 5) {
        result(original, "rejected", "station out of range 1-5");
        return;
      }
      station = (position_t)value;
      selected_station = station;
    }
    save_position(station, original);
  } else if (!strcmp(command, "positions")) {
    char d[112];
    int16_t error =
        (int16_t)encoder_average() - (int16_t)encoder_nominal(selected_station);
    snprintf(d, sizeof d, "1:%u 2:%u 3:%u 4:%u 5:%u measured %u error %+d",
             encoder_nominal(1), encoder_nominal(2), encoder_nominal(3),
             encoder_nominal(4), encoder_nominal(5), encoder_average(), error);
    result(original, "complete", d);
  } else if (!strcmp(command, "export"))
    export_positions(original);
  else if (!strcmp(command, "move")) {
    if (!parse_long(arg, &value) || value < 1 || value > 5) {
      result(original, "rejected", "target out of range 1-5");
      return;
    }
    move_result_t r = controller_request(REQ_MOVE, (position_t)value);
    if (r == MOVE_OK) {
      result(original, "accepted", "moving");
      remember_pending(PENDING_MOVE, original);
    } else
      result(original, "rejected",
             r == MOVE_BUSY          ? "controller busy"
             : r == MOVE_ALREADY     ? "already at target"
             : r == MOVE_POS_UNKNOWN ? "position unknown"
             : r == MOVE_ENDSTOP     ? "at end-stop"
                                     : "controller fault");
  } else if (!strcmp(command, "home")) {
    (void)controller_request(REQ_HOME, 0);
    result(original, "accepted", "homing");
    remember_pending(PENDING_HOME, original);
  } else if (!strcmp(command, "stop")) {
    (void)controller_request(REQ_STOP, 0);
    pending = PENDING_NONE;
    result(original, "complete", "brake requested");
  } else if (!strcmp(command, "clearfault")) {
    dbg_request_t r = {.op = DBG_OP_FAULT_CLEAR};
    if (controller_debug_request(&r))
      result(original, "complete", "fault cleared; position unknown");
    else
      result(original, "rejected", "controller busy");
  } else if (!strcmp(command, "sim")) {
    if (!arg || (strcmp(arg, "on") && strcmp(arg, "off"))) {
      result(original, "rejected", "expected sim on or sim off");
      return;
    }
    dbg_request_t r = {.op = DBG_OP_SIM_ENABLE, .flag = !strcmp(arg, "on")};
    if (controller_debug_request(&r))
      result(original, "complete", r.flag ? "simulation on" : "simulation off");
    else
      result(original, "rejected", "controller busy");
  } else if (!strcmp(command, "arm")) {
    dbg_request_t r = {.op = DBG_OP_ENTER};
    armed = true;
    if (controller_debug_request(&r)) {
      result(original, "complete", "manual drive armed");
    } else {
      armed = false;
      result(original, "rejected", "controller busy or unsafe state");
    }
  } else if (!strcmp(command, "disarm")) {
    dbg_request_t r = {.op = DBG_OP_EXIT};
    armed = false;
    (void)controller_debug_request(&r);
    result(original, "complete", "manual drive disarmed");
  } else if (!strcmp(command, "drive")) {
    char *direction = strtok_r(arg, " \t", &save),
         *duty_text = strtok_r(NULL, " \t", &save),
         *ms_text = strtok_r(NULL, " \t", &save);
    long duty, ms;
    if (!armed) {
      result(original, "rejected", "manual drive is not armed");
      return;
    }
    if (!direction ||
        (!strcmp(direction, "fwd") == 0 && !strcmp(direction, "rev") == 0) ||
        !parse_long(duty_text, &duty) || !parse_long(ms_text, &ms) ||
        duty < 0 || duty > PWM_WRAP || ms < (long)DEBUG_PULSE_MIN_MS ||
        ms > (long)DEBUG_PULSE_MAX_MS) {
      result(original, "rejected",
             "usage: drive fwd|rev <duty 0-255> <ms 10-2000>");
      return;
    }
    dbg_request_t r = {.op = DBG_OP_DRIVE,
                       .dir = !strcmp(direction, "fwd") ? DIR_FWD : DIR_REV,
                       .duty = (uint8_t)duty,
                       .ms = (uint16_t)ms};
    if (controller_debug_request(&r))
      result(original, "accepted", "manual pulse queued");
    else
      result(original, "rejected", "controller busy");
  } else if (!strcmp(command, "selftest"))
    result(original, "complete",
           encoder_raw() <= ADC_MAX_VALUE ? "static checks passed"
                                          : "ADC out of range");
  else if (!strcmp(command, "cfg")) {
    if (!arg) {
      char d[96];
      snprintf(d, sizeof d,
               "DUTY_NORMAL=%u DUTY_APPROACH=%u DUTY_CREEP=%u POS_WINDOW=%u",
               CFG_DUTY_NORMAL, CFG_DUTY_APPROACH, CFG_DUTY_CREEP,
               CFG_POS_WINDOW);
      result(original, "complete", d);
    } else {
      char *key = strtok_r(arg, " \t", &save);
      char *value_text = strtok_r(NULL, " \t", &save);
      for (char *p = key; p && *p; ++p)
        *p = (char)toupper((unsigned char)*p);
      if (!parse_long(value_text, &value) || !cfg_update(key, value))
        result(original, "rejected",
               "unknown key or value violates configuration limits");
      else
        result(original, "complete", "runtime constant updated");
    }
  } else if (!strcmp(command, "exit")) {
    result(original, "complete", "debug interface exited");
    dbg_exit();
  }
}

void dbg_event(event_kind_t kind, uint8_t arg) {
  char detail[64];
  if ((kind == EV_JOG_COMPLETE && pending == PENDING_JOG) ||
      (kind == EV_ARRIVE &&
       (pending == PENDING_MOVE || pending == PENDING_HOME))) {
    snprintf(detail, sizeof detail, "adc %u pos %u", encoder_average(), arg);
    result(pending_text, "complete", detail);
    pending = PENDING_NONE;
    return;
  }
  if (pending != PENDING_NONE &&
      (kind == EV_TIMEOUT || kind == EV_FAULT_HOME ||
       kind == EV_FAULT_OVERTRAVEL || kind == EV_FAULT_STALL ||
       kind == EV_FAULT_DIRECTION)) {
    const char *reason = kind == EV_TIMEOUT            ? "timeout"
                         : kind == EV_FAULT_HOME       ? "home fault"
                         : kind == EV_FAULT_OVERTRAVEL ? "overtravel"
                         : kind == EV_FAULT_STALL      ? "stall"
                                                       : "direction";
    result(pending_text, "failed", reason);
    pending = PENDING_NONE;
    return;
  }
  const char *name = kind == EV_PASS               ? "PASS"
                     : kind == EV_ARRIVE           ? "ARR"
                     : kind == EV_TIMEOUT          ? "ERR timeout"
                     : kind == EV_FAULT_HOME       ? "ERR home fault"
                     : kind == EV_HOMING           ? "homing"
                     : kind == EV_FAULT_OVERTRAVEL ? "ERR overtravel"
                     : kind == EV_FAULT_STALL      ? "ERR stall"
                     : kind == EV_FAULT_DIRECTION  ? "ERR direction"
                                                   : "event";
  snprintf(detail, sizeof detail, "%s%s%u", name,
           (kind == EV_PASS || kind == EV_ARRIVE) ? ":" : "",
           (kind == EV_PASS || kind == EV_ARRIVE) ? arg : 0u);
  dbg_log_push(detail);
}

void dbg_handle_key(char c) {
  if (c == '.') {
    (void)controller_request(REQ_STOP, 0);
    pending = PENDING_NONE;
    result(".", "complete", "immediate brake requested");
    return;
  }
  if (c == 27) {
    input_len = 0;
    input_overflow = false;
    command_line_draw();
    return;
  }
  if (c == '\n' && swallow_lf) {
    swallow_lf = false;
    return;
  }
  if (c != '\n')
    swallow_lf = false;
  if (c == '\r' || c == '\n') {
    swallow_lf = c == '\r';
    if (!input_len && !input_overflow)
      return;
    input[input_len] = '\0';
    if (plain_mode)
      dbg_out_push("\r\n");
    if (input_overflow)
      result(input, "rejected", "line too long");
    else
      submit(input);
    input_len = 0;
    input_overflow = false;
    command_line_draw();
    return;
  }
  if (c == '\b' || c == 127) {
    if (input_len)
      --input_len;
    command_line_draw();
    return;
  }
  if (c >= 32 && c <= 126) {
    if (input_len < DEBUG_COMMAND_MAX)
      input[input_len++] = c;
    else
      input_overflow = true;
    if (plain_mode && echo_enabled) {
      char text[2] = {c, 0};
      dbg_out_push(text);
    } else
      command_line_draw();
  }
}

void dbg_init(void) {
  cfg_reset();
  active = plain_mode = echo_enabled = input_overflow = swallow_lf = armed =
      false;
  selected_station = POS_MIN;
  jog_step = 100u;
  input_len = 0;
  out_head = out_tail = 0u;
  pending = PENDING_NONE;
  auto_enter_deadline = ms_now() + DEBUG_AUTO_ENTER_MS;
  next_refresh = 0u;
  memset(status_shadow, 0, sizeof status_shadow);
}
static void enter(bool plain) {
  plain_mode = plain;
  active = echo_enabled = true;
  input_len = 0;
  input_overflow = false;
  if (!plain)
    dbg_render();
  else
    result("debug", "complete", "command interface entered; type help");
}
void dbg_enter(void) { enter(false); }
void dbg_enter_plain(void) { enter(true); }
void dbg_exit(void) {
  dbg_request_t request = {.op = DBG_OP_EXIT};

  /* Leaving the UI must still hand motor and simulation changes to the tick. */
  (void)controller_debug_request(&request);
  active = echo_enabled = armed = false;
  if (!plain_mode)
    dbg_out_push("\033[1;24r\033[?25h\033[2J\033[H");
}
bool dbg_active(void) { return active; }
bool dbg_plain_mode(void) { return plain_mode; }
bool dbg_auto_enter(char c) {
  if (active || (int32_t)(ms_now() - auto_enter_deadline) >= 0)
    return false;
  enter(false);
  dbg_handle_key(c);
  return true;
}
bool dbg_motor_armed(void) { return armed; }
void dbg_poll(void) {
  uint32_t now = ms_now();
  if (active && !plain_mode && (int32_t)(now - next_refresh) >= 0) {
    dbg_fields_refresh();
    next_refresh = now + DEBUG_REFRESH_MS;
  }
  if (active)
    dbg_out_drain();
}
