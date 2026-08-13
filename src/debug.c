#include "debug.h"

#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/pwm.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "motor.h"
#include "led.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_COMMAND_MAX 48u
#define DEBUG_REFRESH_MS 1000u

typedef enum {
  PENDING_NONE,
  PENDING_JOG,
  PENDING_MOVE,
  PENDING_GOTO,
  PENDING_HOME,
  PENDING_SIM
} pending_t;

static bool active, plain_mode, echo_enabled, input_overflow, swallow_lf;
static bool command_dirty;
static bool armed;
static position_t selected_station;
static uint8_t saved_station_mask;
static uint16_t jog_step;
static char input[DEBUG_COMMAND_MAX + 1u];
static uint8_t input_len;
static char out_buf[DEBUG_OUT_BUFFER];
static volatile uint16_t out_head, out_tail;
static uint32_t next_refresh;
static pending_t pending;
static uint16_t pending_target_adc;
static char pending_text[DEBUG_COMMAND_MAX + 1u];
static char status_shadow[10][81];
static uint8_t frame_phase;
static bool frame_measuring;
static uint32_t frame_bytes_current, frame_bytes_last, frame_draw_count;
static uint16_t field_bytes_last;
static bool first_result;
static bool sim_travel_active;
static uint16_t sim_travel_from, sim_travel_to;
static uint32_t sim_travel_started, sim_travel_duration;
static uint8_t findmin_phase, findmin_duty;
static direction_t findmin_direction;
static uint16_t findmin_min, findmin_max, findmin_start, findmin_noise;
static uint32_t findmin_deadline;
static uint8_t trace_dump_index, trace_dump_count;

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
           uart_is_writable(uart1) ? 1u : 0u);
  const char *cursor = line;
  while (*cursor)
    uart_putc_raw(uart1, *cursor++);
}
#endif

#ifdef LUFTFUGL_TRACE_INPUT
static void trace_raw(const char *text) {
  while (*text)
    uart_putc_raw(uart1, *text++);
}

static void trace_char(char *text, size_t size, char c) {
  unsigned char byte = (unsigned char)c;
  if (c == '\r')
    snprintf(text, size, "\\r");
  else if (c == '\n')
    snprintf(text, size, "\\n");
  else if (c == '\t')
    snprintf(text, size, "\\t");
  else if (c == '\\')
    snprintf(text, size, "\\\\");
  else if (c == '\'')
    snprintf(text, size, "\\\'");
  else if (byte >= 32u && byte <= 126u)
    snprintf(text, size, "%c", c);
  else
    snprintf(text, size, "\\x%02x", byte);
}

void dbg_trace_input_in(char c) {
  char printable[8], line[112];
  trace_char(printable, sizeof printable, c);
  /* The current console has no prompt or asynchronous action enum. */
  snprintf(line, sizeof line,
           "\r\nIN  0x%02x '%s' active=%u plain=%u prompt=0 len=%u action=0\r\n",
           (unsigned char)c, printable, active ? 1u : 0u,
           plain_mode ? 1u : 0u, input_len);
  trace_raw(line);
}

void dbg_trace_input_out(char c, const char *consumed_by,
                         const char *submitted) {
  char printable[8], line[160];
  trace_char(printable, sizeof printable, c);
  if (submitted)
    snprintf(line, sizeof line,
             "\r\nOUT 0x%02x '%s' consumed_by=%s line=\"%s\"\r\n",
             (unsigned char)c, printable, consumed_by, submitted);
  else
    snprintf(line, sizeof line,
             "\r\nOUT 0x%02x '%s' consumed_by=%s len=%u\r\n",
             (unsigned char)c, printable, consumed_by, input_len);
  trace_raw(line);
}

static void trace_dispatch(const char *line, const char *command) {
  char output[144];
  if (command)
    snprintf(output, sizeof output,
             "\r\nDISPATCH line=\"%s\" matched=YES handler=cmd_%s\r\n",
             line, command);
  else
    snprintf(output, sizeof output,
             "\r\nDISPATCH line=\"%s\" matched=NO\r\n", line);
  trace_raw(output);
}

static void trace_result(const char *message) {
  char output[192];
  snprintf(output, sizeof output, "\r\nRESULT   \"%s\"\r\n", message);
  trace_raw(output);
}
#endif

static uint32_t ms_now(void) { return to_ms_since_boot(get_absolute_time()); }
static void print_angle(char *text, size_t size, uint16_t counts);
static const char *state_text(sys_state_t state) {
  static const char *const names[] = {"BOOT", "IDLE", "MOVING", "APPROACH",
                                      "HOMING", "DEBUG"};
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
  if (frame_measuring)
    frame_bytes_current += (uint32_t)length;
#ifdef LUFTFUGL_TRACE_OUTPUT
  output_bytes_pushed += (uint32_t)length;
#endif
}

