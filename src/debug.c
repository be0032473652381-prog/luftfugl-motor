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
#include "pico/time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LUFTFUGL_DEBUG
static bool unavailable_sim_active(void) { return false; }
static void unavailable_sim_enable(bool on) { (void)on; }
static void unavailable_sim_set(uint16_t adc) { (void)adc; }
#define encoder_sim_active unavailable_sim_active
#define encoder_sim_enable unavailable_sim_enable
#define encoder_sim_set unavailable_sim_set
static void dbg_motor_disarm(void) {}
static void dbg_motor_pulse(direction_t direction, uint8_t duty, uint16_t ms) {
  (void)direction;
  (void)duty;
  (void)ms;
}
#endif

typedef enum {
  MENU_ROOT,
  MENU_STATUS,
  MENU_ADC,
  MENU_MOTOR,
  MENU_CAL,
  MENU_CFG,
  MENU_FAULT,
  MENU_TEST,
  MENU_BENCH,
  MENU_SIM
} menu_t;
typedef enum {
  PROMPT_NONE,
  PROMPT_ARM,
  PROMPT_COUPLED,
  PROMPT_CLEAR,
  PROMPT_RATE,
  PROMPT_DUTY,
  PROMPT_DURATION,
  PROMPT_CFG_KEY,
  PROMPT_CFG_VALUE,
  PROMPT_SIM_VALUE,
  PROMPT_SIM_BAND,
  PROMPT_SIM_FROM,
  PROMPT_SIM_TO,
  PROMPT_SIM_MS,
  PROMPT_SIM_DRIFT,
  PROMPT_GOTO_ADC
} prompt_t;
typedef enum {
  ACT_NONE,
  ACT_STATIC,
  ACT_FINDMIN_BASE,
  ACT_FINDMIN_PULSE,
  ACT_CAL_POS_WAIT,
  ACT_CAL_POS_SAMPLE,
  ACT_CAL_STEP,
  ACT_CAL_TRAVEL,
  ACT_CAL_OVER,
  ACT_SELFTEST,
  ACT_BENCH_PINS,
  ACT_GPIO_WALK,
  ACT_TICK_HEALTH,
  ACT_SIM_TRAVEL,
  ACT_SIM_PARK,
  ACT_SIM_DRIFT,
  ACT_SIM_SWEEP
} action_t;
static bool active, armed, coupled, streaming, monitoring, capturing,
    echo_enabled;
static bool plain_mode, menu_focused, jog_mode;
static uint16_t jog_step;
static uint32_t next_field_refresh, auto_enter_deadline;
static char out_buf[DEBUG_OUT_BUFFER];
static volatile uint16_t out_head, out_tail;
typedef struct {
  char uptime[16], state[16], pos[8], adc[12], error[12], armed[8], coupled[8],
      sim[8], faults[12], duty[8], dir[8], target[12], stall[24];
} field_shadow_t;
static field_shadow_t field_shadow;
static bool prompt_swallow_lf;
static menu_t menu;
static prompt_t prompt;
static char input[CONSOLE_LINE_MAX + 1];
#ifdef LUFTFUGL_DEBUG
static char cfg_key[CONSOLE_LINE_MAX + 1];
#endif
static uint8_t input_len;
static uint8_t pulse_duty;
static uint16_t pulse_ms, stream_hz;
static uint32_t arm_deadline, coupled_deadline, next_stream;
static uint16_t capture_min, capture_max;
static uint32_t capture_samples;
static action_t action;
static uint8_t action_stage, action_duty;
static uint32_t action_started, action_deadline, action_sum, action_count,
    action_worst;
static uint16_t action_min, action_max, action_start_adc, noise_floor;
static uint16_t action_means[6];
static uint8_t action_source, action_saved_duty;
static position_t sim_from, sim_to;
static uint16_t sim_band_ms;
static uint32_t bench_tick_start;
static uint16_t self_adc[DEBUG_SELFTEST_ADC_SAMPLES];
static uint32_t self_tick_start;
static bool self_all_full;
static bool action_pass;
static position_t action_last_pos;
static void action_poll(uint32_t now);
static void line(const char *s);
static void dbg_help(void);

static uint32_t ms_now(void) { return to_ms_since_boot(get_absolute_time()); }
static bool simulation_active(void) {
#ifdef LUFTFUGL_DEBUG
  return encoder_sim_active();
#else
  return false;
#endif
}
static void dbg_help(void) {
  switch (menu) {
  case MENU_ROOT:
    line("HELP: choose 1-9; w shows currently runnable tests; x exits debug.");
    break;
  case MENU_STATUS:
    line("HELP: inspect state or stream telemetry; streaming is motion-free.");
    break;
  case MENU_ADC:
    line("HELP: read, monitor, capture, or inspect position windows; no motion "
         "required.");
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_MOTOR:
    line("HELP: manual outputs require typing UNCOUPLED; simulation must be "
         "off.");
    break;
#endif
  case MENU_CAL:
    line("HELP: position sampling is motion-free; motion calibration requires "
         "COUPLED.");
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_CFG:
    line("HELP: overrides are volatile RAM values and reset on debug exit.");
    break;
#endif
  case MENU_FAULT:
    line("HELP: inspect faults/history/counters; clearing a fault still "
         "requires home.");
    break;
  case MENU_TEST:
    line("HELP: static test is motion-free; motion test requires COUPLED and "
         "known position.");
    break;
  case MENU_BENCH:
    line("HELP: all tests are bare-board safe; GPIO walk additionally requires "
         "UNCOUPLED.");
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_SIM:
    line("HELP: enable simulation first; motor inhibit has no override; any "
         "key aborts a sequence.");
    break;
#endif
  default:
    break;
  }
}
static const char *state_text(sys_state_t s) {
  static const char *const n[] = {"BOOT",   "IDLE",  "MOVING", "APPROACH",
                                  "HOMING", "FAULT", "DEBUG"};
  return n[s];
}
static const char *dir_text(direction_t d) {
  return d == DIR_FWD ? "FWD" : d == DIR_REV ? "REV" : "STP";
}
static void line(const char *s) {
  if (active && !plain_mode) {
    size_t n = strlen(s);
    while (n && s[n - 1u] == ' ')
      --n;
    if (n && s[n - 1u] == ':')
      dbg_field_write(11, 1, s);
    else
      dbg_log_push(s);
  } else
    console_debug_line(s);
}
static void position_limits(position_t p, uint16_t *lo, uint16_t *hi) {
  if (p < POS_MIN || p > POS_MAX) {
    *lo = CFG_ADC_SAFE_MIN;
    *hi = CFG_ADC_SAFE_MAX;
    return;
  }
  uint16_t nominal = encoder_nominal(p);
  *lo = nominal - CFG_POS_WINDOW;
  *hi = nominal + CFG_POS_WINDOW;
}
static bool post(dbg_op_t op, direction_t dir, uint8_t duty, uint16_t ms,
                 bool flag) {
  dbg_request_t r = {
      .op = op, .dir = dir, .duty = duty, .ms = ms, .flag = flag};
  return controller_debug_request(&r);
}
static void activity(void) {
  uint32_t n = ms_now();
  if (armed)
    arm_deadline = n + DEBUG_INTERLOCK_TIMEOUT_MS;
  if (coupled)
    coupled_deadline = n + DEBUG_INTERLOCK_TIMEOUT_MS;
}

void dbg_field_write(uint8_t row, uint8_t col, const char *text) {
  char esc[24];
  console_debug_write("\033[s");
  snprintf(esc, sizeof esc, "\033[%u;%uH", row, col);
  console_debug_write(esc);
  console_debug_write(text);
  if (col == 1u)
    console_debug_write("\033[K");
  console_debug_write("\033[u");
}

void dbg_log_push(const char *text) {
  char prefix[20];
  uint32_t seconds = ms_now() / 1000u;
  snprintf(prefix, sizeof prefix, "  %02lu:%02lu:%02lu  ",
           (unsigned long)(seconds / 3600u),
           (unsigned long)((seconds / 60u) % 60u),
           (unsigned long)(seconds % 60u));
  console_debug_write("\033[s\033[24;1H");
  console_debug_write(prefix);
  console_debug_write(text);
  console_debug_write("\033[K\r\n\033[u");
}

bool dbg_plain_mode(void) { return plain_mode; }
void dbg_screen_init(void) {
  console_debug_write("\033[2J\033[H\033[?25l\033[15;24r");
}
void dbg_screen_teardown(void) {
  console_debug_write("\033[1;24r\033[?25h\033[2J\033[H");
}

static void draw_position_rows(void) {
  char b[80];
  position_t p = controller_position();
  snprintf(
      b, sizeof b,
      "  1 %c pos 1 %5u      4 %c pos 4 %5u        j jog          g goto adc",
      p == 1 ? '>' : ' ', encoder_nominal(1), p == 4 ? '>' : ' ',
      encoder_nominal(4));
  dbg_field_write(7, 1, b);
  snprintf(b, sizeof b,
           "  2 %c pos 2 %5u      5 %c pos 5 %5u        h home         s stop",
           p == 2 ? '>' : ' ', encoder_nominal(2), p == 5 ? '>' : ' ',
           encoder_nominal(5));
  dbg_field_write(8, 1, b);
  snprintf(b, sizeof b, "  3 %c pos 3 %5u", p == 3 ? '>' : ' ',
           encoder_nominal(3));
  dbg_field_write(9, 1, b);
}

static void draw_menu_area(void) {
  const char *items;
  switch (menu) {
  case MENU_STATUS:
    items = " STATUS: s dump  t stream  r rate  k timing  z reset";
    break;
  case MENU_ADC:
    items = " ENCODER: a reading  m monitor  c capture  b table  e error";
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_MOTOR:
    items = " MANUAL: A arm  f/v pulse  d duty  t time  b brake  c coast  n "
            "findmin";
    break;
#endif
  case MENU_CAL:
    items = " CALIBRATE: p positions  s step  w travel  o overshoot  r report";
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_CFG:
    items = " CONFIG: l list  s set  d defaults  e export";
    break;
#endif
  case MENU_FAULT:
    items = " FAULTS: f last  h history  c counters  z reset  k clear";
    break;
  case MENU_TEST:
    items = " SELFTEST: s static  m motion";
    break;
  case MENU_BENCH:
    items =
        " BENCH: p pins  g gpio  f pwm  t tick  r reset  o protocol  e echo";
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_SIM:
    items =
        " SIM: e enable  v adc  b position  t travel  p park  l limit  s sweep";
    break;
#endif
  default:
#ifdef LUFTFUGL_DEBUG
    items = " m menus: S status  E encoder  M manual  C calibrate";
#else
    items = " m menus: S status  E encoder  C calibrate  F faults  T selftest  "
            "B bench";
#endif
    break;
  }
  dbg_field_write(11, 1, items);
  dbg_field_write(
      12, 1,
      menu == MENU_ROOT
          ?
#ifdef LUFTFUGL_DEBUG
          "           G config  F faults  T selftest  B bench  I sim"
#else
          ""
#endif
          : " ? help     q root     x exit");
}

void dbg_frame_draw(void) {
  console_debug_write("\033[2J\033[H");
  dbg_field_write(1, 1, " luftfugl " FW_VERSION "          POSITION CONTROL");
  dbg_field_write(1, 61, "up");
  dbg_field_write(3, 1, "");
  dbg_field_write(3, 3, "state");
  dbg_field_write(3, 25, "pos");
  dbg_field_write(3, 41, "adc");
  dbg_field_write(3, 56, "err");
  dbg_field_write(4, 1, "");
  dbg_field_write(4, 3, "armed");
  dbg_field_write(4, 18, "coupled");
  dbg_field_write(4, 36, "sim");
  dbg_field_write(4, 54, "faults");
  dbg_field_write(5, 1, "");
  dbg_field_write(5, 3, "duty");
  dbg_field_write(5, 22, "dir");
  dbg_field_write(5, 34, "target");
  dbg_field_write(5, 54, "stall");
  dbg_field_write(6, 1,
                  "------------------------------------------------------------"
                  "-------------------");
  draw_position_rows();
  dbg_field_write(10, 1,
                  "------------------------------------------------------------"
                  "-------------------");
  draw_menu_area();
  dbg_field_write(13, 1,
                  "------------------------------------------------------------"
                  "-------------------");
  dbg_field_write(14, 1, " EVENTS");
  console_debug_write("\033[15;24r\033[24;1H");
  memset(&field_shadow, 0, sizeof field_shadow);
  dbg_fields_refresh();
}

