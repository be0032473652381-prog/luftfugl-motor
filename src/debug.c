#include "debug.h"

#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/pwm.h"
#include "hardware/uart.h"
#include "motor.h"
#include "pico/time.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMD_MAX 48
#define CMD_ROW 16u
#define DEBUG_REFRESH_MS 1000u

typedef enum {
  PENDING_NONE,
  PENDING_JOG,
  PENDING_SEQUENCE,
  PENDING_MOVE,
  PENDING_HOME,
  PENDING_SIM
} pending_t;

static bool active, plain_mode;
static bool armed;
static position_t selected_station;
static uint16_t jog_step;
static char cmd[CMD_MAX + 1];
static uint8_t cmd_len;
static char out_buf[DEBUG_OUT_BUFFER];
static volatile uint16_t out_head, out_tail;
static uint32_t next_refresh;
static pending_t pending;
static char pending_text[CMD_MAX + 1u];
static char status_shadow[9][81];
static uint8_t frame_phase;
static bool sim_travel_active;
static uint16_t sim_travel_from, sim_travel_to;
static uint32_t sim_travel_started, sim_travel_duration;
static uint8_t findmin_phase, findmin_duty;
static direction_t findmin_direction;
static uint16_t findmin_min, findmin_max, findmin_start, findmin_noise;
static uint32_t findmin_deadline;
static uint16_t seq_target_adc;
static uint8_t seq_step, seq_total;
static char seq_label[16];

#ifdef LUFTFUGL_TRACE_OUTPUT
static uint32_t output_bytes_pushed, output_bytes_dropped, output_bytes_drained;

static void trace_output_snapshot(void) {
  char line[176];
  uint16_t queued =
      out_head >= out_tail ? (uint16_t)(out_head - out_tail)
                           : (uint16_t)(DEBUG_OUT_BUFFER - out_tail + out_head);
  snprintf(line, sizeof line,
           "\r\nOUTBUF pushed=%lu dropped=%lu drained=%lu queued=%u "
           "head=%u tail=%u writable=%u\r\n",
           (unsigned long)output_bytes_pushed,
           (unsigned long)output_bytes_dropped,
           (unsigned long)output_bytes_drained, queued, out_head, out_tail,
           uart_is_writable(uart0) ? 1u : 0u);
  const char *cursor = line;
  while (*cursor)
    uart_putc_raw(uart0, *cursor++);
}
#endif

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
  size_t length = strlen(text);
  if (length > out_free()) {
#ifdef LUFTFUGL_TRACE_OUTPUT
    output_bytes_dropped += (uint32_t)length;
#endif
    return;
  }
  while (*text) {
    uint16_t next = (uint16_t)((out_head + 1u) % DEBUG_OUT_BUFFER);
    out_buf[out_head] = *text++;
    out_head = next;
  }
#ifdef LUFTFUGL_TRACE_OUTPUT
  output_bytes_pushed += (uint32_t)length;
#endif
}

void dbg_out_drain(void) {
  while (out_tail != out_head && uart_is_writable(uart0)) {
    uint32_t started = time_us_32();
    uart_putc_raw(uart0, out_buf[out_tail]);
    console_diag_note_tx_spin(time_us_32() - started);
    out_tail = (uint16_t)((out_tail + 1u) % DEBUG_OUT_BUFFER);
#ifdef LUFTFUGL_TRACE_OUTPUT
    ++output_bytes_drained;
#endif
  }
}

bool dbg_out_pending(void) { return out_tail != out_head; }

static void result(const char *command, const char *outcome,
                   const char *detail) {
  char line[256];
  char message[160];
  uint32_t seconds = ms_now() / 1000u;
  if (!strcmp(outcome, "rejected") || !strcmp(outcome, "failed"))
    snprintf(message, sizeof message, "%s: %s", outcome, detail);
  else
    snprintf(message, sizeof message, "%s", detail);
  if (plain_mode) {
    snprintf(line, sizeof line, " %02lu:%02lu:%02lu  %-12s %s",
             (unsigned long)(seconds / 3600u),
             (unsigned long)((seconds / 60u) % 60u),
             (unsigned long)(seconds % 60u), command, message);
    dbg_out_push(line);
    dbg_out_push("\r\n");
  } else {
    snprintf(line, sizeof line, " %02lu:%02lu:%02lu  %-12.12s %.51s",
             (unsigned long)(seconds / 3600u),
             (unsigned long)((seconds / 60u) % 60u),
             (unsigned long)(seconds % 60u), command, message);
    /* Insert at the top; the terminal shifts older results down one row. */
    dbg_out_push("\033[s\033[18;1H\033[L");
    dbg_out_push(line);
    dbg_out_push("\033[K\033[u");
  }
#ifdef LUFTFUGL_TRACE_OUTPUT
  if (!strcmp(command, "adc") || !strcmp(command, "sel 1"))
    trace_output_snapshot();
#endif
}

void dbg_log_push(const char *text) { result("event", "complete", text); }

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