void dbg_out_drain(void) {
  while (out_tail != out_head && uart_is_writable(uart1)) {
    uint32_t started = time_us_32();
    console_diag_note_debug_tx();
    uart_putc_raw(uart1, out_buf[out_tail]);
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
#ifdef LUFTFUGL_TRACE_INPUT
  trace_result(message);
#endif
  if (plain_mode) {
    snprintf(line, sizeof line, " %02lu:%02lu:%02lu  %-12s %s",
             (unsigned long)(seconds / 3600u),
             (unsigned long)((seconds / 60u) % 60u),
             (unsigned long)(seconds % 60u), command, message);
    dbg_out_push(line);
    dbg_out_push("\r\n");
  } else {
    if (first_result) {
      dbg_out_push("\033[s\033[24;1H\r\n\r\n\033[u");
      first_result = false;
    }
    snprintf(line, sizeof line, "  %02lu:%02lu:%02lu  %-11.11s%s",
             (unsigned long)(seconds / 3600u),
             (unsigned long)((seconds / 60u) % 60u),
             (unsigned long)(seconds % 60u), command, message);
    /* Insert at the top; the terminal shifts older results down one row. */
    dbg_out_push("\033[s\033[24;1H\033[L");
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
  field_bytes_last = (uint16_t)(strlen(esc) + strlen(text) + 8u);
  strncpy(status_shadow[row - 1u], text, 80u);
  status_shadow[row - 1u][80] = '\0';
}

static const char *guidance(uint16_t adc) {
  static char text[80];
  position_t target = controller_target();
  if (controller_state() == ST_MOVING || controller_state() == ST_APPROACH ||
      controller_state() == ST_HOMING) {
    uint16_t target_adc = controller_target_adc();
    uint16_t distance = adc > target_adc ? adc - target_adc : target_adc - adc;
    if (target >= POS_MIN && target <= POS_MAX)
      snprintf(text, sizeof text, "moving to station %u — %u counts to go", target,
               distance);
    else
      snprintf(text, sizeof text, "moving to adc %u, %u counts to go", target_adc,
               distance);
    return text;
  }
  if (saved_station_mask == 0x1fu)
    return "type \"export\" and copy the lines into config.h";
  if (selected_station < POS_MIN || selected_station > POS_MAX)
    return "type \"sel 1\" to start setting up station 1";
  int16_t error = (int16_t)encoder_nominal(selected_station) - (int16_t)adc;
  if (error > (int16_t)CFG_POS_WINDOW || error < -(int16_t)CFG_POS_WINDOW) {
    uint16_t distance = error < 0 ? (uint16_t)-error : (uint16_t)error;
    snprintf(text, sizeof text, "jog toward station %u — %u counts to go",
             selected_station, distance);
    return text;
  }
  snprintf(text, sizeof text, "at station %u — type \"save\" to store it",
           selected_station);
  return text;
}

void dbg_fields_refresh(void) {
  char line[81];
  char target[12] = "--", error[12] = "--";
  char adc_text[8], angle_text[12], angle_value[8], duty[8], step[8];
  char selected[16];
  char direction[12];
  uint32_t seconds = ms_now() / 1000u;
  uint16_t adc = encoder_average();
  uint16_t target_adc = controller_target_adc();
  snprintf(line, sizeof line, " luftfugl 2.0%52sup %02lu:%02lu:%02lu", "",
           (unsigned long)(seconds / 3600u),
           (unsigned long)((seconds / 60u) % 60u),
           (unsigned long)(seconds % 60u));
  field(1, line);
  print_angle(angle_value, sizeof angle_value, adc);
  snprintf(angle_text, sizeof angle_text, "%s deg", angle_value);
  snprintf(adc_text, sizeof adc_text, "%u", adc);
  snprintf(duty, sizeof duty, "%u", motor_duty());
  snprintf(step, sizeof step, "%u", jog_step);
  snprintf(direction, sizeof direction, "%s",
           motor_direction() == DIR_FWD   ? "forward"
           : motor_direction() == DIR_REV ? "back"
                                          : "stopped");
  if (controller_target() >= POS_MIN && controller_target() <= POS_MAX) {
    snprintf(target, sizeof target, "station %u", controller_target());
    snprintf(error, sizeof error, "%+d", (int16_t)target_adc - (int16_t)adc);
  }
  snprintf(selected, sizeof selected, "%s",
           selected_station >= POS_MIN && selected_station <= POS_MAX ?
               (selected_station == 1   ? "station 1"
                : selected_station == 2 ? "station 2"
                : selected_station == 3 ? "station 3"
                : selected_station == 4 ? "station 4"
                                        : "station 5")
                                                                      : "none");
  snprintf(line, sizeof line, "  %-9s%-14s%-9s%-14s%-9s%-14s",
           "STATE", state_text(controller_state()), "TARGET", target,
           "DIR", direction);
  field(3, line);
  snprintf(line, sizeof line, "  %-9s%-14s%-9s%-14s%-9s%-14s",
           "ADC", adc_text, "ANGLE", angle_text,
           "DUTY", duty);
  field(4, line);
  snprintf(line, sizeof line, "  %-9s%-14s%-9s%-14s%-9s%-14s",
           "ERROR", error, "STEP", step, "SELECTED", selected);
  field(5, line);
  char s1[12], s2[12], s3[12], s4[12], s5[12];
  snprintf(s1, sizeof s1, "1:%5u", encoder_nominal(1));
  snprintf(s2, sizeof s2, "2:%5u", encoder_nominal(2));
  snprintf(s3, sizeof s3, "3:%5u", encoder_nominal(3));
  snprintf(s4, sizeof s4, "4:%5u", encoder_nominal(4));
  snprintf(s5, sizeof s5, "5:%5u", encoder_nominal(5));
  snprintf(line, sizeof line, "  %-15.15s%-15.15s%-15.15s%-15.15s%-15.15s",
           s1, s2, s3, s4, s5);
  field(7, line);
  char a1[12], a2[12], a3[12], a4[12], a5[12], value[8];
  print_angle(value, sizeof value, encoder_nominal(1));
  snprintf(a1, sizeof a1, "%s deg", value);
  print_angle(value, sizeof value, encoder_nominal(2));
  snprintf(a2, sizeof a2, "%s deg", value);
  print_angle(value, sizeof value, encoder_nominal(3));
  snprintf(a3, sizeof a3, "%s deg", value);
  print_angle(value, sizeof value, encoder_nominal(4));
  snprintf(a4, sizeof a4, "%s deg", value);
  print_angle(value, sizeof value, encoder_nominal(5));
  snprintf(a5, sizeof a5, "%s deg", value);
  snprintf(line, sizeof line, "  %-15s%-15s%-15s%-15s%-15s",
           a1, a2, a3, a4, a5);
  field(8, line);
  snprintf(line, sizeof line, "  ▸ %s", guidance(adc));
  field(10, line);
}

#ifndef LUFTFUGL_TRACE_INPUT
static void command_line_draw(void) {
  char line[80];
  if (plain_mode)
    return;
  snprintf(line, sizeof line, " ▶ %s", input);
  dbg_out_push("\033[s\033[22;1H");
  dbg_out_push(line);
  dbg_out_push("\033[K\033[u");
}
#endif

void dbg_render(void) {
  if (plain_mode)
    return;
  out_head = out_tail = 0u;
  frame_bytes_current = 0u;
  frame_measuring = true;
  first_result = true;
  dbg_out_push("\033[2J\033[H\033[?25l");
  memset(status_shadow, 0, sizeof status_shadow);
  frame_phase = 1u;
}

#ifndef LUFTFUGL_TRACE_INPUT
static bool status_frame_complete(void) {
  static const uint8_t rows[] = {1, 3, 4, 5, 7, 8, 10};
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i)
    if (!status_shadow[rows[i] - 1u][0])
      return false;
  return true;
}

static void frame_continue(void) {
  static const uint8_t rows[] = {2, 11, 12, 13, 14, 15, 16, 17,
                                 18, 19, 20, 21, 22, 23};
  static const char rule[] =
      "───────────────────────────────────────────────────────────────────────────────";
  static const char *const commands[5][7] = {
      {"adc", "angle", "status", "stations", "limits", "cfg", "diag"},
      {"jog", "step", "pos", "move", "goto", "home", "stop"},
      {"sel", "save", "export", "reset", "bootsel", "plain", "exit"},
      {"selftest", "pins", "pwm", "tick", "trace", "findmin", "help"},
      {"arm", "disarm", "drive", "sim", "led", "", ""}};
  static const char *const examples[3][3] = {
      {"jog +100", "pos 3", "sel 1"},
      {"goto 1260", "cfg DUTY_NORMAL 30", "save 3"},
      {"drive fwd 25 200", "help jog", "export"}};
  char piece[288];
  if (!frame_phase || out_free() < sizeof piece)
    return;
  if (frame_phase <= sizeof rows / sizeof rows[0]) {
    uint8_t item = frame_phase - 1u;
    char content[256];
    if (item == 0u || item == 1u || item == 11u || item == 13u)
      snprintf(content, sizeof content, "%s", rule);
    else if (item >= 2u && item <= 6u) {
      const char *const *c = commands[item - 2u];
      snprintf(content, sizeof content,
               "  %-10s%-10s%-10s%-10s%-10s%-10s%-10s",
               c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
    } else if (item == 7u)
      content[0] = '\0';
    else if (item >= 8u && item <= 10u) {
      const char *const *e = examples[item - 8u];
      snprintf(content, sizeof content, "  %-25s%-25s%-25s", e[0], e[1],
               e[2]);
    } else
      snprintf(content, sizeof content, " ▶ %s", input);
    snprintf(piece, sizeof piece, "\033[%u;1H%s\033[K", rows[item], content);
    dbg_out_push(piece);
    ++frame_phase;
    return;
  }
  if (!status_frame_complete()) {
    dbg_fields_refresh();
    return;
  }
  /* Scrolling setup is the final sequence of the frame draw. */
  dbg_out_push("\033[24;r\033[24;1H");
  frame_bytes_last = frame_bytes_current;
  ++frame_draw_count;
  frame_measuring = false;
  frame_phase = 0u;
}
#endif

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

static uint16_t angle_tenths(uint16_t counts) {
  return (uint16_t)(((uint32_t)counts * 3600u + ADC_MAX_VALUE / 2u) /
                    ADC_MAX_VALUE);
}

static void print_angle(char *text, size_t size, uint16_t counts) {
  uint16_t tenths = angle_tenths(counts);
  snprintf(text, size, "%u.%u", tenths / 10u, tenths % 10u);
}

static void print_limits(const char *command) {
  char lines[7][80], angle[12];
  uint16_t current = encoder_average();
  snprintf(lines[0], sizeof lines[0], "ADC RANGE");
  snprintf(lines[1], sizeof lines[1], "  valid travel    0 .. %u      full 12-bit ADC range", ADC_MAX_VALUE);
  snprintf(lines[2], sizeof lines[2], "  motion limits   none enforced");
  snprintf(lines[3], sizeof lines[3], "  movement        linear in ADC value, not wrap-aware");
  print_angle(angle, sizeof angle, current);
  snprintf(lines[4], sizeof lines[4], "  current         %u           %s deg", current, angle);
  print_angle(angle, sizeof angle, CFG_POS_WINDOW);
  snprintf(lines[5], sizeof lines[5], "  position window %u counts      %s deg either side", CFG_POS_WINDOW, angle);
  snprintf(lines[6], sizeof lines[6], "  jog command     -%u .. +%u counts, one request", ADC_MAX_VALUE, ADC_MAX_VALUE);
  if (plain_mode) {
    for (size_t i = 0; i < 7; ++i)
      result(i ? "" : command, "complete", lines[i]);
  } else {
    for (size_t i = 7; i-- > 0;)
      result(i ? "" : command, "complete", lines[i]);
  }
}

static void print_stations(const char *command) {
  char lines[9][80], angle[12];
  uint16_t current = encoder_average();
  snprintf(lines[0], sizeof lines[0], "STATIONS                 stored      angle      from here");
  for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
    uint16_t nominal = encoder_nominal(p);
    int32_t delta = (int32_t)nominal - current;
    uint16_t distance = (uint16_t)(delta < 0 ? -delta : delta);
    print_angle(angle, sizeof angle, nominal);
    snprintf(lines[p], sizeof lines[p], "  %u                     %6u      %5s deg    %+6ld%s", p, nominal, angle, (long)delta, distance <= CFG_POS_WINDOW ? "  <-- here" : "");
  }
  print_angle(angle, sizeof angle, current);
  snprintf(lines[6], sizeof lines[6], "  current                %6u      %5s deg", current, angle);
  snprintf(lines[7], sizeof lines[7], "  spacing              %u  %u  %u  %u counts", encoder_nominal(2) - encoder_nominal(1), encoder_nominal(3) - encoder_nominal(2), encoder_nominal(4) - encoder_nominal(3), encoder_nominal(5) - encoder_nominal(4));
  if (selected_station >= POS_MIN && selected_station <= POS_MAX)
    snprintf(lines[8], sizeof lines[8], "  selected              %6u", selected_station);
  else
    snprintf(lines[8], sizeof lines[8], "  selected                none");
  if (plain_mode) {
    for (size_t i = 0; i < 9; ++i)
      result(i ? "" : command, "complete", lines[i]);
  } else {
    for (size_t i = 9; i-- > 0;)
      result(i ? "" : command, "complete", lines[i]);
  }
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
           request == MOVE_BUSY ? "controller busy" : "invalid station data");
    return false;
  }
  char detail[48];
  snprintf(detail, sizeof detail, "station %u saved, now %u", station, adc);
  result(command, "complete", detail);
  saved_station_mask |= (uint8_t)(1u << (station - POS_MIN));
  return true;
}