static void refresh_field(char *shadow, size_t size, uint8_t row, uint8_t col,
                          const char *value, bool reverse) {
  if (!strcmp(shadow, value))
    return;
  strncpy(shadow, value, size - 1u);
  shadow[size - 1u] = 0;
  uint8_t width = 8u;
  if (row == 1u && col == 65u)
    width = 11u;
  else if (row == 3u && col == 10u)
    width = 10u;
  else if (row == 3u && col == 30u)
    width = 6u;
  else if (row == 3u && col == 45u)
    width = 8u;
  else if (row == 3u && col == 61u)
    width = 10u;
  else if (row == 4u && col == 10u)
    width = 10u;
  else if (row == 4u && col == 61u)
    width = 10u;
  else if (row == 5u && col == 10u)
    width = 10u;
  else if (row == 5u && col == 61u)
    width = 16u;
  char padded[24], b[40];
  snprintf(padded, sizeof padded, "%-*s", width, value);
  snprintf(b, sizeof b, reverse ? "\033[7m%s\033[0m" : "%s", padded);
  dbg_field_write(row, col, b);
}

void dbg_fields_refresh(void) {
  char b[24];
  dbg_counters_t c;
  motion_check_status_t checks;
  uint32_t seconds = ms_now() / 1000u;
  controller_counters_get(&c);
  controller_motion_checks_get(&checks);
  snprintf(b, sizeof b, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600u),
           (unsigned long)((seconds / 60u) % 60u),
           (unsigned long)(seconds % 60u));
  refresh_field(field_shadow.uptime, sizeof field_shadow.uptime, 1, 65, b,
                false);
  refresh_field(field_shadow.state, sizeof field_shadow.state, 3, 10,
                state_text(controller_state()), false);
  position_t p = controller_position();
  if (p >= POS_MIN && p <= POS_MAX)
    snprintf(b, sizeof b, "%u", p);
  else
    snprintf(b, sizeof b, "?");
  if (strcmp(field_shadow.pos, b)) {
    refresh_field(field_shadow.pos, sizeof field_shadow.pos, 3, 30, b, false);
    draw_position_rows();
  }
  snprintf(b, sizeof b, "%u", encoder_average());
  refresh_field(field_shadow.adc, sizeof field_shadow.adc, 3, 45, b, false);
  uint16_t tgt = controller_target_adc();
  if (tgt)
    snprintf(b, sizeof b, "%+d", (int16_t)tgt - (int16_t)encoder_average());
  else
    b[0] = 0;
  refresh_field(field_shadow.error, sizeof field_shadow.error, 3, 61, b, false);
  refresh_field(field_shadow.armed, sizeof field_shadow.armed, 4, 10,
                armed ? "YES" : "NO", armed);
  refresh_field(field_shadow.coupled, sizeof field_shadow.coupled, 4, 26,
                coupled ? "YES" : "NO", coupled);
  refresh_field(field_shadow.sim, sizeof field_shadow.sim, 4, 41,
                simulation_active() ? "ON" : "OFF", simulation_active());
  snprintf(b, sizeof b, "%lu", (unsigned long)c.faults);
  refresh_field(field_shadow.faults, sizeof field_shadow.faults, 4, 61, b,
                false);
  snprintf(b, sizeof b, "%u", motor_duty());
  refresh_field(field_shadow.duty, sizeof field_shadow.duty, 5, 10, b, false);
  refresh_field(field_shadow.dir, sizeof field_shadow.dir, 5, 26,
                dir_text(motor_direction()), false);
  position_t tp = controller_target();
  if (tp >= POS_MIN && tp <= POS_MAX)
    snprintf(b, sizeof b, "%u", tp);
  else if (tgt)
    snprintf(b, sizeof b, "%u", tgt);
  else
    b[0] = 0;
  refresh_field(field_shadow.target, sizeof field_shadow.target, 5, 41, b,
                false);
  if (checks.stall_armed)
    snprintf(b, sizeof b, "%u/%lums", checks.current_delta,
             (unsigned long)checks.window_remaining_ms);
  else
    b[0] = 0;
  refresh_field(field_shadow.stall, sizeof field_shadow.stall, 5, 61, b, false);
}

void dbg_menu_focus(char key) {
  switch (key) {
  case 'S':
    menu = MENU_STATUS;
    break;
  case 'E':
    menu = MENU_ADC;
    break;
#ifdef LUFTFUGL_DEBUG
  case 'M':
    menu = MENU_MOTOR;
    break;
#endif
  case 'C':
    menu = MENU_CAL;
    break;
#ifdef LUFTFUGL_DEBUG
  case 'G':
    menu = MENU_CFG;
    break;
#endif
  case 'F':
    menu = MENU_FAULT;
    break;
  case 'T':
    menu = MENU_TEST;
    break;
  case 'B':
    menu = MENU_BENCH;
    break;
#ifdef LUFTFUGL_DEBUG
  case 'I':
    menu = MENU_SIM;
    break;
#endif
  default:
    return;
  }
  menu_focused = false;
  draw_menu_area();
}
void dbg_pos_goto(position_t p) {
  move_result_t r = controller_request(REQ_MOVE, p);
  char b[48];
  snprintf(b, sizeof b, "position %u request: %s", p,
           r == MOVE_OK        ? "accepted"
           : r == MOVE_ALREADY ? "already there"
                               : "rejected");
  line(b);
}
void dbg_pos_goto_adc(uint16_t adc) {
  if (adc < CFG_ADC_SAFE_MIN)
    adc = CFG_ADC_SAFE_MIN;
  if (adc > CFG_ADC_SAFE_MAX)
    adc = CFG_ADC_SAFE_MAX;
  move_result_t r = controller_debug_goto_adc(adc);
  char b[56];
  snprintf(b, sizeof b, "goto adc %u: %s", adc,
           r == MOVE_OK        ? "accepted"
           : r == MOVE_ALREADY ? "already in window"
                               : "rejected");
  line(b);
}
void dbg_pos_jog(int16_t counts) {
  int32_t adc = (int32_t)encoder_average() + counts;
  if (adc < CFG_ADC_SAFE_MIN)
    adc = CFG_ADC_SAFE_MIN;
  if (adc > CFG_ADC_SAFE_MAX)
    adc = CFG_ADC_SAFE_MAX;
  dbg_pos_goto_adc((uint16_t)adc);
}
void dbg_pos_step_size(int8_t direction) {
  static const uint16_t steps[] = {DEBUG_JOG_STEP_1, DEBUG_JOG_STEP_2,
                                   DEBUG_JOG_STEP_3, DEBUG_JOG_STEP_4};
  uint8_t i = 0;
  while (i < 4u && steps[i] != jog_step)
    ++i;
  i = direction > 0 ? (uint8_t)((i + 1u) % 4u) : (uint8_t)((i + 3u) % 4u);
  jog_step = steps[i];
  char b[40];
  snprintf(b, sizeof b, "jog step %u counts", jog_step);
  line(b);
}