static void position_text(char *text, size_t size, uint16_t adc) {
  position_t position = encoder_instant();
  if (position >= POS_MIN && position <= POS_MAX) {
    snprintf(text, size, "station %u", position);
    return;
  }
  for (position_t p = POS_MIN; p < POS_MAX; ++p) {
    if (adc > encoder_nominal(p) && adc < encoder_nominal(p + 1u)) {
      snprintf(text, size, "between %u and %u", p, p + 1u);
      return;
    }
  }
  snprintf(text, size, "unknown");
}

static const char *guidance(uint16_t adc) {
  static char text[80];
  if (controller_state() == ST_FAULT)
    return "type \"clearfault\" then \"home\"";
  if (!encoder_in_safe_range())
    return "mechanism is past its limit - jog inward slowly";
  if (selected_station < POS_MIN || selected_station > POS_MAX)
    return "type \"sel 1\" to start setting up station 1";
  int16_t error = (int16_t)encoder_nominal(selected_station) - (int16_t)adc;
  if (error > (int16_t)CFG_POS_WINDOW)
    return "jog forward to reach it";
  if (error < -(int16_t)CFG_POS_WINDOW)
    return "jog back to reach it";
  snprintf(text, sizeof text,
           "you are at station %u - type \"save\" to store it",
           selected_station);
  return text;
}

void dbg_fields_refresh(void) {
  char line[81];
  char position[24];
  uint32_t seconds = ms_now() / 1000u;
  uint16_t adc = encoder_average();
  uint16_t target_adc = controller_target_adc();
  snprintf(line, sizeof line,
           " luftfugl " FW_VERSION
           "                                      up %02lu:%02lu:%02lu",
           (unsigned long)(seconds / 3600u),
           (unsigned long)((seconds / 60u) % 60u),
           (unsigned long)(seconds % 60u));
  field(1, line);
  position_text(position, sizeof position, adc);
  snprintf(line, sizeof line, "  STATE   %-12s  POSITION  %-20s ADC  %u",
           state_text(controller_state()), position, adc);
  field(3, line);
  char target[12] = "--", error[12] = "--";
  if (controller_target() >= POS_MIN && controller_target() <= POS_MAX) {
    snprintf(target, sizeof target, "station %u", controller_target());
    snprintf(error, sizeof error, "%+d", (int16_t)target_adc - (int16_t)adc);
  }
  uint32_t tenths =
      (uint32_t)adc * DEGREE_TENTHS_PER_TURN / ADC_PER_360_DEG;
  snprintf(line, sizeof line,
           "  TARGET   %-12s  ERROR     %-20s ANGLE %lu.%lu deg", target,
           error, (unsigned long)(tenths / 10u),
           (unsigned long)(tenths % 10u));
  field(4, line);
  snprintf(line, sizeof line, "  FAULTS  %-12s  DUTY      %-20u DIR  %s",
           controller_state() == ST_FAULT ? "1" : "0", motor_duty(),
           motor_direction() == DIR_FWD   ? "forward"
           : motor_direction() == DIR_REV ? "back"
                                          : "stopped");
  field(5, line);
  snprintf(line, sizeof line, "  STEP %u  SELECTED %-4s SIM %-3s", jog_step,
           selected_station >= POS_MIN && selected_station <= POS_MAX
               ? (selected_station == 1   ? "1"
                  : selected_station == 2 ? "2"
                  : selected_station == 3 ? "3"
                  : selected_station == 4 ? "4"
                                          : "5")
               : "none",
           encoder_sim_active() ? "on" : "off");
  field(6, line);
  snprintf(line, sizeof line,
           "  STATIONS   1:%u%s  2:%u%s  3:%u%s  4:%u%s  5:%u%s",
           encoder_nominal(1), selected_station == 1 ? " ▶" : "",
           encoder_nominal(2), selected_station == 2 ? " ▶" : "",
           encoder_nominal(3), selected_station == 3 ? " ▶" : "",
           encoder_nominal(4), selected_station == 4 ? " ▶" : "",
           encoder_nominal(5), selected_station == 5 ? " ▶" : "");
  field(8, line);
  snprintf(line, sizeof line, "             %s", guidance(adc));
  field(9, line);
}

static void dbg_out_printf(const char *format, ...) {
  char text[CMD_MAX + 48u];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  dbg_out_push(text);
}

static void cmd_redraw(void) {
  cmd[cmd_len] = '\0';
  dbg_out_printf("\033[s\033[%u;1H Command: %s\033[K\033[u", CMD_ROW, cmd);
}

void dbg_render(void) {
  if (plain_mode)
    return;
  cmd_len = 0u;
  cmd[0] = '\0';
  out_head = out_tail = 0u;
  dbg_out_push("\033[2J\033[H\033[?25l");
  memset(status_shadow, 0, sizeof status_shadow);
  frame_phase = 1u;
}

static bool status_frame_complete(void) {
  static const uint8_t rows[] = {1, 3, 4, 5, 6, 8, 9};
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i)
    if (!status_shadow[rows[i] - 1u][0])
      return false;
  return true;
}