static bool cfg_setting_known(const char *key) {
  static const char *const names[] = {
      "DUTY_NORMAL", "DUTY_APPROACH", "DUTY_CREEP", "DUTY_MIN",
      "APPROACH_COUNTS", "POS_WINDOW", "DEBOUNCE_MS", "BRAKE_HOLD_MS",
      "POS_1_ADC", "POS_2_ADC", "POS_3_ADC", "POS_4_ADC", "POS_5_ADC"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
    if (!strcmp(key, names[i]))
      return true;
  return false;
}

static uint16_t cfg_smallest_gap(const cfg_t *values) {
  const uint16_t positions[] = {values->pos_1_adc, values->pos_2_adc,
                                values->pos_3_adc, values->pos_4_adc,
                                values->pos_5_adc};
  uint16_t smallest = ADC_MAX_VALUE;
  for (size_t i = 1; i < sizeof positions / sizeof positions[0]; ++i) {
    if (positions[i] <= positions[i - 1u])
      return 0u;
    uint16_t gap = positions[i] - positions[i - 1u];
    if (gap < smallest)
      smallest = gap;
  }
  return smallest;
}

static bool cfg_update(const char *key, long value, char *reason,
                       size_t reason_size, char *advice, size_t advice_size) {
  cfg_t next;
  reason[0] = advice[0] = '\0';
  if (!key || !cfg_setting_known(key)) {
    snprintf(reason, reason_size, "no setting called \"%s\"", key ? key : "");
    snprintf(advice, advice_size, "type \"cfg\" to list them");
    return false;
  }
  if (value < 0 || value > UINT16_MAX) {
    snprintf(reason, reason_size, "%s %ld is outside its numeric range", key,
             value);
    return false;
  }
  if (!strcmp(key, "DUTY_APPROACH") && value < cfg.duty_creep) {
    snprintf(reason, reason_size, "DUTY_APPROACH %ld is below DUTY_CREEP %u",
             value, cfg.duty_creep);
    snprintf(advice, advice_size, "set DUTY_CREEP first, or use %u or more",
             cfg.duty_creep);
    return false;
  }
  if (!strncmp(key, "DUTY_", 5) && (value < DUTY_MIN || value > PWM_WRAP)) {
    snprintf(reason, reason_size, "%s %ld is outside %u..255", key, value,
             DUTY_MIN);
    snprintf(advice, advice_size, "use a duty from %u through 255", DUTY_MIN);
    return false;
  }
  if (!strcmp(key, "APPROACH_COUNTS") && (value < 10 || value > 1000)) {
    snprintf(reason, reason_size, "APPROACH_COUNTS %ld is outside 10..1000",
             value);
    snprintf(advice, advice_size, "use 10 through 1000 counts");
    return false;
  }
  if (!strcmp(key, "POS_WINDOW") && value < 10) {
    snprintf(reason, reason_size, "POS_WINDOW %ld is below 10 counts", value);
    snprintf(advice, advice_size, "use 10 counts or more");
    return false;
  }
  if (!strcmp(key, "DEBOUNCE_MS") && (value < 1 || value > 100)) {
    snprintf(reason, reason_size, "DEBOUNCE_MS %ld is outside 1..100 ms", value);
    snprintf(advice, advice_size, "use 1 through 100 ms");
    return false;
  }
  if (!strcmp(key, "BRAKE_HOLD_MS") && value > 1000) {
    snprintf(reason, reason_size, "BRAKE_HOLD_MS %ld is outside 0..1000 ms",
             value);
    snprintf(advice, advice_size, "use 0 through 1000 ms");
    return false;
  }
  if (!strncmp(key, "POS_", 4) && value > (long)ADC_MAX_VALUE) {
    snprintf(reason, reason_size, "%s %ld is outside 0..4095", key, value);
    snprintf(advice, advice_size, "use a valid 12-bit ADC reading");
    return false;
  }
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
  else if (!strcmp(key, "DEBOUNCE_MS"))
    next.debounce_ms = (uint16_t)value;
  else if (!strcmp(key, "BRAKE_HOLD_MS"))
    next.brake_hold_ms = (uint16_t)value;
  else if (!strcmp(key, "POS_1_ADC"))
    next.pos_1_adc = (uint16_t)value;
  else if (!strcmp(key, "POS_2_ADC"))
    next.pos_2_adc = (uint16_t)value;
  else if (!strcmp(key, "POS_3_ADC"))
    next.pos_3_adc = (uint16_t)value;
  else if (!strcmp(key, "POS_4_ADC"))
    next.pos_4_adc = (uint16_t)value;
  else if (!strcmp(key, "POS_5_ADC"))
    next.pos_5_adc = (uint16_t)value;
  if (next.duty_min > next.duty_creep) {
    snprintf(reason, reason_size, "DUTY_MIN %u is above DUTY_CREEP %u",
             next.duty_min, next.duty_creep);
    snprintf(advice, advice_size, "lower DUTY_MIN or raise DUTY_CREEP first");
    return false;
  }
  if (next.duty_creep > next.duty_approach) {
    snprintf(reason, reason_size, "DUTY_APPROACH %u is below DUTY_CREEP %u",
             next.duty_approach, next.duty_creep);
    snprintf(advice, advice_size,
             "set DUTY_CREEP first, or use a value of %u or more",
             next.duty_creep);
    return false;
  }
  if (next.duty_approach > next.duty_normal) {
    snprintf(reason, reason_size, "DUTY_APPROACH %u is above DUTY_NORMAL %u",
             next.duty_approach, next.duty_normal);
    snprintf(advice, advice_size,
             "raise DUTY_NORMAL first, or use a value of %u or less",
             next.duty_normal);
    return false;
  }
  uint16_t smallest_gap = cfg_smallest_gap(&next);
  if (!smallest_gap) {
    snprintf(reason, reason_size, "station values would not stay ascending");
    snprintf(advice, advice_size, "keep POS_1_ADC through POS_5_ADC increasing");
    return false;
  }
  uint16_t window_max = (uint16_t)((smallest_gap - 1u) / 4u);
  if (next.pos_window > window_max) {
    snprintf(reason, reason_size, "POS_WINDOW %u exceeds the %u-count maximum",
             next.pos_window, window_max);
    snprintf(advice, advice_size,
             "reduce POS_WINDOW or increase the smallest station gap");
    return false;
  }
  memcpy((void *)&cfg, &next, sizeof next);
  return true;
}

static void cfg_table_line(const char *name, uint16_t now, uint16_t compiled,
                           const char *limits) {
  char detail[128];
  snprintf(detail, sizeof detail, "  %-20s%c %6u   %7u   %s", name,
           now == compiled ? ' ' : '*', now, compiled, limits);
  result("", "complete", detail);
}

static void print_cfg(const char *command) {
  cfg_t live;
  char limits[64];
  memcpy(&live, (const void *)&cfg, sizeof live);
  uint16_t smallest_gap = cfg_smallest_gap(&live);
  uint16_t window_max = smallest_gap ? (uint16_t)((smallest_gap - 1u) / 4u) : 0u;
  result(command, "complete", "SETTINGS                    now   default   limits");
  cfg_table_line("DUTY_NORMAL", live.duty_normal, DUTY_NORMAL,
                 "25..255, >= DUTY_APPROACH");
  cfg_table_line("DUTY_APPROACH", live.duty_approach, DUTY_APPROACH,
                 "25..255, between CREEP and NORMAL");
  cfg_table_line("DUTY_CREEP", live.duty_creep, DUTY_CREEP,
                 "25..255, between MIN and APPROACH");
  cfg_table_line("DUTY_MIN", live.duty_min, DUTY_MIN,
                 "25..255, <= DUTY_CREEP");
  cfg_table_line("APPROACH_COUNTS", live.approach_counts, APPROACH_COUNTS,
                 "10..1000 counts");
  snprintf(limits, sizeof limits, "10..%u counts, < quarter gap", window_max);
  cfg_table_line("POS_WINDOW", live.pos_window, POS_WINDOW, limits);
  cfg_table_line("DEBOUNCE_MS", live.debounce_ms, DEBOUNCE_MS, "1..100 ms");
  cfg_table_line("BRAKE_HOLD_MS", live.brake_hold_ms, BRAKE_HOLD_MS,
                 "0..1000 ms");
  cfg_table_line("POS_1_ADC", live.pos_1_adc, POS_1_ADC,
                 "0..4095, must stay ascending");
  cfg_table_line("POS_2_ADC", live.pos_2_adc, POS_2_ADC,
                 "0..4095, must stay ascending");
  cfg_table_line("POS_3_ADC", live.pos_3_adc, POS_3_ADC,
                 "0..4095, must stay ascending");
  cfg_table_line("POS_4_ADC", live.pos_4_adc, POS_4_ADC,
                 "0..4095, must stay ascending");
  cfg_table_line("POS_5_ADC", live.pos_5_adc, POS_5_ADC,
                 "0..4095, must stay ascending");
  result("", "complete", "* differs from compiled default");
  result("", "complete",
         "cfg DUTY_NORMAL 40 changes one; cfg reset restores defaults");
}

static bool help_setting(const char *argument, const char *command) {
  char key[24];
  size_t length = strlen(argument);
  if (length >= sizeof key)
    return false;
  for (size_t i = 0; i <= length; ++i)
    key[i] = (char)toupper((unsigned char)argument[i]);
  if (!cfg_setting_known(key))
    return false;
  if (!strcmp(key, "POS_WINDOW")) {
    cfg_t live;
    char detail[128];
    memcpy(&live, (const void *)&cfg, sizeof live);
    uint16_t gap = cfg_smallest_gap(&live);
    uint16_t tenths = angle_tenths(live.pos_window);
    result(command, "complete", "POS_WINDOW - how close counts as arrived");
    snprintf(detail, sizeof detail,
             "now %u counts, about %u.%u degrees either side of a station",
             live.pos_window, tenths / 10u, tenths % 10u);
    result("", "complete", detail);
    result("Examples", "complete", "cfg POS_WINDOW 40 - tighter stopping");
    result("", "complete", "cfg POS_WINDOW 80 - looser, earlier arrival");
    snprintf(detail, sizeof detail,
             "10 to %u counts; below quarter of current %u-count gap",
             gap ? (gap - 1u) / 4u : 0u, gap);
    result("Limits", "complete", detail);
    result("Notes", "complete",
           "For oscillation, reducing DUTY_APPROACH is usually better.");
    return true;
  }
  char detail[96];
  snprintf(detail, sizeof detail,
           "%s is runtime-settable; type cfg for live value and limits", key);
  result(command, "complete", detail);
  result("Notes", "complete",
         "RAM-only; lost on reset. export prints config.h station lines.");
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
    {"diag", "diag", "read-only",
     "Shows temporary UART receive and main-loop timing counters."},
    {"sel", "sel 3", "station 1 to 5",
     "Chooses which station save will update."},
    {"jog", "jog +2000", "-4095 to +4095 counts",
     "Creep speed only; 100 counts is roughly 7 degrees."},
    {"step", "step 250", "10, 25, 100, 250 or 500",
     "Changes the suggested calibration step."},
    {"save", "save 3", "station 1 to 5",
     "Without a number, saves the selected station."},
    {"stations", "stations", "read-only",
     "Shows stored readings and difference from now."},
    {"limits", "limits", "read-only",
     "One count is about 0.09 degrees; values come from the live configuration."},
    {"export", "export", "read-only",
     "Prints values ready to paste into config.h."},
    {"reset", "reset", "no arguments; reset stations restores calibration",
     "Restarts from flash; prints resetting before rebooting."},
    {"bootsel", "bootsel", "no arguments",
     "Restarts into the USB bootloader for recovery."},
    {"move", "move 2", "station 1 to 5", "Uses closed-loop position control."},
    {"pos", "pos 3", "station 1 to 5",
     "Alias for move; uses closed-loop position control."},
    {"goto", "goto 4000", "ADC range 0 to 4095",
     "Moves directly to one raw ADC target; movement is not wrap-aware."},
    {"home", "home", "no arguments",
     "Returns to station 1 through the guarded home path."},
    {"stop", "stop", "no arguments",
     "Brakes immediately; a period works without Enter."},
    {"status", "status", "read-only", "Shows the full controller state."},
    {"adc", "adc", "read-only", "Shows raw, filtered and classified sensing."},
    {"angle", "angle", "read-only",
     "Shows the filtered ADC reading converted to degrees."},
    {"led", "led raw ff000000", "on/off/auto, rgbw on/off, or a wire-order hex word",
     "GP18; station 3 deep yellow; station 4 pink; station 5 red-rose hazard."},
    {"selftest", "selftest", "no motion",
     "Checks configuration, ADC and the 1 kHz tick."},
    {"tick", "tick", "read-only", "Shows loop timing and watchdog health."},
    {"trace", "trace", "read-only",
     "Dumps the latest move at 50 ms intervals: time, ADC, direction and duty."},
    {"pins", "pins", "read-only", "Shows live motor and sensor pin levels."},
    {"pwm", "pwm", "read-only",
     "Shows PWM configuration and calculated frequency."},
    {"cfg", "cfg DUTY_CREEP 30", "validated RAM values",
     "Changes are RAM-only and lost on reset; export prints config.h station lines."},
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

static void submit(char *typed) {
  char original[DEBUG_COMMAND_MAX + 1u], candidates[96], *arg, *word, *save;
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
#ifdef LUFTFUGL_TRACE_INPUT
  trace_dispatch(original, command);
#endif
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
              !strcmp(command, "angle") ||
              !strcmp(command, "diag") ||
              !strcmp(command, "stations") || !strcmp(command, "limits") ||
              !strcmp(command, "export") ||
              !strcmp(command, "home") || !strcmp(command, "stop") ||
              !strcmp(command, "arm") ||
              !strcmp(command, "disarm") || !strcmp(command, "selftest") ||
              !strcmp(command, "tick") ||
              !strcmp(command, "trace") ||
              !strcmp(command, "pins") || !strcmp(command, "pwm") ||
              !strcmp(command, "findmin") || !strcmp(command, "plain") ||
              !strcmp(command, "bootsel") || !strcmp(command, "exit"))) {
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
      } else if (!help_setting(arg, original)) {
        char detail[96];
        snprintf(detail, sizeof detail,
                 "no command called \"%s\", try \"help\"", arg);
        result(original, "rejected", detail);
      }
    } else {
      for (size_t i = sizeof help_entries / sizeof help_entries[0]; i-- > 0;)
        result(help_entries[i].name, "complete", help_entries[i].example);
    }
  } else if (!strcmp(command, "diag")) {
    char d[192];
    console_diag_format(d, sizeof d);
    result(original, "complete", d);
    snprintf(d, sizeof d,
             "frame_bytes=%lu field_bytes=%u frame_draws=%lu static_redraws=%lu",
             (unsigned long)frame_bytes_last, field_bytes_last,
             (unsigned long)frame_draw_count, (unsigned long)frame_draw_count);
    result("", "complete", d);
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
  } else if (!strcmp(command, "angle")) {
    char angle[12], detail[48];
    uint16_t adc = encoder_average();
    print_angle(angle, sizeof angle, adc);
    snprintf(detail, sizeof detail, "adc %u = %s degrees", adc, angle);
    result(original, "complete", detail);
  } else if (!strcmp(command, "led")) {
    if (!arg) {
      char detail[96];
      position_t station = encoder_confirmed();
      if (led_mode() == LED_MODE_AUTO) {
        if (station >= POS_MIN && station <= POS_MAX)
          snprintf(detail, sizeof detail,
                   "automatic: %s    (station %u; %s; GP18)",
                   led_is_on() ? "on" : "off", station,
                   led_rgbw() ? "RGBW" : "RGB");
        else
          snprintf(detail, sizeof detail, "station 5: %s    (position unknown)",
                   led_is_on() ? "on" : "off");
      } else if (led_mode() == LED_MODE_FORCED_RAW)
        snprintf(detail, sizeof detail, "forced raw; %s; GP18; PIO%u SM%u",
                 led_rgbw() ? "GGRRBBWW" : "GGRRBB", led_pio_index(),
                 led_state_machine());
      else
        snprintf(detail, sizeof detail, "forced %s; PIO%u SM%u offset %u",
                 led_is_on() ? "red rose 128,12,20" : "off",
                 led_pio_index(), led_state_machine(), led_program_offset());
      result(original, "complete", detail);
    } else if (!strcmp(arg, "on")) {
      led_set_mode(LED_MODE_FORCED_ON);
      result(original, "complete", "forced red rose 128,12,20");
    } else if (!strcmp(arg, "off")) {
      led_set_mode(LED_MODE_FORCED_OFF);
      result(original, "complete", "forced off");
    } else if (!strcmp(arg, "auto")) {
      led_set_mode(LED_MODE_AUTO);
      result(original, "complete",
             "following station 3 butter / 4 peach / 5 rose hazard");
    } else if (!strcmp(arg, "rgbw on")) {
      led_set_rgbw(true);
      result(original, "complete", "RGBW enabled; sending G,R,B,W with W=0");
    } else if (!strcmp(arg, "rgbw off")) {
      led_set_rgbw(false);
      result(original, "complete", "RGBW disabled; sending G,R,B");
    } else if (!strncmp(arg, "raw ", 4u)) {
      const char *hex = arg + 4u;
      char *hex_end;
      size_t digits = strlen(hex);
      unsigned long word = strtoul(hex, &hex_end, 16);
      size_t required = led_rgbw() ? 8u : 6u;
      bool valid_hex = true;
      for (size_t i = 0; i < digits; ++i)
        if (!isxdigit((unsigned char)hex[i]))
          valid_hex = false;
      if (digits != required || *hex_end || !valid_hex) {
        result(original, "rejected",
               led_rgbw() ? "RGBW raw format is eight hex digits GGRRBBWW"
                          : "RGB raw format is six hex digits GGRRBB");
      } else {
        char detail[72];
        led_set_raw((uint32_t)word);
        snprintf(detail, sizeof detail, "forced raw %0*lx (%s wire order)",
                 (int)required, word, led_rgbw() ? "GGRRBBWW" : "GGRRBB");
        result(original, "complete", detail);
      }
    } else
      result(original, "rejected",
             "usage: led on/off/auto, led rgbw on/off, led raw <hex>, or led");
  } else if (!strcmp(command, "jog")) {
    if (arg && !strcmp(arg, "+"))
      value = jog_step;
    else if (arg && !strcmp(arg, "-"))
      value = -(long)jog_step;
    else if (!parse_long(arg, &value)) {
      result(original, "rejected", "type a distance, for example \"jog +100\"");
      return;
    }
    if (value < -(long)ADC_MAX_VALUE || value > (long)ADC_MAX_VALUE) {
      char detail[80];
      snprintf(detail, sizeof detail, "%ld is outside -%u to +%u", value,
               ADC_MAX_VALUE, ADC_MAX_VALUE);
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
             r == JOG_BUSY ? "controller busy" : "invalid jog");
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
  } else if (!strcmp(command, "stations"))
    print_stations(original);
  else if (!strcmp(command, "limits"))
    print_limits(original);
  else if (!strcmp(command, "reset")) {
    if (!arg) {
      while (dbg_out_pending())
        dbg_out_drain();
      const char *message = "resetting...\r\n";
      while (*message)
        uart_putc_raw(uart1, *message++);
      uart_tx_wait_blocking(uart1);
      watchdog_reboot(0, 0, 0);
      return;
    }
    if (strcmp(arg, "stations")) {
      result(original, "rejected", "type \"reset\" or \"reset stations\"");
      return;
    }
    if (controller_request_reset_positions() == MOVE_OK) {
      saved_station_mask = 0u;
      result(original, "complete", "compiled station values restored");
    } else
      result(original, "rejected", "controller is moving; type \"stop\" first");
  } else if (!strcmp(command, "bootsel")) {
    while (dbg_out_pending())
      dbg_out_drain();
    const char *message = "entering USB bootloader...\r\n";
    while (*message)
      uart_putc_raw(uart1, *message++);
    uart_tx_wait_blocking(uart1);
    reset_usb_boot(0, 0);
    return;
  } else if (!strcmp(command, "export"))
    export_positions(original);
  else if (!strcmp(command, "move") || !strcmp(command, "pos")) {
    if (!parse_long(arg, &value) || value < 1 || value > 5) {
      result(original, "rejected", "station must be 1 to 5");
      return;
    }
    move_result_t r = controller_request(REQ_MOVE, (position_t)value);
    if (r == MOVE_OK) {
      char detail[64];
      snprintf(detail, sizeof detail, "moving to station %ld, adc %u", value,
               encoder_nominal((position_t)value));
      result(original, "accepted", detail);
      pending_target_adc = encoder_nominal((position_t)value);
      remember_pending(PENDING_MOVE, original);
    } else
      result(original, "rejected",
             r == MOVE_BUSY          ? "already moving"
             : r == MOVE_ALREADY     ? "already at target"
             : r == MOVE_POS_UNKNOWN ? "position unknown"
                                     : "invalid target");
  } else if (!strcmp(command, "goto")) {
    if (!parse_long(arg, &value) || value < 0 || value > (long)ADC_MAX_VALUE) {
      result(original, "rejected", "type an ADC reading, for example \"goto 1260\"");
      return;
    }
    uint16_t current = encoder_average();
    uint16_t magnitude = (uint16_t)(value > current ? value - current : current - value);
    if (magnitude <= CFG_POS_WINDOW) {
      char detail[64];
      snprintf(detail, sizeof detail, "already within position window of %ld", value);
      result(original, "complete", detail);
      return;
    }
    if (controller_state() != ST_IDLE) {
      result(original, "rejected", "already moving");
      return;
    }
    move_result_t r = controller_debug_goto_adc((uint16_t)value);
    if (r == MOVE_OK) {
      char detail[80];
      snprintf(detail, sizeof detail, "moving directly to adc %ld, %u counts %s",
               value, magnitude, value > current ? "forward" : "back");
      result(original, "accepted", detail);
      pending_target_adc = (uint16_t)value;
      remember_pending(PENDING_GOTO, original);
    } else
      result(original, "rejected", r == MOVE_BUSY ? "controller busy"
                                                   : "invalid target");
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
  } else if (!strcmp(command, "trace")) {
    trace_dump_index = 0u;
    trace_dump_count = controller_motion_trace_count();
    if (!trace_dump_count)
      result(original, "complete", "no move samples recorded");
    else {
      char detail[64];
      snprintf(detail, sizeof detail, "dumping %u samples at 50 ms intervals",
               trace_dump_count);
      result(original, "complete", detail);
    }
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
    else {
      findmin_phase = 1u;
      findmin_direction = encoder_average() > ADC_MAX_VALUE / 2u ? DIR_REV
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
                timing.count != 0u;
    result(
        original, pass ? "complete" : "failed",
        pass ? "static checks passed: ADC, stations, duties and tick"
             : "a static check failed; use adc, stations, cfg and tick to "
               "inspect it");
  } else if (!strcmp(command, "cfg")) {
    if (!arg) {
      print_cfg(original);
    } else {
      char *key = strtok_r(arg, " \t", &save);
      char *value_text = strtok_r(NULL, " \t", &save);
      for (char *p = key; p && *p; ++p)
        *p = (char)toupper((unsigned char)*p);
      if (key && !strcmp(key, "RESET") && !value_text) {
        cfg_reset();
        result(original, "complete", "compiled defaults restored");
      } else if (!parse_long(value_text, &value)) {
        result(original, "rejected", "usage: cfg SETTING VALUE, or cfg reset");
      } else {
        char reason[96], advice[96];
        if (!cfg_update(key, value, reason, sizeof reason, advice,
                        sizeof advice)) {
          result(original, "rejected", reason);
          if (advice[0])
            result("", "rejected", advice);
        } else
          result(original, "complete", "runtime constant updated");
      }
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
  char detail[80], angle[12];
  if (kind == EV_JOG_COMPLETE && pending == PENDING_GOTO) {
    uint16_t adc = encoder_average();
    print_angle(angle, sizeof angle, adc);
    snprintf(detail, sizeof detail, "done     ADC %u  %s deg  err %+d", adc,
             angle, (int16_t)adc - (int16_t)pending_target_adc);
    result(pending_text, "complete", detail);
    pending = PENDING_NONE;
    return;
  }
  if ((kind == EV_JOG_COMPLETE && pending == PENDING_JOG) ||
      (kind == EV_ARRIVE &&
       (pending == PENDING_MOVE || pending == PENDING_HOME))) {
    uint16_t adc = encoder_average();
    print_angle(angle, sizeof angle, adc);
    if (pending == PENDING_MOVE)
      snprintf(detail, sizeof detail, "arrived  ADC %u  %s deg  err %+d", adc,
               angle, (int16_t)adc - (int16_t)pending_target_adc);
    else
      snprintf(detail, sizeof detail, "done     ADC %u  %s deg", adc, angle);
    result(pending_text, "complete", detail);
    pending = PENDING_NONE;
    return;
  }
  const char *name = kind == EV_PASS               ? "PASS"
                     : kind == EV_ARRIVE           ? "ARR"
                     : kind == EV_HOMING           ? "homing"
                     : kind == EV_STOPPED_UNKNOWN  ? "stopped, position unknown"
                                                   : "event";
  snprintf(detail, sizeof detail, "%s%s%u", name,
           (kind == EV_PASS || kind == EV_ARRIVE) ? ":" : "",
           (kind == EV_PASS || kind == EV_ARRIVE) ? arg : 0u);
  dbg_log_push(detail);
}

void dbg_handle_key(char c) {
  if (c == 27) {
    input_len = 0;
    input[0] = '\0';
    input_overflow = false;
    command_dirty = true;
#ifdef LUFTFUGL_TRACE_INPUT
    dbg_trace_input_out(c, "ESCAPE", NULL);
#endif
    return;
  }
  if (c == '\n' && swallow_lf) {
    swallow_lf = false;
#ifdef LUFTFUGL_TRACE_INPUT
    dbg_trace_input_out(c, "DISCARD_LF", NULL);
#endif
    return;
  }
  if (c != '\n')
    swallow_lf = false;
  if (c == '\r' || c == '\n') {
    swallow_lf = c == '\r';
    if (!input_len && !input_overflow) {
#ifdef LUFTFUGL_TRACE_INPUT
      dbg_trace_input_out(c, "IGNORED_EMPTY", NULL);
#endif
      return;
    }
    input[input_len] = '\0';
#ifdef LUFTFUGL_TRACE_INPUT
    char submitted[DEBUG_COMMAND_MAX + 1u];
    memcpy(submitted, input, input_len + 1u);
#endif
    if (plain_mode)
      dbg_out_push("\r\n");
    if (input_overflow)
      result(input, "rejected", "line too long");
    else
      submit(input);
#ifdef LUFTFUGL_TRACE_INPUT
    dbg_trace_input_out(c, input_overflow ? "DISCARD_OVERFLOW" : "SUBMIT",
                        submitted);
#endif
    input_len = 0;
    input[0] = '\0';
    input_overflow = false;
    command_dirty = true;
#ifndef LUFTFUGL_TRACE_INPUT
    if (!plain_mode && !frame_phase &&
        out_free() > DEBUG_COMMAND_MAX + 32u) {
      command_line_draw();
      command_dirty = false;
    }
#endif
    return;
  }
  if (c == '\b' || c == 127) {
    if (input_len) {
      --input_len;
      input[input_len] = '\0';
    }
    command_dirty = true;
#ifdef LUFTFUGL_TRACE_INPUT
    dbg_trace_input_out(c, "BACKSPACE", NULL);
#endif
    return;
  }
  if (c >= 32 && c <= 126) {
    if (input_len < DEBUG_COMMAND_MAX) {
      input[input_len++] = c;
      input[input_len] = '\0';
    } else
      input_overflow = true;
    if (plain_mode && echo_enabled) {
      char text[2] = {c, 0};
      dbg_out_push(text);
    } else
      command_dirty = true;
#ifdef LUFTFUGL_TRACE_INPUT
    dbg_trace_input_out(c, input_overflow ? "DISCARD_OVERFLOW" : "CMDLINE",
                        NULL);
#endif
  }
#ifdef LUFTFUGL_TRACE_INPUT
  else
    dbg_trace_input_out(c, "NOWHERE", NULL);
#endif
}

void dbg_init(void) {
  cfg_reset();
  active = plain_mode = echo_enabled = input_overflow = swallow_lf = armed =
      false;
  command_dirty = false;
  selected_station = POS_UNKNOWN;
  saved_station_mask = 0u;
  jog_step = 100u;
  input_len = 0;
  out_head = out_tail = 0u;
  pending = PENDING_NONE;
  pending_target_adc = 0u;
  sim_travel_active = false;
  findmin_phase = 0u;
  trace_dump_index = trace_dump_count = 0u;
  frame_phase = 0u;
  first_result = true;
  frame_measuring = false;
  frame_bytes_current = frame_bytes_last = frame_draw_count = 0u;
  field_bytes_last = 0u;
  next_refresh = 0u;
  memset(status_shadow, 0, sizeof status_shadow);
}
static void enter(bool plain) {
  plain_mode = plain;
  active = echo_enabled = true;
  input_len = 0;
  input_overflow = false;
  command_dirty = false;
#ifndef LUFTFUGL_TRACE_INPUT
  if (!plain)
    dbg_render();
#endif
}
void dbg_enter(void) { enter(false); }
void dbg_enter_plain(void) { enter(true); }
void dbg_exit(void) {
  dbg_request_t request = {.op = DBG_OP_EXIT};

  /* Leaving the UI must still hand motor and simulation changes to the tick. */
  (void)controller_debug_request(&request);
  active = echo_enabled = armed = false;
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
  if (trace_dump_index < trace_dump_count && !dbg_out_pending()) {
    motion_trace_entry_t entry;
    if (controller_motion_trace_get(trace_dump_index, &entry)) {
      char detail[96];
      snprintf(detail, sizeof detail,
               "sample %u t=%lums adc=%u dir=%s duty=%u",
               (unsigned)(trace_dump_index + 1u), (unsigned long)entry.ms,
               entry.adc, dir_text(entry.direction), entry.duty);
      result("trace", "complete", detail);
    }
    ++trace_dump_index;
  }
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
#ifndef LUFTFUGL_TRACE_INPUT
  if (active && !plain_mode && frame_phase)
    frame_continue();
  if (active && !plain_mode && !frame_phase && command_dirty &&
      out_free() > DEBUG_COMMAND_MAX + 32u) {
    command_line_draw();
    command_dirty = false;
  }
  if (active && !plain_mode && !frame_phase &&
      (int32_t)(now - next_refresh) >= 0) {
    dbg_fields_refresh();
    next_refresh = now + DEBUG_REFRESH_MS;
  }
#endif
  if (active)
    dbg_out_drain();
}