void dbg_out_push(const char *text) {
  while (*text) {
    uint16_t next = (uint16_t)((out_head + 1u) % DEBUG_OUT_BUFFER);
    if (next == out_tail)
      out_tail = (uint16_t)((out_tail + 1u) % DEBUG_OUT_BUFFER);
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

void dbg_init(void) {
  cfg_reset();
  active = armed = coupled = streaming = monitoring = capturing = false;
  plain_mode = false;
  menu_focused = jog_mode = false;
  jog_step = DEBUG_JOG_STEP_DEFAULT;
  next_field_refresh = 0;
  menu = MENU_ROOT;
  prompt = PROMPT_NONE;
  prompt_swallow_lf = false;
  pulse_duty = DUTY_CREEP;
  pulse_ms = DEBUG_PULSE_DEFAULT_MS;
  stream_hz = DEBUG_STREAM_DEFAULT_HZ;
  action = ACT_NONE;
  echo_enabled = false;
  out_head = out_tail = 0u;
  auto_enter_deadline = ms_now() + DEBUG_AUTO_ENTER_MS;
}
static void enter_mode(bool plain) {
  plain_mode = plain;
  echo_enabled = true;
  active = true;
  menu = MENU_ROOT;
  if (plain)
    dbg_render();
  else {
    dbg_screen_init();
    dbg_frame_draw();
    dbg_log_push("debug entered; ANSI/VT100 80x24 minimum");
  }
}
void dbg_enter(void) { enter_mode(false); }
void dbg_enter_plain(void) { enter_mode(true); }
bool dbg_auto_enter(char c) {
  if (active || (int32_t)(ms_now() - auto_enter_deadline) >= 0)
    return false;
  enter_mode(false);
  dbg_handle_key(c);
  return true;
}
void dbg_exit(void) {
  bool was_plain = plain_mode;
  echo_enabled = false;
  (void)post(DBG_OP_EXIT, DIR_STOP, 0, 0, false);
  armed = coupled = false;
  streaming = monitoring = capturing = false;
  action = ACT_NONE;
  cfg_reset();
  active = false;
  if (!was_plain)
    dbg_screen_teardown();
  console_debug_line("debug exited");
}
bool dbg_active(void) { return active; }
void dbg_render_header(void) {
  char b[DEBUG_HEADER_BUFFER_SIZE];
  dbg_counters_t c;
  uint16_t v = encoder_average();
  position_t p = encoder_instant();
  int16_t error = (p >= POS_MIN && p <= POS_MAX) ? encoder_error_to(p) : 0;
  controller_counters_get(&c);
  snprintf(
      b, sizeof b,
      "=== luftfugl debug 1.0 ============================\r\n state %s   pos "
      "%s   adc %u (error %+d counts)\r\n armed %s     coupled %s     sim %s   "
      "  faults %lu\r\n===================================================",
      state_text(controller_state()),
      p == 1   ? "1"
      : p == 2 ? "2"
      : p == 3 ? "3"
      : p == 4 ? "4"
      : p == 5 ? "5"
               : "?",
      v, error, armed ? "YES" : "NO", coupled ? "YES" : "NO",
      simulation_active() ? "\033[7mON\033[0m" : "OFF",
      (unsigned long)c.faults);
  line(b);
}
void dbg_render(void) {
  char b[DEBUG_MENU_BUFFER_SIZE];
  if (!plain_mode) {
    dbg_frame_draw();
    return;
  }
  dbg_render_header();
  switch (menu) {
  case MENU_ROOT:
#ifdef LUFTFUGL_DEBUG
    snprintf(
        b, sizeof b,
        " 1  status & telemetry .... stream %s\r\n 2  encoder & adc ......... "
        "avg %u\r\n 3  motor manual .......... %s\r\n 4  calibration "
        "........... %s\r\n 5  configuration ......... defaults/overrides\r\n "
        "6  faults & history ...... state %s\r\n 7  self-test ............. "
        "static ready\r\n 8  bench tests ........... bare-board ready\r\n 9  "
        "simulation ............ %s\r\n w  what can run now\r\n x  exit",
        streaming ? "ON" : "OFF", encoder_average(),
        armed ? "ARMED" : "needs UNCOUPLED",
        coupled ? "COUPLED" : "needs COUPLED", state_text(controller_state()),
        encoder_sim_active() ? "ON" : "OFF");
#else
    snprintf(b, sizeof b,
             " 1  status & telemetry .... stream %s\r\n 2  encoder & adc "
             "......... avg %u\r\n 4  calibration ........... %s\r\n 6  faults "
             "& history ...... state %s\r\n 7  self-test ............. static "
             "ready\r\n 8  bench tests ........... bare-board ready\r\n w  "
             "what can run now\r\n x  exit",
             streaming ? "ON" : "OFF", encoder_average(),
             coupled ? "COUPLED" : "needs COUPLED",
             state_text(controller_state()));
#endif
    line(b);
    break;
  case MENU_STATUS:
    snprintf(b, sizeof b,
             " s  state dump ............ %s\r\n t  telemetry stream ...... "
             "%s\r\n r  stream rate ........... %u Hz\r\n k  tick timing "
             "........... measured\r\n z  reset timing stats\r\n ?  help   w "
             "available   q back   x exit",
             state_text(controller_state()), streaming ? "ON" : "OFF",
             stream_hz);
    line(b);
    break;
  case MENU_ADC:
    snprintf(b, sizeof b,
             " a  adc reading ........... raw %u avg %u\r\n m  live monitor "
             ".......... %s\r\n c  min/max capture ....... %s\r\n b  position "
             "table ........ active\r\n e  position error ........ current\r\n "
             "? help   w available   q back   x exit",
             encoder_raw(), encoder_average(), monitoring ? "ON" : "OFF",
             capturing ? "ON" : "OFF");
    line(b);
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_MOTOR:
    snprintf(
        b, sizeof b,
        " A  arm/disarm ............ %s\r\n f  pulse forward ......... duty "
        "%u, %u ms\r\n v  pulse reverse ......... duty %u, %u ms\r\n d  pulse "
        "duty ............ %u / %u\r\n t  pulse duration ........ %u ms\r\n b  "
        "brake   c coast   s standby   n find minimum",
        armed ? "ARMED" : "needs UNCOUPLED", pulse_duty, pulse_ms, pulse_duty,
        pulse_ms, pulse_duty, PWM_WRAP, pulse_ms);
    line(b);
    break;
#endif
  case MENU_CAL:
    snprintf(
        b, sizeof b,
        " p  position readings ..... motion-free\r\n s  step time "
        "............. %s\r\n w  full travel ........... %s\r\n o  overshoot "
        "............. %s\r\n r  calibration report .... session RAM",
        coupled ? "ready" : "needs COUPLED",
        coupled ? "ready" : "needs COUPLED",
        coupled ? "ready" : "needs COUPLED");
    line(b);
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_CFG:
    snprintf(
        b, sizeof b,
        " l  list constants ........ DUTY_NORMAL %u\r\n s  set override "
        ".......... volatile RAM\r\n d  reset defaults\r\n e  export config.h",
        CFG_DUTY_NORMAL);
    line(b);
    break;
#endif
  case MENU_FAULT:
    snprintf(b, sizeof b,
             " f  last fault ............ state %s\r\n h  position history "
             "...... %u entries\r\n c  counters\r\n z  reset counters\r\n k  "
             "clear fault ........... home still required",
             state_text(controller_state()), controller_history_count());
    line(b);
    break;
  case MENU_TEST:
    snprintf(
        b, sizeof b,
        " s  static self-test ...... ready\r\n m  motion self-test ...... %s",
        coupled ? "ready" : "needs COUPLED");
    line(b);
    break;
  case MENU_BENCH:
    snprintf(b, sizeof b,
             " p  pin state readout ..... %s\r\n g  gpio walk ............. "
             "%s\r\n f  pwm report ............ slice %u\r\n t  tick/watchdog "
             "......... ready\r\n r  reset reason .......... uptime %lu ms\r\n "
             "o  protocol strings\r\n e  character echo ........ %s\r\n s  "
             "stall/direction status",
             action == ACT_BENCH_PINS ? "STREAMING" : "5 Hz",
             armed ? "ready" : "needs UNCOUPLED",
             pwm_gpio_to_slice_num(PIN_PWMA), (unsigned long)ms_now(),
             echo_enabled ? "ON" : "OFF");
    line(b);
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_SIM:
    snprintf(b, sizeof b,
             " e  simulation ............ %s\r\n v  adc value ............. %u "
             "/ %u\r\n b  jump to position ...... current %u\r\n t  travel "
             "sequence ....... prompted\r\n p  park between stations\r\n l  "
             "over-travel fault\r\n s  sweep windows",
             encoder_sim_active() ? "ON, MOTOR INHIBITED" : "OFF",
             encoder_sim_value(), ADC_MAX_VALUE, encoder_instant());
    line(b);
    break;
#endif
  default:
    break;
  }
}
void dbg_abort(void) {
  action_t stopped = action;
  if (action == ACT_CAL_OVER)
    cfg.duty_approach = action_saved_duty;
  action = ACT_NONE;
  (void)controller_request(REQ_STOP, 0);
  if (stopped == ACT_GPIO_WALK)
    (void)post(DBG_OP_GPIO_SET, DIR_STOP, DEBUG_GPIO_OP_ALL_LOW, 0, false);
  else
    (void)post(DBG_OP_SIM_ENABLE, DIR_STOP, 0, 0, false);
  encoder_sim_enable(false);
  armed = coupled = false;
  line("ABORTED");
}

static void finish_prompt(void) {
  if (input_len == 0u && (prompt == PROMPT_ARM || prompt == PROMPT_COUPLED ||
                          prompt == PROMPT_CLEAR))
    return;
  input[input_len] = '\0';
  if (prompt == PROMPT_ARM) {
    if (!strcmp(input, "UNCOUPLED")) {
      if (encoder_sim_active()) {
        line("rejected: disable simulation first");
        prompt = PROMPT_NONE;
        input_len = 0;
        return;
      }
      armed = true;
      coupled = false;
      arm_deadline = ms_now() + DEBUG_INTERLOCK_TIMEOUT_MS;
      (void)post(DBG_OP_ENTER, DIR_STOP, 0, 0, false);
      line("manual drive armed");
    } else
      line("arming cancelled");
  } else if (prompt == PROMPT_COUPLED) {
    if (!strcmp(input, "COUPLED")) {
      if (encoder_sim_active()) {
        line("rejected: disable simulation first");
        prompt = PROMPT_NONE;
        input_len = 0;
        return;
      }
      coupled = true;
      armed = false;
      coupled_deadline = ms_now() + DEBUG_INTERLOCK_TIMEOUT_MS;
      line("coupled motion confirmed");
    } else
      line("coupled confirmation cancelled");
  } else if (prompt == PROMPT_CLEAR) {
    if (!strcmp(input, "CLEAR")) {
      dbg_request_t r = {.op = DBG_OP_FAULT_CLEAR};
      (void)controller_debug_request(&r);
      line("fault flag cleared; home required");
    } else
      line("fault clear cancelled");
  } else if (prompt == PROMPT_RATE) {
    long v = strtol(input, NULL, 10);
    dbg_stream_set_rate((uint16_t)v);
#ifdef LUFTFUGL_DEBUG
  } else if (prompt == PROMPT_DUTY) {
    long v = strtol(input, NULL, 10);
    if (v >= 0 && v <= PWM_WRAP) {
      pulse_duty = (uint8_t)v;
      line(v < DUTY_MIN ? "pulse duty updated; below DUTY_MIN"
                        : "pulse duty updated");
    } else
      line("pulse duty must be 0..255");
  } else if (prompt == PROMPT_DURATION) {
    long v = strtol(input, NULL, 10);
    if (v >= (long)DEBUG_PULSE_MIN_MS && v <= (long)DEBUG_PULSE_MAX_MS) {
      pulse_ms = (uint16_t)v;
      line("pulse duration updated");
    } else
      line("pulse duration must be 10..2000 ms");
  } else if (prompt == PROMPT_CFG_KEY) {
    strncpy(cfg_key, input, sizeof cfg_key);
    cfg_key[sizeof cfg_key - 1] = 0;
    line("value (integer, unit and range depend on key): ");
    prompt = PROMPT_CFG_VALUE;
    input_len = 0;
    return;
  } else if (prompt == PROMPT_CFG_VALUE) {
    long v = strtol(input, NULL, 10);
    line(dbg_cfg_set(cfg_key, v) ? "configuration override applied"
                                 : "configuration override rejected");
  } else if (prompt == PROMPT_SIM_VALUE) {
    long v = strtol(input, NULL, 10);
    if (v >= 0 && v <= (long)ADC_MAX_VALUE)
      dbg_sim_set_value((uint16_t)v);
    else
      line("rejected: adc must be 0-4095 counts");
  } else if (prompt == PROMPT_SIM_BAND) {
    long v = strtol(input, NULL, 10);
    if (v >= POS_MIN && v <= POS_MAX)
      dbg_sim_set_position((position_t)v);
    else
      line("rejected: position must be 1-5");
  } else if (prompt == PROMPT_SIM_FROM) {
    long v = strtol(input, NULL, 10);
    if (v >= POS_MIN && v <= POS_MAX) {
      sim_from = (position_t)v;
      line("to position (1-5): ");
      prompt = PROMPT_SIM_TO;
      input_len = 0;
      return;
    } else
      line("rejected: from position must be 1-5");
  } else if (prompt == PROMPT_SIM_TO) {
    long v = strtol(input, NULL, 10);
    if (v >= POS_MIN && v <= POS_MAX) {
      sim_to = (position_t)v;
      line("milliseconds per position (12-65535 ms): ");
      prompt = PROMPT_SIM_MS;
      input_len = 0;
      return;
    } else
      line("rejected: to position must be 1-5");
  } else if (prompt == PROMPT_SIM_MS) {
    long v = strtol(input, NULL, 10);
    if (v >= DEBUG_SIM_MIN_POSITION_MS && v <= UINT16_MAX)
      dbg_sim_travel(sim_from, sim_to, (uint16_t)v);
    else
      line("rejected: rate is below debounce window or out of range");
  } else if (prompt == PROMPT_SIM_DRIFT) {
    long v = strtol(input, NULL, 10);
    if (v == POS_MIN || v == POS_MAX)
      dbg_sim_overtravel((position_t)v);
    else
      line("rejected: limit must be 1 or 5");
#endif
  } else if (prompt == PROMPT_GOTO_ADC) {
    long v = strtol(input, NULL, 10);
    if (v >= 0 && v <= (long)ADC_MAX_VALUE)
      dbg_pos_goto_adc((uint16_t)v);
    else
      line("rejected: adc must be 0-4095 counts");
  }
  prompt = PROMPT_NONE;
  input_len = 0;
  if (!plain_mode)
    draw_menu_area();
}

static bool handle_prompt_char(char c) {
  static bool previous_was_cr;
  if (prompt == PROMPT_NONE)
    return false;
  if (c == 27) {
    prompt = PROMPT_NONE;
    input_len = 0;
    line("prompt cancelled");
    if (!plain_mode)
      draw_menu_area();
    return true;
  }
  if (!plain_mode && c == 's') {
    (void)controller_request(REQ_STOP, 0);
    line("stop requested");
    return true;
  }
  if (c == '\n' && previous_was_cr) {
    previous_was_cr = false;
    return true;
  }
  if (c == '\r' || c == '\n') {
    previous_was_cr = c == '\r';
    if (input_len == 0u)
      return true;
    if (echo_enabled)
      console_debug_write("\r\n");
    finish_prompt();
    return true;
  }
  previous_was_cr = false;
  if (c == '\b' || c == 127) {
    if (input_len != 0u) {
      --input_len;
      if (echo_enabled)
        console_debug_write("\b \b");
    }
    return true;
  }
  if (input_len < CONSOLE_LINE_MAX)
    input[input_len++] = c;
  if (echo_enabled) {
    char echoed[2] = {c, '\0'};
    console_debug_write(echoed);
  }
  return true;
}

void dbg_handle_key(char c) {
  if (handle_prompt_char(c))
    return;
#ifndef LUFTFUGL_DEBUG
  if (plain_mode && menu == MENU_ROOT && (c == '3' || c == '5' || c == '9'))
    return;
#endif
  if (echo_enabled) {
    if (c == '\b' || c == 127)
      console_debug_write("\b \b");
    else if (c >= 32 && c <= 126) {
      char e[2] = {c, 0};
      console_debug_write(e);
    }
  }
  if (!plain_mode) {
    if (c == 's') {
      (void)controller_request(REQ_STOP, 0);
      line("stop requested");
      return;
    }
    if (c >= '1' && c <= '5') {
      dbg_pos_goto((position_t)(c - '0'));
      return;
    }
    if (c == 'h') {
      if (controller_state() == ST_FAULT)
        line("rejected: faulted; clear fault before motion");
      else {
        (void)controller_request(REQ_HOME, 0);
        line("home requested");
      }
      return;
    }
    if (c == 'S' || c == 'E' || c == 'M' || c == 'C' || c == 'G' || c == 'F' ||
        c == 'T' || c == 'B' || c == 'I') {
      dbg_menu_focus(c);
      return;
    }
    if (c == 'm') {
      menu_focused = true;
      line("menu focus: press S E M C G F T B or I");
      return;
    }
    if (menu_focused) {
      dbg_menu_focus(c);
      return;
    }
    if (c == 'j') {
      jog_mode = true;
      line("JOG: + or -, [ or ] changes step, s stops");
      return;
    }
    if (jog_mode && (c == '+' || c == '-')) {
      dbg_pos_jog(c == '+' ? (int16_t)jog_step : -(int16_t)jog_step);
      return;
    }
    if (jog_mode && (c == '[' || c == ']')) {
      dbg_pos_step_size(c == ']' ? 1 : -1);
      return;
    }
    if (c == 'g') {
      dbg_field_write(11, 1, "goto adc (0-4095 counts, clamped safe):");
      prompt = PROMPT_GOTO_ADC;
      input_len = 0;
      return;
    }
  }
  if (prompt_swallow_lf) {
    prompt_swallow_lf = false;
    if (c == '\n')
      return;
  }
  if (prompt != PROMPT_NONE && (c == '\r' || c == '\n')) {
    if (!input_len)
      return;
    if (echo_enabled)
      console_debug_write("\r\n");
    prompt_swallow_lf = c == '\r';
    finish_prompt();
    return;
  }
  if ((c == '\b' || c == 127) && prompt != PROMPT_NONE) {
    if (input_len) {
      --input_len;
      if (echo_enabled)
        console_debug_write("\b \b");
    }
    return;
  }
  if (plain_mode && echo_enabled && c == '\n')
    console_debug_write("\r\n");
  if (action != ACT_NONE) {
    if (action == ACT_BENCH_PINS) {
      action = ACT_NONE;
      line("PIN STATE stopped");
      return;
    }
    if (action == ACT_CAL_POS_WAIT && c == ' ') {
      action = ACT_CAL_POS_SAMPLE;
      action_started = ms_now();
      action_deadline = action_started + DEBUG_CAL_SAMPLE_MS;
      action_sum = action_count = 0;
      action_min = ADC_MAX_VALUE;
      action_max = 0;
      return;
    }
    dbg_abort();
    return;
  }
  if (prompt != PROMPT_NONE) {
    if (c == '\r')
      return;
    if (c == '\n') {
      finish_prompt();
      return;
    }
    if (input_len < CONSOLE_LINE_MAX)
      input[input_len++] = c;
    return;
  }
  activity();
  if (c == 'x') {
    dbg_exit();
    return;
  }
  if (c == 'w') {
    dbg_what_can_run();
    return;
  }
  if (c == '?') {
    dbg_help();
    return;
  }
  if (c == 'q') {
    menu = MENU_ROOT;
    dbg_render();
    return;
  }
  if (menu == MENU_ROOT && c >= '1' && c <= '9') {
    menu = (menu_t)(c - '0');
    dbg_render();
    return;
  }
  switch (menu) {
  case MENU_STATUS:
    if (c == 's')
      dbg_status_dump();
    else if (c == 't')
      dbg_stream_toggle();
    else if (c == 'r') {
      {
        char b[64];
        snprintf(b, sizeof b, "rate (1-50 Hz, current %u Hz):", stream_hz);
        line(b);
      }
      prompt = PROMPT_RATE;
      input_len = 0;
    } else if (c == 'k')
      dbg_timing_stats();
    else if (c == 'z')
      dbg_timing_reset();
    break;
  case MENU_ADC:
    if (c == 'a')
      dbg_adc_read_once();
    else if (c == 'm')
      dbg_adc_monitor_toggle();
    else if (c == 'c')
      dbg_adc_capture_toggle();
    else if (c == 'b')
      dbg_position_table();
    else if (c == 'e')
      dbg_position_error();
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_MOTOR:
    if (c == 'A') {
      if (armed)
        dbg_motor_disarm();
      else
        dbg_motor_arm();
    } else if (c == 'f')
      dbg_motor_pulse(DIR_FWD, pulse_duty, pulse_ms);
    else if (c == 'v')
      dbg_motor_pulse(DIR_REV, pulse_duty, pulse_ms);
    else if (c == 'd') {
      {
        char b[64];
        snprintf(b, sizeof b, "duty (0-255, current %u): ", pulse_duty);
        line(b);
      }
      prompt = PROMPT_DUTY;
      input_len = 0;
    } else if (c == 't') {
      {
        char b[72];
        snprintf(b, sizeof b,
                 "duration (10-2000 ms, current %u ms): ", pulse_ms);
        line(b);
      }
      prompt = PROMPT_DURATION;
      input_len = 0;
    } else if (c == 'b')
      dbg_motor_brake();
    else if (c == 'c')
      dbg_motor_coast();
    else if (c == 's')
      dbg_motor_standby(true);
    else if (c == 'n')
      dbg_motor_find_min(DIR_FWD);
    break;
#endif
  case MENU_CAL:
    if (c == 'p')
      dbg_cal_positions();
    else if (c == 's')
      dbg_cal_step_time();
    else if (c == 'w')
      dbg_cal_travel_time();
    else if (c == 'o')
      dbg_cal_overshoot();
    else if (c == 'r')
      dbg_cal_report();
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_CFG:
    if (c == 'l')
      dbg_cfg_list();
    else if (c == 's') {
      line("key:");
      prompt = PROMPT_CFG_KEY;
      input_len = 0;
    } else if (c == 'd')
      dbg_cfg_reset();
    else if (c == 'e')
      dbg_cfg_export();
    break;
#endif
  case MENU_FAULT:
    if (c == 'f')
      dbg_fault_show();
    else if (c == 'h')
      dbg_history_dump();
    else if (c == 'c')
      dbg_counters_show();
    else if (c == 'z')
      dbg_counters_reset();
    else if (c == 'k')
      dbg_fault_clear();
    break;
  case MENU_TEST:
    if (c == 's')
      dbg_selftest_static();
    else if (c == 'm')
      dbg_selftest_motion();
    break;
  case MENU_BENCH:
    if (c == 'p')
      dbg_bench_pins();
    else if (c == 'g')
      dbg_bench_gpio_walk();
    else if (c == 'f')
      dbg_bench_pwm_report();
    else if (c == 't')
      dbg_bench_tick_health();
    else if (c == 'r')
      dbg_bench_reset_reason();
    else if (c == 'o')
      dbg_bench_protocol_list();
    else if (c == 'e')
      dbg_bench_echo_toggle();
    else if (c == 's')
      dbg_bench_motion_checks();
    break;
#ifdef LUFTFUGL_DEBUG
  case MENU_SIM:
    if (c == 'e')
      dbg_sim_toggle();
    else if (c == 'v') {
      line("adc value (0-4095, unit counts): ");
      prompt = PROMPT_SIM_VALUE;
      input_len = 0;
    } else if (c == 'b') {
      line("position (1-5): ");
      prompt = PROMPT_SIM_BAND;
      input_len = 0;
    } else if (c == 't') {
      line("from position (1-5): ");
      prompt = PROMPT_SIM_FROM;
      input_len = 0;
    } else if (c == 'p')
      dbg_sim_park();
    else if (c == 'l') {
      line("limit (1 or 5): ");
      prompt = PROMPT_SIM_DRIFT;
      input_len = 0;
    } else if (c == 's')
      dbg_sim_sweep();
    break;
#endif
  default:
    break;
  }
}
void dbg_poll(void) {
  uint32_t n = ms_now();
  if (active && !plain_mode && (int32_t)(n - next_field_refresh) >= 0) {
    dbg_fields_refresh();
    next_field_refresh = n + DEBUG_SCREEN_REFRESH_MS;
  }
  action_poll(n);
  if (armed && (int32_t)(n - arm_deadline) >= 0)
    dbg_motor_disarm();
  if (coupled && (int32_t)(n - coupled_deadline) >= 0)
    dbg_coupled_clear();
  if (streaming && (int32_t)(n - next_stream) >= 0) {
    char b[128];
    snprintf(b, sizeof b, "T %lu %s %u %u %s %u %u %u", (unsigned long)n,
             state_text(controller_state()), controller_position(),
             controller_target(), dir_text(motor_direction()), motor_duty(),
             encoder_raw(), encoder_average());
    line(b);
    next_stream = n + DEBUG_SELFTEST_WINDOW_MS / stream_hz;
  }
  if (monitoring && (int32_t)(n - next_stream) >= 0) {
    dbg_adc_read_once();
    next_stream = n + DEBUG_ADC_MONITOR_PERIOD_MS;
  }
  if (capturing) {
    uint16_t v = encoder_average();
    if (!capture_samples || v < capture_min)
      capture_min = v;
    if (!capture_samples || v > capture_max)
      capture_max = v;
    capture_samples++;
  }
}

void dbg_status_dump(void) {
  char b[160];
  snprintf(b, sizeof b,
           "state %s pos %u target %u dir %s duty %u deadline %lu lastdir %s "
           "avg %u armed %s uptime %lu",
           state_text(controller_state()), controller_position(),
           controller_target(), dir_text(motor_direction()), motor_duty(),
           (unsigned long)controller_deadline_ms(),
           dir_text(controller_last_direction()), encoder_average(),
           armed ? "YES" : "NO", (unsigned long)ms_now());
  line(b);
}
void dbg_stream_toggle(void) {
  streaming = !streaming;
  next_stream = ms_now();
  line(streaming ? "telemetry started" : "telemetry stopped");
}
void dbg_stream_set_rate(uint16_t hz) {
  if (hz >= DEBUG_STREAM_MIN_HZ && hz <= DEBUG_STREAM_MAX_HZ)
    stream_hz = hz;
  line(hz >= DEBUG_STREAM_MIN_HZ && hz <= DEBUG_STREAM_MAX_HZ
           ? "stream rate updated"
           : "stream rate must be 1..50");
}
void dbg_timing_stats(void) {
  tick_stats_t s;
  char b[128];
  controller_timing_get(&s);
  snprintf(b, sizeof b, "TIMING min=%lu max=%lu mean=%llu overruns=%lu",
           (unsigned long)s.min_us, (unsigned long)s.max_us,
           s.count ? (unsigned long long)(s.sum_us / s.count) : 0ull,
           (unsigned long)s.overruns);
  line(b);
}
void dbg_timing_reset(void) {
  controller_timing_reset();
  line("timing statistics reset");
}
void dbg_adc_read_once(void) {
  char b[96];
  snprintf(b, sizeof b, "ADC raw=%u avg=%u position=%u confirmed=%s",
           encoder_raw(), encoder_average(), encoder_instant(),
           encoder_confirmed() == encoder_instant() ? "YES" : "NO");
  line(b);
}
void dbg_adc_monitor_toggle(void) {
  monitoring = !monitoring;
  next_stream = ms_now();
  line(monitoring ? "ADC monitor started" : "ADC monitor stopped");
}
void dbg_adc_capture_toggle(void) {
  capturing = !capturing;
  if (capturing) {
    capture_samples = 0;
    capture_min = ADC_MAX_VALUE;
    capture_max = 0;
    line("ADC capture started");
  } else {
    char b[160];
    uint16_t lo, hi;
    position_t p = controller_position();
    position_limits(p, &lo, &hi);
    uint16_t margin = (capture_min > lo ? capture_min - lo : 0) <
                              (hi > capture_max ? hi - capture_max : 0)
                          ? (capture_min > lo ? capture_min - lo : 0)
                          : (hi > capture_max ? hi - capture_max : 0);
    uint16_t width = hi - lo + 1u;
    snprintf(b, sizeof b,
             "CAPTURE pos=%u samples=%lu min=%u max=%u ripple=%u window=%u..%u "
             "margin=%u/%u%%",
             p, (unsigned long)capture_samples, capture_min, capture_max,
             capture_max - capture_min, lo, hi, margin,
             (unsigned)(margin * DEBUG_PERCENT_SCALE / width));
    line(b);
  }
}
void dbg_position_table(void) {
  char b[128];
  line("POSITION TABLE");
  for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
    uint16_t n = encoder_nominal(p), v = encoder_average();
    int16_t e = (int16_t)n - (int16_t)v;
    snprintf(b, sizeof b, "  %u nominal=%u window=%u..%u measured_error=%+d", p,
             n, n - CFG_POS_WINDOW, n + CFG_POS_WINDOW, e);
    line(b);
  }
}
void dbg_position_error(void) {
  char b[112];
  uint16_t v = encoder_average(), best = UINT16_MAX;
  position_t nearest = POS_MIN;
  for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
    uint16_t n = encoder_nominal(p), d = v > n ? v - n : n - v;
    if (d < best) {
      best = d;
      nearest = p;
    }
  }
  int16_t signed_error = (int16_t)v - (int16_t)encoder_nominal(nearest);
  int32_t degrees_tenths =
      (int32_t)signed_error * 1800 /
      (encoder_nominal(POS_MAX) - encoder_nominal(POS_MIN));
  snprintf(b, sizeof b, "POSITION ERROR nearest=%u counts=%+d degrees=%+ld.%ld",
           nearest, signed_error, (long)(degrees_tenths / 10),
           (long)(degrees_tenths < 0 ? -(degrees_tenths % 10)
                                     : degrees_tenths % 10));
  line(b);
}
#ifdef LUFTFUGL_DEBUG
bool dbg_motor_arm(void) {
  line(
      "Manual drive bypasses position limits.\r\nThe mechanism has NO physical "
      "end-stops.\r\nType UNCOUPLED to confirm the motor is disconnected:");
  input_len = 0;
  prompt = PROMPT_ARM;
  return false;
}
void dbg_motor_disarm(void) {
  armed = false;
  (void)post(DBG_OP_EXIT, DIR_STOP, 0, 0, false);
}
bool dbg_motor_armed(void) { return armed; }
void dbg_motor_pulse(direction_t d, uint8_t duty, uint16_t ms) {
  if (!armed) {
    line("debug: not armed");
    return;
  }
  if (!post(DBG_OP_DRIVE, d, duty, ms, false))
    line("debug: busy");
}
void dbg_motor_brake(void) {
  if (!post(DBG_OP_BRAKE, DIR_STOP, 0, 0, false))
    line("debug: brake request rejected");
}
void dbg_motor_coast(void) {
  if (post(DBG_OP_COAST, DIR_STOP, 0, 0, false))
    line("motor coasting; mechanism may be moved by hand");
}
void dbg_motor_standby(bool on) {
  (void)post(DBG_OP_STANDBY, DIR_STOP, 0, 0, on);
}
void dbg_motor_find_min(direction_t d) {
  char b[48];
  if (!armed) {
    line("debug: not armed");
    return;
  }
  snprintf(b, sizeof b, "FINDMIN dir=%s", dir_text(d));
  line(b);
  action = ACT_FINDMIN_BASE;
  action_stage = (uint8_t)d;
  action_duty = DUTY_MIN - DEBUG_FINDMIN_DUTY_OFFSET;
  action_started = ms_now();
  action_deadline = action_started + DEBUG_FINDMIN_PULSE_MS;
  action_min = ADC_MAX_VALUE;
  action_max = 0;
}
#endif
bool dbg_coupled_confirm(void) {
  line("This test moves the mechanism under closed-loop control.\r\nPosition "
       "limits ARE enforced. The mechanism must be connected.\r\nType COUPLED "
       "to confirm:");
  input_len = 0;
  prompt = PROMPT_COUPLED;
  return false;
}
void dbg_coupled_clear(void) { coupled = false; }
bool dbg_coupled(void) { return coupled; }
void dbg_cal_positions(void) {
  line("CAL POSITIONS\r\n Move to position 1, press SPACE (q to abort)");
  action = ACT_CAL_POS_WAIT;
  action_stage = 1;
}
void dbg_cal_step_time(void) {
  if (controller_state() == ST_FAULT) {
    line("rejected: motion is blocked while faulted");
    return;
  }
  if (!coupled) {
    dbg_coupled_confirm();
    return;
  }
  if (controller_position() != 3) {
    line("CAL STEP requires position 3");
    return;
  }
  line("CAL STEP");
  action = ACT_CAL_STEP;
  action_stage = 0;
  action_worst = 0;
  action_started = 0;
}
void dbg_cal_travel_time(void) {
  if (controller_state() == ST_FAULT) {
    line("rejected: motion is blocked while faulted");
    return;
  }
  if (!coupled) {
    dbg_coupled_confirm();
    return;
  }
  if (controller_position() != POS_MAX) {
    line("travel calibration requires position 5");
    return;
  }
  line("CAL TRAVEL");
  action = ACT_CAL_TRAVEL;
  action_stage = 0;
  action_started = 0;
}
void dbg_cal_overshoot(void) {
  if (controller_state() == ST_FAULT) {
    line("rejected: motion is blocked while faulted");
    return;
  }
  if (!coupled) {
    dbg_coupled_confirm();
    return;
  }
  if (controller_position() != 2 && controller_position() != 4) {
    line("CAL OVERSHOOT requires position 2 or 4");
    return;
  }
  {
    char b[64];
    snprintf(b, sizeof b, "CAL OVERSHOOT target=3 nominal=%u",
             DEBUG_NOMINAL_P3);
    line(b);
  }
  action = ACT_CAL_OVER;
  action_stage = 0;
  action_started = 0;
  action_count = 0;
  action_source = controller_position();
  action_saved_duty = cfg.duty_approach;
  action_duty = DEBUG_OVERSHOOT_DUTY_HIGH;
}
void dbg_cal_report(void) { dbg_cfg_export(); }
#ifdef LUFTFUGL_DEBUG
void dbg_cfg_list(void) { dbg_cfg_export(); }
bool dbg_cfg_set(const char *k, int32_t v) {
  if (v < 0)
    return false;
  if ((!strncmp(k, "DUTY_", 5) && v > PWM_WRAP) ||
      (!strncmp(k, "POS_", 4) && v > (int32_t)ADC_MAX_VALUE) ||
      (!strncmp(k, "ADC_SAFE_", 9) && v > (int32_t)ADC_MAX_VALUE) ||
      ((strcmp(k, "TIMEOUT_STEP_MS") && strcmp(k, "TIMEOUT_HOME_MS")) &&
       v > UINT16_MAX))
    return false;
  cfg_t n;
  memcpy(&n, (const void *)&cfg, sizeof n);
  if (!strcmp(k, "DUTY_NORMAL"))
    n.duty_normal = v;
  else if (!strcmp(k, "DUTY_APPROACH"))
    n.duty_approach = v;
  else if (!strcmp(k, "DUTY_CREEP"))
    n.duty_creep = v;
  else if (!strcmp(k, "DUTY_MIN"))
    n.duty_min = v;
  else if (!strcmp(k, "POS_1_ADC"))
    n.pos_1_adc = v;
  else if (!strcmp(k, "POS_2_ADC"))
    n.pos_2_adc = v;
  else if (!strcmp(k, "POS_3_ADC"))
    n.pos_3_adc = v;
  else if (!strcmp(k, "POS_4_ADC"))
    n.pos_4_adc = v;
  else if (!strcmp(k, "POS_5_ADC"))
    n.pos_5_adc = v;
  else if (!strcmp(k, "POS_WINDOW"))
    n.pos_window = v;
  else if (!strcmp(k, "APPROACH_COUNTS"))
    n.approach_counts = v;
  else if (!strcmp(k, "ADC_SAFE_MIN"))
    n.adc_safe_min = v;
  else if (!strcmp(k, "ADC_SAFE_MAX"))
    n.adc_safe_max = v;
  else if (!strcmp(k, "STALL_DELTA"))
    n.stall_delta = v;
  else if (!strcmp(k, "STALL_WINDOW_MS"))
    n.stall_window_ms = v;
  else if (!strcmp(k, "REVERSE_DELTA"))
    n.reverse_delta = v;
  else if (!strcmp(k, "DEBOUNCE_MS"))
    n.debounce_ms = v;
  else if (!strcmp(k, "BRAKE_HOLD_MS"))
    n.brake_hold_ms = v;
  else if (!strcmp(k, "TIMEOUT_STEP_MS"))
    n.timeout_step_ms = v;
  else if (!strcmp(k, "TIMEOUT_HOME_MS"))
    n.timeout_home_ms = v;
  else
    return false;
  if (n.duty_min > n.duty_creep || n.duty_creep > n.duty_approach ||
      n.duty_approach > n.duty_normal)
    return false;
  if (!(n.pos_1_adc < n.pos_2_adc && n.pos_2_adc < n.pos_3_adc &&
        n.pos_3_adc < n.pos_4_adc && n.pos_4_adc < n.pos_5_adc))
    return false;
  uint16_t gap = n.pos_2_adc - n.pos_1_adc, d = n.pos_3_adc - n.pos_2_adc;
  if (d < gap)
    gap = d;
  d = n.pos_4_adc - n.pos_3_adc;
  if (d < gap)
    gap = d;
  d = n.pos_5_adc - n.pos_4_adc;
  if (d < gap)
    gap = d;
  if ((uint32_t)n.pos_window * 4u >= gap || n.adc_safe_min >= n.adc_safe_max ||
      n.stall_window_ms == 0u)
    return false;
  if (n.debounce_ms == 0 || n.brake_hold_ms == 0 ||
      n.timeout_step_ms < DEBUG_CFG_TIMEOUT_MIN_MS ||
      n.timeout_home_ms < DEBUG_CFG_TIMEOUT_MIN_MS)
    return false;
  memcpy((void *)&cfg, &n, sizeof n);
  return true;
}
void dbg_cfg_reset(void) {
  cfg_reset();
  line("configuration defaults restored");
}
#endif
void dbg_cfg_export(void) {
  char b[96];
  line("CONFIG EXPORT");
#define OUT(name, val)                                                         \
  do {                                                                         \
    snprintf(b, sizeof b, "#define " name " %lu", (unsigned long)(val));       \
    line(b);                                                                   \
  } while (0)
  OUT("DUTY_NORMAL", cfg.duty_normal);
  OUT("DUTY_APPROACH", cfg.duty_approach);
  OUT("DUTY_CREEP", cfg.duty_creep);
  OUT("DUTY_MIN", cfg.duty_min);
  OUT("POS_1_ADC", cfg.pos_1_adc);
  OUT("POS_2_ADC", cfg.pos_2_adc);
  OUT("POS_3_ADC", cfg.pos_3_adc);
  OUT("POS_4_ADC", cfg.pos_4_adc);
  OUT("POS_5_ADC", cfg.pos_5_adc);
  OUT("POS_WINDOW", cfg.pos_window);
  OUT("APPROACH_COUNTS", cfg.approach_counts);
  OUT("ADC_SAFE_MIN", cfg.adc_safe_min);
  OUT("ADC_SAFE_MAX", cfg.adc_safe_max);
  OUT("STALL_DELTA", cfg.stall_delta);
  OUT("STALL_WINDOW_MS", cfg.stall_window_ms);
  OUT("REVERSE_DELTA", cfg.reverse_delta);
  OUT("DEBOUNCE_MS", cfg.debounce_ms);
  OUT("BRAKE_HOLD_MS", cfg.brake_hold_ms);
  OUT("TIMEOUT_STEP_MS", cfg.timeout_step_ms);
  OUT("TIMEOUT_HOME_MS", cfg.timeout_home_ms);
#undef OUT
}
void dbg_fault_show(void) {
  fault_record_t f;
  char b[160];
  controller_fault_get(&f);
  snprintf(b, sizeof b,
           "FAULT kind=%u ms=%lu state=%s pos=%u target=%u deadline=%lu",
           f.kind, (unsigned long)f.ms, state_text(f.state), f.pos, f.target,
           (unsigned long)f.deadline_ms);
  line(b);
}
void dbg_history_dump(void) {
  line("HISTORY (newest last)");
  for (uint8_t i = 0; i < controller_history_count(); ++i) {
    hist_entry_t h;
    char b[64];
    if (controller_history_get(i, &h)) {
      const char *k = h.kind == 0 ? "PASS" : h.kind == 1 ? "ARR" : "UNKNOWN";
      snprintf(b, sizeof b, "  %lu  %s %u", (unsigned long)h.ms, k, h.pos);
      line(b);
    }
  }
}
void dbg_counters_show(void) {
  dbg_counters_t c;
  char b[200];
  controller_counters_get(&c);
  snprintf(b, sizeof b,
           "COUNTERS moves_ok=%lu moves_timeout=%lu faults=%lu "
           "limit_rejects=%lu pass_events=%lu tick_overruns=%lu",
           (unsigned long)c.moves_ok, (unsigned long)c.moves_timeout,
           (unsigned long)c.faults, (unsigned long)c.limit_rejects,
           (unsigned long)c.pass_events, (unsigned long)c.tick_overruns);
  line(b);
}
void dbg_counters_reset(void) {
  controller_counters_reset();
  line("counters reset");
}
void dbg_fault_clear(void) {
  line("Clearing a fault does not restore position.\r\nA home sequence will "
       "still be required before any move.\r\nType CLEAR to confirm:");
  input_len = 0;
  prompt = PROMPT_CLEAR;
}
bool dbg_selftest_static(void) {
  tick_stats_t s;
  controller_timing_get(&s);
  self_tick_start = s.count;
  self_all_full = console_event_queue_full();
  action = ACT_STATIC;
  action_stage = 0;
  action_count = 0;
  action_deadline = ms_now();
  line("SELFTEST STATIC");
  return true;
}
bool dbg_selftest_motion(void) {
  if (controller_state() == ST_FAULT) {
    line("rejected: motion is blocked while faulted");
    return false;
  }
  if (!coupled) {
    dbg_coupled_confirm();
    return false;
  }
  line("SELFTEST MOTION");
  action = ACT_SELFTEST;
  action_stage = 0;
  action_started = 0;
  return true;
}