static void frame_continue(void) {
  static const char *const pieces[] = {
      "\033[2;1H---------------------------------------------------------------"
      "----------------",
      "\033[10;1H--------------------------------------------------------------"
      "----------------",
      "\033[11;1H  adc          show reading       jog +100     move forward",
      "\033[12;1H  status       show all state     jog -100     move back",
      "\033[13;1H  sel 3        select station 3   save         store station",
      "\033[14;1H  move 2       go to station 2    help adc     command detail",
      "\033[15;1H--------------------------------------------------------------"
      "----------------",
      "\033[16;1H Command: ",
      "\033[17;1H--------------------------------------------------------------"
      "----------------"};
  if (!frame_phase || out_free() < 120u)
    return;
  if (frame_phase <= sizeof pieces / sizeof pieces[0]) {
    dbg_out_push(pieces[frame_phase - 1u]);
    ++frame_phase;
    return;
  }
  if (!status_frame_complete()) {
    dbg_fields_refresh();
    return;
  }
  /* Scrolling setup is the final sequence of the frame draw. */
  dbg_out_push("\033[18;24r\033[18;1H");
  frame_phase = 0u;
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
  snprintf(detail, sizeof detail, "station %u saved, now %u", station, adc);
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

static void findmin_sample(void) {
  uint16_t adc = encoder_average();
  if (adc < findmin_min)
    findmin_min = adc;
  if (adc > findmin_max)
    findmin_max = adc;
}

static bool findmin_pulse(void) {
  dbg_request_t request = {.op = DBG_OP_DRIVE,
                           .dir = findmin_direction,
                           .duty = findmin_duty,
                           .ms = DEBUG_FINDMIN_PULSE_MS};
  return controller_debug_request(&request);
}

static void findmin_poll(uint32_t now) {
  if (!findmin_phase)
    return;
  if (!encoder_in_safe_range()) {
    (void)controller_request(REQ_STOP, 0);
    result("findmin", "failed", "stopped because the safe range was reached");
    findmin_phase = 0u;
    return;
  }
  findmin_sample();
  if ((int32_t)(now - findmin_deadline) < 0)
    return;
  if (findmin_phase == 1u) {
    findmin_noise = findmin_max - findmin_min;
    findmin_start = encoder_average();
    if (!findmin_pulse())
      return;
    findmin_phase = 2u;
    findmin_deadline = now + DEBUG_FINDMIN_PULSE_MS;
    findmin_min = ADC_MAX_VALUE;
    findmin_max = 0u;
    return;
  }
  uint16_t now_adc = encoder_average();
  uint16_t change = now_adc > findmin_start ? now_adc - findmin_start
                                            : findmin_start - now_adc;
  if (change > findmin_noise) {
    char detail[80];
    uint16_t suggested =
        (uint16_t)((findmin_duty *
                        (DEBUG_PERCENT_SCALE + DEBUG_FINDMIN_MARGIN_PERCENT) +
                    DEBUG_PERCENT_SCALE - 1u) /
                   DEBUG_PERCENT_SCALE);
    snprintf(detail, sizeof detail,
             "lowest moving duty %u; suggested DUTY_MIN %u", findmin_duty,
             suggested);
    result("findmin", "complete", detail);
    findmin_phase = 0u;
    return;
  }
  if (findmin_duty >= DEBUG_FINDMIN_DUTY_MAX) {
    result("findmin", "failed", "no motion found through duty 120");
    findmin_phase = 0u;
    return;
  }
  findmin_duty += DEBUG_FINDMIN_DUTY_STEP;
  findmin_start = now_adc;
  if (!findmin_pulse())
    return;
  findmin_deadline = now + DEBUG_FINDMIN_PULSE_MS;
  findmin_min = ADC_MAX_VALUE;
  findmin_max = 0u;
}

static void export_positions(const char *command) {
  char detail[48];
  for (position_t p = POS_MAX; p >= POS_MIN; --p) {
    snprintf(detail, sizeof detail, "#define POS_%u_ADC %u", p,
             encoder_nominal(p));
    result(p == POS_MIN ? command : "", "complete", detail);
    if (p == POS_MIN)
      break;
  }
}

typedef struct {
  const char *name, *example, *limits, *notes;
} help_entry_t;

static const help_entry_t help_entries[] = {
    {"help", "help jog", "one command name, or none",
     "Shows examples, limits and plain-language notes."},
    {"adc", "adc", "read-only", "Shows raw, filtered and classified sensing."},
    {"status", "status", "read-only", "Shows the full controller state."},
    {"angle", "angle 60", "0 to 360 degrees and inside the safe range",
     "With no number, shows the current angle. Large moves use bounded jogs."},
    {"goto", "goto 1260", "ADC 272 to 2915",
     "Moves in checked steps of 500 counts or less."},
    {"sel", "sel 3", "station 1 to 5",
     "Chooses which station save will update."},
    {"jog", "jog +100", "10 to 500 counts",
     "Creep speed only; 100 counts is roughly 7 degrees."},
    {"step", "step 250", "10, 25, 100, 250 or 500",
     "Changes the suggested calibration step."},
    {"save", "save 3", "station 1 to 5",
     "Without a number, saves the selected station."},
    {"stations", "stations", "read-only",
     "Shows stored readings and difference from now."},
    {"export", "export", "read-only",
     "Prints values ready to paste into config.h."},
    {"move", "move 2", "station 1 to 5", "Uses closed-loop position control."},
    {"home", "home", "no arguments",
     "Returns to station 1 through the guarded home path."},
    {"stop", "stop", "no arguments",
     "Brakes immediately; a period works without Enter."},
    {"faults", "faults", "read-only", "Shows the last fault and counters."},
    {"clearfault", "clearfault", "fault state only",
     "Clears the fault; position becomes unknown."},
    {"selftest", "selftest", "no motion",
     "Checks configuration, ADC and the 1 kHz tick."},
    {"tick", "tick", "read-only", "Shows loop timing and watchdog health."},
    {"pins", "pins", "read-only", "Shows live motor and sensor pin levels."},
    {"pwm", "pwm", "read-only",
     "Shows PWM configuration and calculated frequency."},
    {"cfg", "cfg DUTY_CREEP 30", "validated RAM values",
     "Changes are lost at reset."},
    {"sim", "sim adc 2047", "ADC 0 to 4095",
     "Simulation inhibits physical motor output."},
    {"arm", "arm", "idle controller",
     "Unlocks manual pulses until disarm or exit."},
    {"disarm", "disarm", "no arguments",
     "Brakes and closes the manual interlock."},
    {"drive", "drive fwd 60 200", "duty 0-255, 10-2000 ms",
     "Requires arm; direction is fwd or rev."},
    {"findmin", "findmin", "requires arm",
     "Tests for the lowest duty that produces motion."},
    {"plain", "plain", "no arguments",
     "Switches to line-oriented output without escape codes."},
    {"exit", "exit", "no arguments", "Leaves the debug console safely."}};

static const char *resolve(const char *word, char *candidates,
                           size_t candidates_size) {
  const char *match = NULL;
  size_t n = strlen(word);
  if (candidates && candidates_size)
    candidates[0] = '\0';
  for (size_t i = 0; i < sizeof help_entries / sizeof help_entries[0]; ++i)
    if (!strcmp(help_entries[i].name, word))
      return help_entries[i].name;
  if (!strcmp(word, "st"))
    return "status";
  for (size_t i = 0; i < sizeof help_entries / sizeof help_entries[0]; ++i) {
    if (!strncmp(help_entries[i].name, word, n)) {
      if (candidates && candidates_size) {
        size_t used = strlen(candidates);
        snprintf(candidates + used, candidates_size - used, "%s%s",
                 used ? ", " : "", help_entries[i].name);
      }
      if (match)
        match = "";
      else
        match = help_entries[i].name;
    }
  }
  return match && !*match ? NULL : match;
}

static const help_entry_t *help_detail(const char *word) {
  const char *command = resolve(word, NULL, 0u);
  if (!command)
    return NULL;
  for (size_t i = 0; i < sizeof help_entries / sizeof help_entries[0]; ++i)
    if (!strcmp(help_entries[i].name, command))
      return &help_entries[i];
  return NULL;
}

static bool sequence_step(void) {
  int32_t remaining = (int32_t)seq_target_adc - (int32_t)encoder_average();
  if (remaining > JOG_MAX_COUNTS)
    remaining = JOG_MAX_COUNTS;
  else if (remaining < -JOG_MAX_COUNTS)
    remaining = -JOG_MAX_COUNTS;
  uint16_t from;
  if (controller_request_jog((int16_t)remaining, &from) != JOG_OK)
    return false;
  pending = PENDING_SEQUENCE;
  return true;
}

static void sequence_start(uint16_t target, const char *label) {
  uint16_t current = encoder_average();
  uint16_t distance = current > target ? current - target : target - current;
  if (target < CFG_ADC_SAFE_MIN || target > CFG_ADC_SAFE_MAX) {
    char detail[80];
    snprintf(detail, sizeof detail, "%u is outside the safe range %u to %u",
             target, CFG_ADC_SAFE_MIN, CFG_ADC_SAFE_MAX);
    result(label, "rejected", detail);
    return;
  }
  if (distance < JOG_MIN_COUNTS) {
    char detail[64];
    snprintf(detail, sizeof detail, "already within 10 counts of %u", target);
    result(label, "rejected", detail);
    return;
  }
  seq_target_adc = target;
  seq_step = 0u;
  seq_total = (uint8_t)((distance + JOG_MAX_COUNTS - 1u) / JOG_MAX_COUNTS);
  strncpy(seq_label, label, sizeof seq_label - 1u);
  seq_label[sizeof seq_label - 1u] = '\0';
  char detail[80];
  snprintf(detail, sizeof detail, "moving %u counts %s in %u steps", distance,
           target > current ? "forward" : "back", seq_total);
  result(label, "accepted", detail);
  if (!sequence_step()) {
    pending = PENDING_NONE;
    result(label, "rejected", "controller busy or unsafe state");
  }
}

static bool parse_angle_tenths(const char *text, uint16_t *tenths) {
  char *end;
  if (!text || !*text)
    return false;
  unsigned long whole = strtoul(text, &end, 10);
  unsigned long fraction = 0u;
  if (*end == '.') {
    ++end;
    if (!isdigit((unsigned char)*end))
      return false;
    fraction = (unsigned long)(*end++ - '0');
  }
  while (isspace((unsigned char)*end))
    ++end;
  if (*end || whole > 360u || (whole == 360u && fraction != 0u))
    return false;
  *tenths = (uint16_t)(whole * 10u + fraction);
  return true;
}

static void submit(char *typed) {
  char original[CMD_MAX + 1u], candidates[96], *arg, *word, *save;
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
  const char *command = resolve(word, candidates, sizeof candidates);
  arg = strtok_r(NULL, "", &save);
  while (arg && isspace((unsigned char)*arg))
    ++arg;
  if (!command) {
    char detail[160];
    if (candidates[0])
      snprintf(detail, sizeof detail, "\"%s\" matches %s", word, candidates);
    else
      snprintf(detail, sizeof detail, "no command called \"%s\", try \"help\"",
               word);
    result(original, "rejected", detail);
    return;
  }
  if (arg && (!strcmp(command, "status") || !strcmp(command, "adc") ||
              !strcmp(command, "stations") || !strcmp(command, "export") ||
              !strcmp(command, "home") || !strcmp(command, "stop") ||
              !strcmp(command, "clearfault") || !strcmp(command, "arm") ||
              !strcmp(command, "disarm") || !strcmp(command, "selftest") ||
              !strcmp(command, "faults") || !strcmp(command, "tick") ||
              !strcmp(command, "pins") || !strcmp(command, "pwm") ||
              !strcmp(command, "findmin") || !strcmp(command, "plain") ||
              !strcmp(command, "exit"))) {
    result(original, "rejected", "unexpected argument; try help <command>");
    return;
  }
  if (!strcmp(command, "help")) {
    if (arg) {
      const help_entry_t *entry = help_detail(arg);
      if (entry) {
        result("Notes", "complete", entry->notes);
        result("Limits", "complete", entry->limits);
        result("Examples", "complete", entry->example);
        result(original, "complete", entry->name);
      } else {
        char detail[96];
        snprintf(detail, sizeof detail,
                 "no command called \"%s\", try \"help\"", arg);
        result(original, "rejected", detail);
      }
    } else {
      for (size_t i = sizeof help_entries / sizeof help_entries[0]; i-- > 0;)
        result(help_entries[i].name, "complete", help_entries[i].example);
    }
  } else if (!strcmp(command, "status")) {
    char d[128];
    snprintf(d, sizeof d,
             "state %s pos %u target %u dir %s duty %u adc %u rx-coupled %lu",
             state_text(controller_state()), controller_position(),
             controller_target(), dir_text(motor_direction()), motor_duty(),
             encoder_average(),
             (unsigned long)console_renderer_rx_discards());
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
  } else if (!strcmp(command, "angle")) {
    if (!arg) {
      uint32_t tenths =
          (uint32_t)encoder_average() * DEGREE_TENTHS_PER_TURN /
          ADC_PER_360_DEG;
      char d[64];
      snprintf(d, sizeof d, "now at %lu.%lu degrees, ADC %u",
               (unsigned long)(tenths / 10u), (unsigned long)(tenths % 10u),
               encoder_average());
      result(original, "complete", d);
    } else {
      uint16_t tenths;
      if (!parse_angle_tenths(arg, &tenths)) {
        result(original, "rejected", "angle is outside 0 to 360 degrees");
        return;
      }
      uint16_t target = (uint16_t)((uint32_t)tenths * ADC_PER_360_DEG /
                                   DEGREE_TENTHS_PER_TURN);
      if (target < CFG_ADC_SAFE_MIN || target > CFG_ADC_SAFE_MAX) {
        char d[96];
        snprintf(d, sizeof d,
                 "%s degrees is ADC %u, outside the safe range %u to %u", arg,
                 target, CFG_ADC_SAFE_MIN, CFG_ADC_SAFE_MAX);
        result(original, "rejected", d);
      } else
        sequence_start(target, original);
    }
  } else if (!strcmp(command, "goto")) {
    if (!arg) {
      char d[48];
      snprintf(d, sizeof d, "current reading %u", encoder_average());
      result(original, "complete", d);
    } else if (!parse_long(arg, &value) || value < 0 ||
               value > (long)ADC_MAX_VALUE)
      result(original, "rejected", "ADC must be from 0 to 4095");
    else
      sequence_start((uint16_t)value, original);
  } else if (!strcmp(command, "jog")) {
    if (arg && !strcmp(arg, "+"))
      value = jog_step;
    else if (arg && !strcmp(arg, "-"))
      value = -(long)jog_step;
    else if (!parse_long(arg, &value)) {
      result(original, "rejected", "type a distance, for example \"jog +100\"");
      return;
    }
    if (labs(value) < JOG_MIN_COUNTS) {
      result(original, "rejected", "below minimum 10 counts; try \"jog +10\"");
      return;
    }
    if (labs(value) > JOG_MAX_COUNTS) {
      char detail[80];
      snprintf(detail, sizeof detail, "%ld is too far, the most is 500",
               labs(value));
      result(original, "rejected", detail);
      return;
    }
    uint16_t from;
    jog_result_t r = controller_request_jog((int16_t)value, &from);
    if (r == JOG_OK) {
      (void)from;
      result(original, "accepted", "moving...");
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
    char detail[64];
    snprintf(detail, sizeof detail, "station %ld selected, stored value %u",
             value, encoder_nominal((position_t)value));
    result(original, "complete", detail);
  } else if (!strcmp(command, "save")) {
    position_t station = selected_station;
    if (arg) {
      if (!parse_long(arg, &value) || value < 1 || value > 5) {
        result(original, "rejected", "station out of range 1-5");
        return;
      }
      station = (position_t)value;
      selected_station = station;
    }
    if (station < POS_MIN || station > POS_MAX) {
      result(original, "rejected",
             "select a station first, for example \"sel 1\"");
      return;
    }
    save_position(station, original);
  } else if (!strcmp(command, "stations")) {
    char d[112];
    if (selected_station >= POS_MIN && selected_station <= POS_MAX) {
      int16_t error = (int16_t)encoder_average() -
                      (int16_t)encoder_nominal(selected_station);
      snprintf(d, sizeof d,
               "1:%u 2:%u 3:%u 4:%u 5:%u; selected %u, now %u, difference %+d",
               encoder_nominal(1), encoder_nominal(2), encoder_nominal(3),
               encoder_nominal(4), encoder_nominal(5), selected_station,
               encoder_average(), error);
    } else
      snprintf(d, sizeof d, "1:%u 2:%u 3:%u 4:%u 5:%u; selected none, now %u",
               encoder_nominal(1), encoder_nominal(2), encoder_nominal(3),
               encoder_nominal(4), encoder_nominal(5), encoder_average());
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
    sim_travel_active = false;
    findmin_phase = 0u;
    armed = false;
    result(original, "complete", "brake requested");
  } else if (!strcmp(command, "clearfault")) {
    dbg_request_t r = {.op = DBG_OP_FAULT_CLEAR};
    if (controller_state() != ST_FAULT)
      result(original, "rejected", "there is no fault to clear");
    else if (controller_debug_request(&r))
      result(original, "complete", "fault cleared; position unknown");
    else
      result(original, "rejected", "controller busy");
  } else if (!strcmp(command, "faults")) {
    fault_record_t fault;
    dbg_counters_t counters;
    char detail[128];
    controller_fault_get(&fault);
    controller_counters_get(&counters);
    if (!counters.faults)
      snprintf(detail, sizeof detail,
               "no faults; timeouts %lu, limit rejects %lu",
               (unsigned long)counters.moves_timeout,
               (unsigned long)counters.limit_rejects);
    else
      snprintf(detail, sizeof detail,
               "last fault event %u at %lums; faults %lu, timeouts %lu, limit "
               "rejects %lu",
               fault.kind, (unsigned long)fault.ms,
               (unsigned long)counters.faults,
               (unsigned long)counters.moves_timeout,
               (unsigned long)counters.limit_rejects);
    result(original, "complete", detail);
  } else if (!strcmp(command, "tick")) {
    tick_stats_t timing;
    char detail[112];
    controller_timing_get(&timing);
    uint32_t average =
        timing.count ? (uint32_t)(timing.sum_us / timing.count) : 0u;
    snprintf(detail, sizeof detail,
             "1 kHz tick alive; min %lu us, average %lu us, max %lu us, "
             "overruns %lu; watchdog 100 ms",
             (unsigned long)timing.min_us, (unsigned long)average,
             (unsigned long)timing.max_us, (unsigned long)timing.overruns);
    result(original, "complete", detail);
  } else if (!strcmp(command, "pins")) {
    char detail[112];
    snprintf(
        detail, sizeof detail,
        "AIN1 GP%u=%u AIN2 GP%u=%u PWMA GP%u=%u STBY GP%u=%u SENSE GP%u=%u",
        PIN_AIN1, gpio_get(PIN_AIN1), PIN_AIN2, gpio_get(PIN_AIN2), PIN_PWMA,
        gpio_get(PIN_PWMA), PIN_STBY, gpio_get(PIN_STBY), PIN_SENSE,
        gpio_get(PIN_SENSE));
    result(original, "complete", detail);
  } else if (!strcmp(command, "pwm")) {
    uint slice = pwm_gpio_to_slice_num(PIN_PWMA);
    uint32_t div16 = pwm_hw->slice[slice].div & DEBUG_PWM_DIV_MASK;
    uint16_t wrap = pwm_hw->slice[slice].top;
    uint32_t frequency = div16
                             ? (clock_get_hz(clk_sys) * DEBUG_PWM_FIXED_SCALE) /
                                   (div16 * (wrap + 1u))
                             : 0u;
    char detail[96];
    snprintf(detail, sizeof detail,
             "slice %u wrap %u divider %lu/16 calculated %lu Hz duty %u", slice,
             wrap, (unsigned long)div16, (unsigned long)frequency,
             motor_duty());
    result(original, "complete", detail);
  } else if (!strcmp(command, "sim")) {
    char *sub = strtok_r(arg, " \t", &save);
    if (sub && (!strcmp(sub, "on") || !strcmp(sub, "off"))) {
      dbg_request_t r = {.op = DBG_OP_SIM_ENABLE, .flag = !strcmp(sub, "on")};
      if (controller_debug_request(&r))
        result(original, "complete",
               r.flag ? "simulation on; motor inhibited" : "simulation off");
      else
        result(original, "rejected", "controller is busy; type \"stop\" first");
    } else if (sub && !strcmp(sub, "adc")) {
      char *adc_text = strtok_r(NULL, " \t", &save);
      if (!encoder_sim_active())
        result(original, "rejected",
               "simulation is off; type \"sim on\" first");
      else if (!parse_long(adc_text, &value) || value < 0 ||
               value > (long)ADC_MAX_VALUE)
        result(original, "rejected", "ADC must be from 0 to 4095");
      else {
        dbg_request_t r = {.op = DBG_OP_SIM_SET, .adc = (uint16_t)value};
        if (controller_debug_request(&r))
          result(original, "complete", "simulated reading updated");
        else
          result(original, "rejected", "controller is busy; try again");
      }
    } else if (sub && !strcmp(sub, "travel")) {
      char *from_text = strtok_r(NULL, " \t", &save);
      char *to_text = strtok_r(NULL, " \t", &save);
      char *ms_text = strtok_r(NULL, " \t", &save);
      long from, to, ms;
      if (!encoder_sim_active())
        result(original, "rejected",
               "simulation is off; type \"sim on\" first");
      else if (!parse_long(from_text, &from) || !parse_long(to_text, &to) ||
               !parse_long(ms_text, &ms) || from < 1 || from > 5 || to < 1 ||
               to > 5 || from == to || ms < 1 || ms > 10000)
        result(original, "rejected", "try \"sim travel 1 5 300\"");
      else {
        sim_travel_from = encoder_nominal((position_t)from);
        sim_travel_to = encoder_nominal((position_t)to);
        sim_travel_duration = (uint32_t)(labs(to - from) * ms);
        sim_travel_started = ms_now();
        sim_travel_active = true;
        remember_pending(PENDING_SIM, original);
        result(original, "accepted", "simulated travel running...");
      }
    } else
      result(original, "rejected",
             "try \"sim on\", \"sim adc 2047\" or \"sim travel 1 5 300\"");
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
  } else if (!strcmp(command, "findmin")) {
    if (!armed)
      result(original, "rejected",
             "manual drive is not armed; type \"arm\" first");
    else if (!encoder_in_safe_range())
      result(original, "rejected", "mechanism is outside the safe range");
    else {
      findmin_phase = 1u;
      findmin_direction =
          encoder_average() >
                  (uint16_t)((CFG_ADC_SAFE_MIN + CFG_ADC_SAFE_MAX) / 2u)
              ? DIR_REV
              : DIR_FWD;
      findmin_duty = CFG_DUTY_MIN > DEBUG_FINDMIN_DUTY_OFFSET
                         ? CFG_DUTY_MIN - DEBUG_FINDMIN_DUTY_OFFSET
                         : 0u;
      findmin_deadline = ms_now() + DEBUG_FINDMIN_PULSE_MS;
      findmin_min = ADC_MAX_VALUE;
      findmin_max = 0u;
      result(original, "accepted",
             findmin_direction == DIR_FWD
                 ? "measuring noise, then testing inward forward pulses..."
                 : "measuring noise, then testing inward back pulses...");
    }
  } else if (!strcmp(command, "selftest")) {
    tick_stats_t timing;
    controller_timing_get(&timing);
    bool ordered = true;
    for (position_t p = POS_MIN; p < POS_MAX; ++p)
      if (encoder_nominal(p) >= encoder_nominal(p + 1u))
        ordered = false;
    bool duties = CFG_DUTY_MIN <= CFG_DUTY_CREEP &&
                  CFG_DUTY_CREEP <= CFG_DUTY_APPROACH &&
                  CFG_DUTY_APPROACH <= CFG_DUTY_NORMAL;
    bool pass = encoder_raw() <= ADC_MAX_VALUE && ordered && duties &&
                CFG_TIMEOUT_HOME_MS >= 4u * CFG_TIMEOUT_STEP_MS &&
                timing.count != 0u;
    result(
        original, pass ? "complete" : "failed",
        pass ? "static checks passed: ADC, stations, duties, timeouts and tick"
             : "a static check failed; use adc, stations, cfg and tick to "
               "inspect it");
  } else if (!strcmp(command, "cfg")) {
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
  } else if (!strcmp(command, "plain")) {
    if (!plain_mode)
      dbg_out_push("\033[1;24r\033[?25h\033[2J\033[H");
    plain_mode = true;
    result(original, "complete", "line-oriented mode; type \"exit\" to leave");
  } else if (!strcmp(command, "exit")) {
    result(original, "complete", "debug interface exited");
    dbg_exit();
  }
}

void dbg_event(event_kind_t kind, uint8_t arg) {
  char detail[64];
  if (kind == EV_JOG_COMPLETE && pending == PENDING_SEQUENCE) {
    ++seq_step;
    snprintf(detail, sizeof detail, "step %u of %u, now at %u", seq_step,
             seq_total, encoder_average());
    result(seq_label, "complete", detail);
    if (seq_step < seq_total && sequence_step())
      return;
    if (seq_step == seq_total) {
      uint16_t now = encoder_average();
      uint16_t error = now > seq_target_adc ? now - seq_target_adc
                                            : seq_target_adc - now;
      snprintf(detail, sizeof detail, "done, now at %u, %u counts from target",
               now, error);
      result(seq_label, "complete", detail);
    } else
      result(seq_label, "failed", "sequence cancelled; controller unavailable");
    pending = PENDING_NONE;
    return;
  }
  if ((kind == EV_JOG_COMPLETE && pending == PENDING_JOG) ||
      (kind == EV_ARRIVE &&
       (pending == PENDING_MOVE || pending == PENDING_HOME))) {
    snprintf(detail, sizeof detail, "done, now at %u", encoder_average());
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
    result(pending == PENDING_SEQUENCE ? seq_label : pending_text, "failed",
           reason);
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
                     : kind == EV_STOPPED_UNKNOWN  ? "stopped, position unknown"
                                                   : "event";
  snprintf(detail, sizeof detail, "%s%s%u", name,
           (kind == EV_PASS || kind == EV_ARRIVE) ? ":" : "",
           (kind == EV_PASS || kind == EV_ARRIVE) ? arg : 0u);
  dbg_log_push(detail);
}

static void dbg_execute(char *line) { submit(line); }

void dbg_input_char(char c) {
  if (c == '\r' || c == '\n') {
    if (cmd_len) {
      cmd[cmd_len] = '\0';
      dbg_execute(cmd);
      cmd_len = 0;
    }
    cmd_redraw();
    return;
  }
  if (c == '\b' || c == 0x7f) {
    if (cmd_len) {
      cmd_len--;
      cmd_redraw();
    }
    return;
  }
  if (c < 0x20 || c > 0x7e) { cmd_len = 0; cmd_redraw(); return; }
  if (cmd_len >= CMD_MAX) { cmd_len = 0; cmd_redraw(); return; }
  cmd[cmd_len++] = c;
  cmd_redraw();
}

void dbg_init(void) {
  cfg_reset();
  active = plain_mode = armed = false;
  selected_station = POS_UNKNOWN;
  jog_step = 100u;
  cmd_len = 0;
  out_head = out_tail = 0u;
  pending = PENDING_NONE;
  sim_travel_active = false;
  findmin_phase = 0u;
  frame_phase = 0u;
  next_refresh = 0u;
  memset(status_shadow, 0, sizeof status_shadow);
}
static void enter(bool plain) {
  plain_mode = plain;
  active = true;
  cmd_len = 0;
  cmd[0] = '\0';
  if (!plain)
    dbg_render();
  else
    dbg_out_push(" luftfugl debug; type help\r\n");
}
void dbg_enter(void) { enter(false); }
void dbg_enter_plain(void) { enter(true); }
void dbg_exit(void) {
  dbg_request_t request = {.op = DBG_OP_EXIT};

  /* Leaving the UI must still hand motor and simulation changes to the tick. */
  (void)controller_debug_request(&request);
  active = armed = false;
  sim_travel_active = false;
  findmin_phase = 0u;
  if (!plain_mode)
    dbg_out_push("\033[1;24r\033[?25h\033[2J\033[H");
}
bool dbg_active(void) { return active; }
bool dbg_plain_mode(void) { return plain_mode; }
bool dbg_motor_armed(void) { return armed; }
void dbg_poll(void) {
  uint32_t now = ms_now();
  findmin_poll(now);
  if (sim_travel_active) {
    uint32_t elapsed = now - sim_travel_started;
    uint16_t adc;
    if (elapsed >= sim_travel_duration) {
      adc = sim_travel_to;
      sim_travel_active = false;
    } else {
      int32_t span = (int32_t)sim_travel_to - (int32_t)sim_travel_from;
      adc =
          (uint16_t)((int32_t)sim_travel_from +
                     (span * (int32_t)elapsed) / (int32_t)sim_travel_duration);
    }
    dbg_request_t request = {.op = DBG_OP_SIM_SET, .adc = adc};
    if (!controller_debug_request(&request) && !sim_travel_active)
      sim_travel_active = true;
    else if (!sim_travel_active && pending == PENDING_SIM) {
      char detail[48];
      snprintf(detail, sizeof detail, "done, now at %u", adc);
      result(pending_text, "complete", detail);
      pending = PENDING_NONE;
    }
  }
  if (active && !plain_mode && frame_phase)
    frame_continue();
  if (active && !plain_mode && !frame_phase &&
      (int32_t)(now - next_refresh) >= 0) {
    dbg_fields_refresh();
    next_refresh = now + DEBUG_REFRESH_MS;
  }
  if (active)
    dbg_out_drain();
}