static uint16_t sim_nominal(position_t pos) { return encoder_nominal(pos); }

static bool sim_history_ok(position_t from, position_t to) {
  uint8_t distance = (uint8_t)(from > to ? from - to : to - from);
  uint8_t used = controller_history_count();
  if (used < distance)
    return false;
  uint8_t first = (uint8_t)(used - distance);
  for (uint8_t i = 0; i < distance; ++i) {
    hist_entry_t h;
    if (!controller_history_get((uint8_t)(first + i), &h))
      return false;
    position_t expected =
        to > from ? (position_t)(from + i + 1u) : (position_t)(from - i - 1u);
    uint8_t kind = (uint8_t)(i + 1u == distance ? 1u : 0u);
    if (h.pos != expected || h.kind != kind)
      return false;
  }
  return true;
}

void dbg_what_can_run(void) {
  line("AVAILABLE NOW");
  line("  2/a  adc reading            ready");
  line("  7/s  static self-test       ready");
  line("  8/*  bench tests            ready");
  line("  9/*  simulation             ready");
  if (armed)
    line("  3/*  manual drive           ready");
  if (coupled && controller_position() != POS_UNKNOWN) {
    line("  4/s  step time              ready");
    line("  7/m  motion self-test       ready");
  }
  line("BLOCKED");
  if (!armed)
    line("  3/*  manual drive           needs UNCOUPLED (menu 3, key A)");
  if (!(coupled && controller_position() != POS_UNKNOWN))
    line("  4/s  step time              needs COUPLED and a known position");
  if (!(coupled && controller_position() != POS_UNKNOWN))
    line("  7/m  motion self-test       needs COUPLED and a known position");
}

void dbg_bench_pins(void) {
  line("PIN STATE (any key to stop)");
  action = ACT_BENCH_PINS;
  action_deadline = ms_now();
}
void dbg_bench_gpio_walk(void) {
  if (!armed) {
    line("rejected: needs UNCOUPLED (menu 3, key A)");
    return;
  }
  if (encoder_sim_active()) {
    line("rejected: disable simulation first");
    return;
  }
  line("GPIO WALK  (UNCOUPLED confirmed, PWMA held 0)");
  action = ACT_GPIO_WALK;
  action_stage = 0;
  action_deadline = ms_now();
}
void dbg_bench_pwm_report(void) {
  char b[128];
  uint slice = pwm_gpio_to_slice_num(PIN_PWMA);
  uint chan = pwm_gpio_to_channel(PIN_PWMA);
  uint32_t clock = clock_get_hz(clk_sys);
  uint32_t raw = pwm_hw->slice[slice].div & DEBUG_PWM_DIV_MASK;
  uint32_t wrap = pwm_hw->slice[slice].top;
  uint32_t hz10 =
      (uint32_t)(((uint64_t)clock * DEBUG_PWM_FIXED_SCALE * DEBUG_FREQ_TENTHS) /
                 (raw * (wrap + 1u)));
  bool pass = hz10 >= DEBUG_PWM_SPEC_HZ * DEBUG_FREQ_TENTHS *
                          (DEBUG_PERCENT_SCALE - DEBUG_PWM_TOLERANCE_PERCENT) /
                          DEBUG_PERCENT_SCALE &&
              hz10 <= DEBUG_PWM_SPEC_HZ * DEBUG_FREQ_TENTHS *
                          (DEBUG_PERCENT_SCALE + DEBUG_PWM_TOLERANCE_PERCENT) /
                          DEBUG_PERCENT_SCALE;
  line("PWM CONFIG");
  snprintf(b, sizeof b, "  slice        %u, channel %c (GP%u)", slice,
           chan ? 'B' : 'A', PIN_PWMA);
  line(b);
  snprintf(b, sizeof b, "  clk_sys      %lu Hz", (unsigned long)clock);
  line(b);
  snprintf(b, sizeof b,
           "  clkdiv       %lu.%04lu  (raw 0x%03lX, 8.4 fixed point)",
           (unsigned long)(raw / DEBUG_PWM_FIXED_SCALE),
           (unsigned long)((raw % DEBUG_PWM_FIXED_SCALE) *
                           DEBUG_DECIMAL_4_SCALE / DEBUG_PWM_FIXED_SCALE),
           (unsigned long)raw);
  line(b);
  snprintf(b, sizeof b, "  wrap         %lu", (unsigned long)wrap);
  line(b);
  snprintf(b, sizeof b, "  frequency    %lu.%lu Hz",
           (unsigned long)(hz10 / DEBUG_FREQ_TENTHS),
           (unsigned long)(hz10 % DEBUG_FREQ_TENTHS));
  line(b);
  snprintf(b, sizeof b, "  spec         %u Hz, tolerance %u%%   %s",
           DEBUG_PWM_SPEC_HZ, DEBUG_PWM_TOLERANCE_PERCENT,
           pass ? "PASS" : "FAIL");
  line(b);
}
void dbg_bench_tick_health(void) {
  tick_stats_t s;
  controller_timing_get(&s);
  bench_tick_start = s.count;
  action_started = ms_now();
  action_deadline = action_started + DEBUG_TICK_HEALTH_WINDOW_MS;
  action = ACT_TICK_HEALTH;
  line("TICK HEALTH");
}
void dbg_bench_reset_reason(void) {
  char b[96];
  line("RESET REASON");
  line(watchdog_caused_reboot() ? "  watchdog reset"
                                : "  power-on or debug reset");
  snprintf(b, sizeof b, "  uptime %lu ms", (unsigned long)ms_now());
  line(b);
}
void dbg_bench_protocol_list(void) {
  line(
      "PROTOCOL STRINGS (listing only, not emitted)\r\n  commands   pos | adc "
      "| jog +/-N | setpos N | savepos | move N | stop | status | home\r\n  "
      "reports    ADC raw=N avg=N pos=N|? | #define POS_N_ADC value\r\n  ok    "
      "     OK: jog +/-N from ADC | OK: pos N = ADC\r\n             OK: moving "
      "to N\r\n             OK: already at N\r\n             OK: stopped\r\n   "
      "          OK: homing\r\n  errors     ERR: unknown command\r\n           "
      "  ERR: invalid jog\r\n             ERR: invalid target\r\n             "
      "ERR: at end-stop\r\n             ERR: busy\r\n             ERR: "
      "position unknown\r\n             ERR: fault\r\n             ERR: line "
      "too long\r\n             ERR: timeout\r\n             ERR: fault home "
      "timeout\r\n             ERR: overtravel\r\n             ERR: stall\r\n  "
      "           ERR: direction\r\n             ERR: watchdog reset\r\n  "
      "events     PASS:N | ARR:N | OK: homing | STOPPED: position unknown");
}
void dbg_bench_echo_toggle(void) {
  echo_enabled = true;
  line("character echo ON");
}
void dbg_bench_motion_checks(void) {
  motion_check_status_t s;
  char b[128];
  controller_motion_checks_get(&s);
  snprintf(
      b, sizeof b,
      "MOTION CHECKS delta=%u window_remaining=%lu ms stall=%s direction=%s",
      s.current_delta, (unsigned long)s.window_remaining_ms,
      s.stall_armed ? "ARMED" : "DISARMED",
      s.direction_armed ? "ARMED" : "DISARMED");
  line(b);
}

#ifdef LUFTFUGL_DEBUG
void dbg_sim_toggle(void) {
  if (encoder_sim_active()) {
    if (post(DBG_OP_SIM_ENABLE, DIR_STOP, 0, 0, false))
      line("SIM DISABLE\r\n  motor inhibit released\r\n  adc source: "
           "hardware\r\n  sim OFF");
    return;
  }
  armed = coupled = false;
  if (post(DBG_OP_SIM_ENABLE, DIR_STOP, 0, 0, true))
    line("SIM ENABLE\r\n  motor inhibited (STBY forced low)\r\n  adc source: "
         "simulated, starting at 372 (position 1)\r\n  sim ON");
  else
    line("rejected: debug mailbox busy");
}
void dbg_sim_set_value(uint16_t adc) {
  char b[112];
  if (!encoder_sim_active()) {
    line("rejected: enable simulation first");
    return;
  }
  encoder_sim_set(adc);
  snprintf(b, sizeof b,
           "SIM ADC %u (classification updates through filter and debounce)",
           adc);
  line(b);
}
void dbg_sim_set_position(position_t pos) {
  char b[96];
  if (!encoder_sim_active()) {
    line("rejected: enable simulation first");
    return;
  }
  encoder_sim_set(sim_nominal(pos));
  snprintf(b, sizeof b, "SIM POSITION %u, adc %u", pos, sim_nominal(pos));
  line(b);
}
void dbg_sim_travel(position_t from, position_t to, uint16_t ms) {
  char b[96];
  if (!encoder_sim_active()) {
    line("rejected: enable simulation first");
    return;
  }
  if (from == to) {
    line("rejected: from and to must differ");
    return;
  }
  sim_from = from;
  sim_to = to;
  sim_band_ms = ms;
  encoder_sim_set(sim_nominal(from));
  snprintf(b, sizeof b, "SIM TRAVEL %u -> %u, %u ms/position", from, to, ms);
  line(b);
  action = ACT_SIM_TRAVEL;
  action_stage = 0;
  action_started = ms_now();
  action_deadline = action_started + DEBUG_SIM_SWEEP_SETTLE_MS;
  action_count = 0;
  action_pass = true;
}
void dbg_sim_park(void) {
  if (!encoder_sim_active()) {
    line("rejected: enable simulation first");
    return;
  }
  line("SIM PARK BETWEEN STATIONS");
  encoder_sim_set((encoder_nominal(2) + encoder_nominal(3)) / 2u);
  action = ACT_SIM_PARK;
  action_started = ms_now();
  action_deadline = action_started + DEBUG_SIM_SWEEP_SETTLE_MS;
  action_stage = 0;
}
void dbg_sim_overtravel(position_t limit) {
  char b[64];
  if (!encoder_sim_active()) {
    line("rejected: enable simulation first");
    return;
  }
  snprintf(b, sizeof b, "SIM OVERTRAVEL LIMIT %u", limit);
  line(b);
  sim_from = limit;
  encoder_sim_set(sim_nominal(limit));
  action = ACT_SIM_DRIFT;
  action_stage = 0;
  action_deadline = ms_now() + DEBUG_SIM_SWEEP_SETTLE_MS;
}
void dbg_sim_sweep(void) {
  if (!encoder_sim_active()) {
    line("rejected: enable simulation first");
    return;
  }
  (void)post(DBG_OP_ENTER, DIR_STOP, 0, 0, false);
  line("SIM SWEEP WINDOWS");
  encoder_sim_set(0);
  action = ACT_SIM_SWEEP;
  action_count = 0;
  action_last_pos = POS_UNKNOWN;
  action_pass = true;
  action_deadline = ms_now() + DEBUG_SIM_SWEEP_SETTLE_MS;
}
#endif

static void action_sample_adc(void) {
  uint16_t v = encoder_average();
  if (v < action_min)
    action_min = v;
  if (v > action_max)
    action_max = v;
  action_sum += v;
  ++action_count;
}

static void action_poll(uint32_t now) {
  static const position_t step_targets[] = {4, 3, 2, 3};
  static const position_t self_targets[] = {2, 3, 2, 1};
  char b[128];
  if (action == ACT_NONE || action == ACT_CAL_POS_WAIT)
    return;
  if (action == ACT_BENCH_PINS) {
    if ((int32_t)(now - action_deadline) < 0)
      return;
    uint slice = pwm_gpio_to_slice_num(PIN_PWMA);
    uint32_t cc = pwm_hw->slice[slice].cc;
    uint16_t level = (uint16_t)(pwm_gpio_to_channel(PIN_PWMA) ? cc >> 16 : cc);
    snprintf(b, sizeof b,
             "  GP%u AIN1 %u  GP%u AIN2 %u  GP%u PWMA pwm, level %u / %u",
             PIN_AIN1, gpio_get(PIN_AIN1), PIN_AIN2, gpio_get(PIN_AIN2),
             PIN_PWMA, level, PWM_WRAP);
    line(b);
    snprintf(b, sizeof b, "  GP%u STBY %u  GP%u SENSE adc %u", PIN_STBY,
             gpio_get(PIN_STBY), PIN_SENSE, encoder_raw());
    line(b);
    action_deadline = now + DEBUG_BENCH_PIN_PERIOD_MS;
    return;
  }
  if (action == ACT_GPIO_WALK) {
    if ((int32_t)(now - action_deadline) < 0)
      return;
    if (action_stage == 0u) {
      (void)post(DBG_OP_GPIO_SET, DIR_STOP, DEBUG_GPIO_OP_AIN1, 0, false);
      line("  AIN1 high ...");
    } else if (action_stage == 1u) {
      (void)post(DBG_OP_GPIO_SET, DIR_STOP, DEBUG_GPIO_OP_AIN2, 0, false);
      line("  AIN1 low\r\n  AIN2 high ...");
    } else if (action_stage == 2u) {
      (void)post(DBG_OP_GPIO_SET, DIR_STOP, DEBUG_GPIO_OP_STBY, 0, false);
      line("  AIN2 low\r\n  STBY high ...");
    } else {
      (void)post(DBG_OP_GPIO_SET, DIR_STOP, DEBUG_GPIO_OP_ALL_LOW, 0, false);
      line("  STBY low\r\n  done");
      action = ACT_NONE;
      return;
    }
    ++action_stage;
    action_deadline = now + DEBUG_GPIO_WALK_STEP_MS;
    return;
  }
  if (action == ACT_TICK_HEALTH) {
    if ((int32_t)(now - action_deadline) < 0)
      return;
    tick_stats_t s;
    controller_timing_get(&s);
    uint32_t elapsed = now - action_started, ticks = s.count - bench_tick_start,
             rate10 = (ticks * DEBUG_FREQ_TENTHS * DEBUG_SELFTEST_WINDOW_MS) /
                      elapsed;
    bool pass =
        rate10 >=
            (TICK_HZ - DEBUG_TICK_RATE_TOLERANCE_HZ) * DEBUG_FREQ_TENTHS &&
        rate10 <= (TICK_HZ + DEBUG_TICK_RATE_TOLERANCE_HZ) * DEBUG_FREQ_TENTHS;
    snprintf(
        b, sizeof b,
        "  measured rate   %lu.%lu Hz over %lu.%02lu s     %s (1000 +/- 20)",
        (unsigned long)(rate10 / 10u), (unsigned long)(rate10 % 10u),
        (unsigned long)(elapsed / 1000u),
        (unsigned long)((elapsed % 1000u) / 10u), pass ? "PASS" : "FAIL");
    line(b);
    snprintf(b, sizeof b,
             "  duration        min %lu us  max %lu us  mean %llu us",
             (unsigned long)s.min_us, (unsigned long)s.max_us,
             s.count ? (unsigned long long)(s.sum_us / s.count) : 0ull);
    line(b);
    snprintf(b, sizeof b, "  overruns        %lu", (unsigned long)s.overruns);
    line(b);
    line("  watchdog        enabled, 100 ms, pause_on_debug true");
    snprintf(b, sizeof b, "  time to expiry  %lu ms at last read",
             (unsigned long)(watchdog_get_time_remaining_ms()));
    line(b);
    action = ACT_NONE;
    return;
  }
  if (action == ACT_SIM_TRAVEL) {
    if (action_stage == 0u) {
      if (encoder_confirmed() != sim_from ||
          (int32_t)(now - action_deadline) < 0)
        return;
      if (controller_request(REQ_MOVE, sim_to) != MOVE_OK) {
        line("  RESULT: FAIL  (move request rejected)");
        action = ACT_NONE;
        return;
      }
      snprintf(b, sizeof b, "  > move %u\r\n  OK: moving to %u", sim_to,
               sim_to);
      line(b);
      action_last_pos = sim_from;
      action_stage = 1u;
      action_deadline = now + sim_band_ms;
      return;
    }
    if (action_stage == 1u) {
      if ((int32_t)(now - action_deadline) < 0)
        return;
      position_t next = action_last_pos + (sim_to > sim_from ? 1 : -1);
      encoder_sim_set(sim_nominal(next));
      action_last_pos = next;
      action_deadline = now + sim_band_ms;
      if (next == sim_to)
        action_stage = 2u;
      return;
    }
    if (controller_state() == ST_FAULT) {
      line("  RESULT: FAIL  (controller fault)");
      action = ACT_NONE;
      return;
    }
    if (controller_state() == ST_IDLE && controller_position() == sim_to) {
      uint32_t expected =
          sim_from > sim_to ? sim_from - sim_to : sim_to - sim_from;
      action_pass = sim_history_ok(sim_from, sim_to);
      snprintf(b, sizeof b, "  final state IDLE, pos %u", sim_to);
      line(b);
      snprintf(b, sizeof b,
               "  RESULT: %s  (%lu events expected, order checked by "
               "controller history)",
               action_pass ? "PASS" : "FAIL", (unsigned long)expected);
      line(b);
      action = ACT_NONE;
    }
    return;
  }
  if (action == ACT_SIM_PARK) {
    if ((int32_t)(now - action_deadline) < 0)
      return;
    line(encoder_confirmed() == POS_BETWEEN && controller_state() == ST_IDLE
             ? "  RESULT: PASS  (known between-station angle, controller idle)"
             : "  RESULT: FAIL  (between-station classification or state "
               "incorrect)");
    action = ACT_NONE;
    return;
  }
  if (action == ACT_SIM_DRIFT) {
    if (action_stage == 0u) {
      if ((int32_t)(now - action_deadline) < 0 ||
          encoder_confirmed() != sim_from)
        return;
      uint16_t injected =
          sim_from == POS_MIN ? CFG_ADC_SAFE_MIN - 1u : CFG_ADC_SAFE_MAX + 1u;
      snprintf(b, sizeof b, "  established pos %u (adc %u)", sim_from,
               sim_nominal(sim_from));
      line(b);
      encoder_sim_set(injected);
      snprintf(b, sizeof b, "  injected adc %u beyond safe range", injected);
      line(b);
      action_stage = 1u;
      action_started = now;
      return;
    }
    if (controller_state() != ST_FAULT)
      return;
    snprintf(b, sizeof b, "  fault after %lu ms, dir %s, STBY %u",
             (unsigned long)(now - action_started), dir_text(motor_direction()),
             gpio_get(PIN_STBY));
    line(b);
    line(motor_direction() == DIR_STOP && !gpio_get(PIN_STBY)
             ? "  RESULT: PASS  (braked, disabled, faulted)"
             : "  RESULT: FAIL  (unsafe terminal state)");
    action = ACT_NONE;
    return;
  }
  if (action == ACT_SIM_SWEEP) {
    if ((int32_t)(now - action_deadline) < 0)
      return;
    uint16_t value = (uint16_t)action_count;
    position_t expected = POS_BETWEEN;
    if (value < CFG_ADC_SAFE_MIN || value > CFG_ADC_SAFE_MAX)
      expected = POS_UNKNOWN;
    else
      for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
        uint16_t n = encoder_nominal(p), d = value > n ? value - n : n - value;
        if (d <= CFG_POS_WINDOW) {
          expected = p;
          break;
        }
      }
    if (encoder_confirmed() != expected)
      action_pass = false;
    if (action_count == ADC_MAX_VALUE) {
      snprintf(b, sizeof b, "  safe range %u..%u", CFG_ADC_SAFE_MIN,
               CFG_ADC_SAFE_MAX);
      line(b);
      for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
        uint16_t n = encoder_nominal(p);
        snprintf(b, sizeof b, "  position %u window %u..%u", p,
                 n - CFG_POS_WINDOW, n + CFG_POS_WINDOW);
        line(b);
      }
      line(action_pass
               ? "  RESULT: PASS  (0..4095 matches windows and safe range)"
               : "  RESULT: FAIL  (classification mismatch)");
      (void)post(DBG_OP_SIM_ENABLE, DIR_STOP, 0, 0, true);
      action = ACT_NONE;
      return;
    }
    ++action_count;
    encoder_sim_set((uint16_t)action_count);
    action_deadline = now + DEBUG_SIM_SWEEP_SETTLE_MS;
    return;
  }
  if (action == ACT_STATIC) {
    if (action_stage == 0u) {
      if ((int32_t)(now - action_deadline) < 0)
        return;
      self_adc[action_count++] = encoder_raw();
      self_all_full = self_all_full && console_event_queue_full();
      if (action_count < DEBUG_SELFTEST_ADC_SAMPLES) {
        action_deadline = now + DEBUG_SELFTEST_SAMPLE_PERIOD_MS;
        return;
      }
      tick_stats_t stats;
      controller_timing_get(&stats);
      self_tick_start = stats.count;
      action_started = now;
      action_deadline = now + DEBUG_SELFTEST_WINDOW_MS;
      action_stage = 1u;
      return;
    }
    if ((int32_t)(now - action_deadline) < 0) {
      self_all_full = self_all_full && console_event_queue_full();
      return;
    }
    tick_stats_t stats;
    controller_timing_get(&stats);
    uint32_t elapsed = now - action_started;
    uint32_t expected = (elapsed * TICK_HZ) / DEBUG_SELFTEST_WINDOW_MS;
    uint32_t ticks = stats.count - self_tick_start;
    bool adc_ok = true, all_same = true;
    for (uint8_t i = 0; i < DEBUG_SELFTEST_ADC_SAMPLES; ++i) {
      if (i && self_adc[i] != self_adc[0])
        all_same = false;
      if (self_adc[i] == 0u ||
          (self_adc[i] == ADC_MAX_VALUE && encoder_instant() != POS_UNKNOWN))
        adc_ok = false;
    }
    adc_ok = adc_ok && !all_same;
    bool ordered =
        CFG_POS_1_ADC < CFG_POS_2_ADC && CFG_POS_2_ADC < CFG_POS_3_ADC &&
        CFG_POS_3_ADC < CFG_POS_4_ADC && CFG_POS_4_ADC < CFG_POS_5_ADC;
    uint16_t gap = CFG_POS_2_ADC - CFG_POS_1_ADC,
             d = CFG_POS_3_ADC - CFG_POS_2_ADC;
    if (d < gap)
      gap = d;
    d = CFG_POS_4_ADC - CFG_POS_3_ADC;
    if (d < gap)
      gap = d;
    d = CFG_POS_5_ADC - CFG_POS_4_ADC;
    if (d < gap)
      gap = d;
    bool windows = (uint32_t)CFG_POS_WINDOW * 4u < gap;
    bool duties = CFG_DUTY_MIN <= CFG_DUTY_CREEP &&
                  CFG_DUTY_CREEP <= CFG_DUTY_APPROACH &&
                  CFG_DUTY_APPROACH <= CFG_DUTY_NORMAL;
    bool timeouts = CFG_TIMEOUT_HOME_MS >= 4u * CFG_TIMEOUT_STEP_MS;
    bool tick_ok = ticks + DEBUG_SELFTEST_TICK_TOLERANCE >= expected &&
                   ticks <= expected + DEBUG_SELFTEST_TICK_TOLERANCE;
    bool queue_ok = !self_all_full;
    position_t pos = encoder_confirmed();
    bool position_ok = pos == POS_UNKNOWN || pos == POS_BETWEEN ||
                       (pos >= POS_MIN && pos <= POS_MAX);
    snprintf(b, sizeof b, "  ADC responds %s", adc_ok ? "PASS" : "FAIL");
    line(b);
    snprintf(b, sizeof b, "  positions ordered %s  windows %s",
             ordered ? "PASS" : "FAIL", windows ? "PASS" : "FAIL");
    line(b);
    snprintf(b, sizeof b, "  duties %s  timeouts %s", duties ? "PASS" : "FAIL",
             timeouts ? "PASS" : "FAIL");
    line(b);
    snprintf(b, sizeof b, "  tick %lu/%lu %s  queue %s  position %s",
             (unsigned long)ticks, (unsigned long)expected,
             tick_ok ? "PASS" : "FAIL", queue_ok ? "PASS" : "FAIL",
             position_ok ? "PASS" : "FAIL");
    line(b);
    line(adc_ok && ordered && windows && duties && timeouts && tick_ok &&
                 queue_ok && position_ok
             ? "SELFTEST STATIC PASS"
             : "SELFTEST STATIC FAIL");
    action = ACT_NONE;
    return;
  }
  if (action == ACT_FINDMIN_BASE) {
    action_sample_adc();
    if ((int32_t)(now - action_deadline) < 0)
      return;
    noise_floor = action_max - action_min;
    action_start_adc = encoder_average();
    dbg_motor_pulse((direction_t)action_stage, action_duty,
                    DEBUG_FINDMIN_PULSE_MS);
    action_started = now;
    action_deadline = now + DEBUG_FINDMIN_PULSE_MS;
    action_min = ADC_MAX_VALUE;
    action_max = 0;
    action = ACT_FINDMIN_PULSE;
    return;
  }
  if (action == ACT_FINDMIN_PULSE) {
    action_sample_adc();
    if ((int32_t)(now - action_deadline) < 0)
      return;
    uint16_t change = encoder_average() > action_start_adc
                          ? encoder_average() - action_start_adc
                          : action_start_adc - encoder_average();
    bool moved = change > noise_floor;
    snprintf(
        b, sizeof b,
        moved ? "  duty %u  MOTION (adc %u -> %u, delta %u, noise floor %u)"
              : "  duty %u  no motion (adc %u -> %u, delta %u, noise floor %u)",
        action_duty, action_start_adc, encoder_average(), change, noise_floor);
    line(b);
    if (moved) {
      uint16_t suggested =
          (uint16_t)((action_duty *
                          (DEBUG_PERCENT_SCALE + DEBUG_FINDMIN_MARGIN_PERCENT) +
                      DEBUG_PERCENT_SCALE - 1u) /
                     DEBUG_PERCENT_SCALE);
      snprintf(b, sizeof b,
               "FINDMIN result=%u  suggest DUTY_MIN=%u (result +10%% margin)",
               action_duty, suggested);
      line(b);
      action = ACT_NONE;
      return;
    }
    if (action_duty >= DEBUG_FINDMIN_DUTY_MAX) {
      line("FINDMIN stopped at duty 120 without motion");
      action = ACT_NONE;
      return;
    }
    action_duty += DEBUG_FINDMIN_DUTY_STEP;
    action_start_adc = encoder_average();
    dbg_motor_pulse((direction_t)action_stage, action_duty,
                    DEBUG_FINDMIN_PULSE_MS);
    action_deadline = now + DEBUG_FINDMIN_PULSE_MS;
    action_min = ADC_MAX_VALUE;
    action_max = 0;
    return;
  }
  if (action == ACT_CAL_POS_SAMPLE) {
    action_sample_adc();
    if ((int32_t)(now - action_deadline) < 0)
      return;
    action_means[action_stage - 1u] = (uint16_t)(action_sum / action_count);
    snprintf(b, sizeof b, "  pos%u mean=%lu spread=%u", action_stage,
             (unsigned long)(action_sum / action_count),
             action_max - action_min);
    line(b);
    if (action_stage < POS_MAX) {
      ++action_stage;
      snprintf(b, sizeof b, " Move to position %u, press SPACE", action_stage);
      line(b);
      action = ACT_CAL_POS_WAIT;
    } else {
      cfg.pos_1_adc = action_means[0];
      cfg.pos_2_adc = action_means[1];
      cfg.pos_3_adc = action_means[2];
      cfg.pos_4_adc = action_means[3];
      cfg.pos_5_adc = action_means[4];
      line("CALIBRATED POSITION VALUES");
      dbg_position_table();
      for (uint8_t i = 1; i < POS_MAX; ++i)
        if ((uint16_t)(action_means[i] - action_means[i - 1u]) <
            DEBUG_CAL_MIN_SEPARATION)
          line("  MARGINAL adjacent separation under 200");
      action = ACT_NONE;
    }
    return;
  }
  if (action == ACT_CAL_STEP) {
    if (action_stage >= 4u) {
      uint32_t suggest =
          ((action_worst * 2u + DEBUG_CAL_ROUND_MS - 1u) / DEBUG_CAL_ROUND_MS) *
          DEBUG_CAL_ROUND_MS;
      snprintf(
          b, sizeof b,
          "  worst=%lu  suggest TIMEOUT_STEP_MS=%lu (worst x2, rounded up)",
          (unsigned long)action_worst, (unsigned long)suggest);
      line(b);
      action = ACT_NONE;
      return;
    }
    position_t dst = step_targets[action_stage];
    if (!action_started) {
      if (controller_request(REQ_MOVE, dst) == MOVE_OK)
        action_started = now;
      return;
    }
    if (controller_state() == ST_FAULT) {
      line("CAL STEP failed");
      action = ACT_NONE;
      return;
    }
    if (controller_state() == ST_IDLE && controller_position() == dst) {
      uint32_t elapsed = now - action_started;
      if (elapsed > action_worst)
        action_worst = elapsed;
      static const position_t srcs[] = {3, 4, 3, 2};
      snprintf(b, sizeof b, "  %u->%u  %lu ms", srcs[action_stage], dst,
               (unsigned long)elapsed);
      line(b);
      ++action_stage;
      action_started = 0;
    }
    return;
  }
  if (action == ACT_CAL_TRAVEL) {
    if (!action_started) {
      if (controller_request(REQ_HOME, 0) == MOVE_OK)
        action_started = now;
      return;
    }
    if (controller_state() == ST_IDLE && controller_position() == 1) {
      uint32_t elapsed = now - action_started;
      snprintf(b, sizeof b, "  5->1 %lu ms  suggest TIMEOUT_HOME_MS=%lu",
               (unsigned long)elapsed, (unsigned long)(elapsed * 2u));
      line(b);
      action = ACT_NONE;
    } else if (controller_state() == ST_FAULT) {
      line("CAL TRAVEL failed");
      action = ACT_NONE;
    }
    return;
  }
  if (action == ACT_CAL_OVER) {
    static const uint8_t duties[] = {
        DEBUG_OVERSHOOT_DUTY_HIGH, DEBUG_OVERSHOOT_DUTY_HIGH,
        DEBUG_OVERSHOOT_DUTY_MID,  DEBUG_OVERSHOOT_DUTY_MID,
        DEBUG_OVERSHOOT_DUTY_LOW,  DEBUG_OVERSHOOT_DUTY_LOW};
    static const position_t sources[] = {2, 4, 2, 4, 2, 4};
    if (action_stage >= DEBUG_OVERSHOOT_TEST_COUNT) {
      cfg.duty_approach = action_saved_duty;
      line("  suggest DUTY_APPROACH=60");
      action = ACT_NONE;
      return;
    }
    position_t source = sources[action_stage];
    action_duty = duties[action_stage];
    if (controller_state() == ST_FAULT) {
      cfg.duty_approach = action_saved_duty;
      line("CAL OVERSHOOT failed");
      action = ACT_NONE;
      return;
    }
    if (action_count == 0u && controller_position() != source) {
      if (controller_state() == ST_IDLE &&
          controller_request(REQ_MOVE, source) == MOVE_OK)
        action_count = 1u;
      return;
    }
    if (action_count == 1u) {
      if (controller_state() == ST_IDLE && controller_position() == source)
        action_count = 0u;
      else
        return;
    }
    if (!action_started) {
      cfg.duty_approach = action_duty;
      if (controller_request(REQ_MOVE, 3) == MOVE_OK)
        action_started = now;
      return;
    }
    if (controller_state() == ST_IDLE && controller_position() == 3) {
      int32_t off = (int32_t)encoder_average() - DEBUG_NOMINAL_P3;
      snprintf(b, sizeof b, "  duty %u  settle=%u  offset=%+ld", action_duty,
               encoder_average(), (long)off);
      line(b);
      ++action_stage;
      action_started = 0;
    }
    return;
  }
  if (action == ACT_SELFTEST) {
    if (action_stage == 0 && !action_started) {
      if (controller_request(REQ_HOME, 0) == MOVE_OK)
        action_started = now;
      return;
    }
    if (controller_state() == ST_FAULT) {
      line("RESULT: motion self-test FAIL");
      action = ACT_NONE;
      return;
    }
    if (action_stage == 0 && controller_state() == ST_IDLE &&
        controller_position() == 1) {
      snprintf(b, sizeof b, "  home        ARR 1   %lu ms  PASS",
               (unsigned long)(now - action_started));
      line(b);
      action_stage = 1;
      action_started = 0;
      return;
    }
    if (action_stage >= 1 && action_stage <= 4) {
      position_t dst = self_targets[action_stage - 1u];
      if (!action_started) {
        if (controller_request(REQ_MOVE, dst) == MOVE_OK)
          action_started = now;
        return;
      }
      if (controller_state() == ST_IDLE && controller_position() == dst) {
        snprintf(b, sizeof b, "  step        ARR %u   %lu ms  PASS", dst,
                 (unsigned long)(now - action_started));
        line(b);
        ++action_stage;
        action_started = 0;
        return;
      }
    }
    if (action_stage == 5) {
      line("RESULT: 5/5 PASS");
      action = ACT_NONE;
    }
    return;
  }
}
