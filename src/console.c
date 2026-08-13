#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "hardware/gpio.h"
#include "hardware/regs/uart.h"
#include "hardware/structs/uart.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "motor.h"
#include "pico/time.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef LUFTFUGL_MONITOR
#include "debug.h"
#endif
typedef struct {
  event_kind_t kind;
  uint8_t arg;
} event_t;
static char line_buffer[CONSOLE_LINE_MAX + 1];
static uint8_t line_length;
static bool discard_line;
static volatile event_t event_queue[EVENT_QUEUE_DEPTH];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;
static volatile uint32_t rx_chars;
static volatile uint32_t rx_overruns;
static volatile uint32_t poll_calls;
static volatile uint32_t poll_max_gap_us;
static volatile uint32_t tx_spin_us;
static uint32_t last_poll_us;
static volatile char rx_ring[128];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;

static void console_uart_rx_irq(void) {
  while (uart_is_readable(uart0)) {
    uint32_t received = uart_get_hw(uart0)->dr;
    ++rx_chars;
    if (received & (UART_UARTDR_OE_BITS | UART_UARTDR_BE_BITS |
                    UART_UARTDR_PE_BITS | UART_UARTDR_FE_BITS)) {
      ++rx_overruns;
      uart_get_hw(uart0)->rsr = 0u;
      continue;
    }
    uint8_t next = (uint8_t)((rx_head + 1u) % sizeof rx_ring);
    if (next != rx_tail) {
      rx_ring[rx_head] = (char)(received & UART_UARTDR_DATA_BITS);
      rx_head = next;
    }
  }
}

static void write_text(const char *text) {
  while (*text) {
    uint32_t started = time_us_32();
    uart_putc_raw(uart0, *text++);
    tx_spin_us += time_us_32() - started;
  }
}
static void write_line(const char *text) {
  write_text(text);
  write_text("\r\n");
}
static const char *state_name(sys_state_t state) {
  static const char *const names[] = {
      "BOOT",  "IDLE", "MOVING", "APPROACH", "HOMING", "FAULT",
#ifdef LUFTFUGL_MONITOR
      "DEBUG",
#endif
  };
  return (unsigned)state < sizeof names / sizeof names[0] ? names[state]
                                                          : "FAULT";
}
static const char *direction_name(direction_t direction) {
  if (direction == DIR_FWD)
    return "FWD";
  if (direction == DIR_REV)
    return "REV";
  return "STP";
}
static void print_position(const char *prefix, position_t position) {
  char output[16];
  if (position == POS_UNKNOWN || position == POS_BETWEEN)
    snprintf(output, sizeof output, "%s?", prefix);
  else
    snprintf(output, sizeof output, "%s%u", prefix, position);
  write_line(output);
}
static void console_status(void) {
  char output[80];
  position_t position = controller_position();
  if (position == POS_UNKNOWN || position == POS_BETWEEN)
    snprintf(output, sizeof output, "POS:? DIR:%s SPD:%u STATE:%s",
             direction_name(motor_direction()), motor_duty(),
             state_name(controller_state()));
  else
    snprintf(output, sizeof output, "POS:%u DIR:%s SPD:%u STATE:%s", position,
             direction_name(motor_direction()), motor_duty(),
             state_name(controller_state()));
  write_line(output);
}
static void console_adc(void) {
  char output[40];
  position_t position = encoder_instant();
  if (position == POS_UNKNOWN || position == POS_BETWEEN)
    snprintf(output, sizeof output, "ADC raw=%u avg=%u pos=?", encoder_raw(),
             encoder_average());
  else
    snprintf(output, sizeof output, "ADC raw=%u avg=%u pos=%u", encoder_raw(),
             encoder_average(), position);
  write_line(output);
}
static void handle_move(char *argument) {
  char *end;
  long target;
  char output[32];
  move_result_t result;
  if (!argument || !*argument) {
    write_line("ERR: invalid target");
    return;
  }
  target = strtol(argument, &end, 10);
  while (isspace((unsigned char)*end))
    end++;
  if (end == argument || *end) {
    write_line("ERR: invalid target");
    return;
  }
  if ((controller_position() == POS_MIN && target < POS_MIN) ||
      (controller_position() == POS_MAX && target > POS_MAX))
    result = MOVE_ENDSTOP;
  else if (target < POS_MIN || target > POS_MAX)
    result = MOVE_INVALID;
  else
    result = controller_request(REQ_MOVE, (position_t)target);
  switch (result) {
  case MOVE_OK:
    snprintf(output, sizeof output, "OK: moving to %ld", target);
    break;
  case MOVE_ALREADY:
    snprintf(output, sizeof output, "OK: already at %ld", target);
    break;
  case MOVE_INVALID:
    snprintf(output, sizeof output, "ERR: invalid target");
    break;
  case MOVE_ENDSTOP:
    snprintf(output, sizeof output, "ERR: at end-stop");
    break;
  case MOVE_BUSY:
    snprintf(output, sizeof output, "ERR: busy");
    break;
  case MOVE_POS_UNKNOWN:
    snprintf(output, sizeof output, "ERR: position unknown");
    break;
  default:
    snprintf(output, sizeof output, "ERR: fault");
    break;
  }
  write_line(output);
}
static void handle_jog(char *argument) {
  char *end;
  long delta;
  char output[48];
  uint16_t from_adc;
  if (!argument || !*argument) {
    write_line("ERR: invalid jog");
    return;
  }
  delta = strtol(argument, &end, 10);
  while (isspace((unsigned char)*end))
    end++;
  if (end == argument || *end || delta < -JOG_MAX_COUNTS ||
      delta > JOG_MAX_COUNTS) {
    write_line("ERR: invalid jog");
    return;
  }
  switch (controller_request_jog((int16_t)delta, &from_adc)) {
  case JOG_OK:
    snprintf(output, sizeof output, "OK: jog %+ld from %u", delta, from_adc);
    break;
  case JOG_INVALID:
    snprintf(output, sizeof output, "ERR: invalid jog");
    break;
  case JOG_ENDSTOP:
    snprintf(output, sizeof output, "ERR: at end-stop");
    break;
  case JOG_OVERTRAVEL:
    snprintf(output, sizeof output, "ERR: overtravel");
    break;
  case JOG_BUSY:
    snprintf(output, sizeof output, "ERR: busy");
    break;
  default:
    snprintf(output, sizeof output, "ERR: fault");
    break;
  }
  write_line(output);
}
static void handle_setpos(char *argument) {
  char *end;
  long station;
  char output[32];
  uint16_t adc = encoder_average();
  if (!argument || !*argument) {
    write_line("ERR: invalid target");
    return;
  }
  station = strtol(argument, &end, 10);
  while (isspace((unsigned char)*end))
    end++;
  if (end == argument || *end || station < POS_MIN || station > POS_MAX) {
    write_line("ERR: invalid target");
    return;
  }
  switch (controller_request_setpos((position_t)station, adc)) {
  case MOVE_OK:
    snprintf(output, sizeof output, "OK: pos %ld = %u", station, adc);
    break;
  case MOVE_BUSY:
    snprintf(output, sizeof output, "ERR: busy");
    break;
  case MOVE_FAULT:
    snprintf(output, sizeof output, "ERR: fault");
    break;
  default:
    snprintf(output, sizeof output, "ERR: invalid target");
    break;
  }
  write_line(output);
}
static void console_savepos(void) {
  char output[32];
  for (position_t p = POS_MIN; p <= POS_MAX; ++p) {
    snprintf(output, sizeof output, "#define POS_%u_ADC %u", p,
             encoder_nominal(p));
    write_line(output);
  }
}
static void console_handle_line(char *line) {
  char *argument;
  char *p, *end = line + strlen(line);
  while (end > line && isspace((unsigned char)end[-1]))
    *--end = '\0';
  argument = strchr(line, ' ');
  if (argument) {
    *argument++ = '\0';
    while (*argument == ' ')
      argument++;
  }
  for (p = line; *p; p++)
    *p = (char)tolower((unsigned char)*p);
  if (!strcmp(line, "pos") && !argument)
    print_position("POS:", controller_position());
  else if (!strcmp(line, "adc") && !argument)
    console_adc();
  else if (!strcmp(line, "jog"))
    handle_jog(argument);
  else if (!strcmp(line, "setpos"))
    handle_setpos(argument);
  else if (!strcmp(line, "savepos") && !argument)
    console_savepos();
  else if (!strcmp(line, "move"))
    handle_move(argument);
  else if (!strcmp(line, "stop") && !argument) {
    (void)controller_request(REQ_STOP, POS_UNKNOWN);
    write_line("OK: stopped");
  } else if (!strcmp(line, "status") && !argument)
    console_status();
  else if (!strcmp(line, "home") && !argument) {
    (void)controller_request(REQ_HOME, POS_UNKNOWN);
    write_line("OK: homing");
  }
#ifdef LUFTFUGL_MONITOR
  else if (!strcmp(line, "dbg") && !argument)
    dbg_enter();
  else if (!strcmp(line, "dbg") && argument && !strcmp(argument, "plain"))
    dbg_enter_plain();
#endif
  else
    write_line("ERR: unknown command");
}
void console_init(void) {
  uart_init(uart0, UART_BAUD);
  gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
  gpio_pull_up(PIN_UART_RX);
  uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
  uart_set_hw_flow(uart0, false, false);
  uart_set_fifo_enabled(uart0, true);
  uart_get_hw(uart0)->rsr = 0u;
  line_length = 0;
  discard_line = false;
  event_head = 0;
  event_tail = 0;
  rx_chars = 0u;
  rx_overruns = 0u;
  poll_calls = 0u;
  poll_max_gap_us = 0u;
  tx_spin_us = 0u;
  last_poll_us = 0u;
  rx_head = 0u;
  rx_tail = 0u;
  irq_set_exclusive_handler(UART0_IRQ, console_uart_rx_irq);
  irq_set_enabled(UART0_IRQ, true);
  uart_set_irq_enables(uart0, true, false);
  write_line("luftfugl motor fw " FW_VERSION);
}
void console_poll(void) {
  uint32_t now = time_us_32();
  if (last_poll_us != 0u) {
    uint32_t gap = now - last_poll_us;
    if (gap > poll_max_gap_us)
      poll_max_gap_us = gap;
  }
  last_poll_us = now;
  ++poll_calls;
  while (rx_tail != rx_head) {
    char c = rx_ring[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) % sizeof rx_ring);
#ifdef LUFTFUGL_MONITOR
    if (dbg_active()) {
      dbg_handle_key(c);
      continue;
    }
#endif
    if (c == '\r')
      continue;
    if (c == '\n') {
      if (!discard_line && line_length) {
        line_buffer[line_length] = '\0';
        console_handle_line(line_buffer);
      }
      line_length = 0;
      discard_line = false;
    } else if (!discard_line) {
      if (line_length < CONSOLE_LINE_MAX)
        line_buffer[line_length++] = c;
      else {
        write_line("ERR: line too long");
        discard_line = true;
      }
    }
  }
}
void console_push_event(event_kind_t kind, uint8_t arg) {
  uint8_t next = (uint8_t)((event_head + 1u) % EVENT_QUEUE_DEPTH);
  if (next == event_tail)
    return;
  event_queue[event_head].kind = kind;
  event_queue[event_head].arg = arg;
  event_head = next;
}
void console_drain_events(void) {
  while (event_tail != event_head) {
    event_t event = event_queue[event_tail];
    char output[32];
    event_tail = (uint8_t)((event_tail + 1u) % EVENT_QUEUE_DEPTH);
#ifdef LUFTFUGL_MONITOR
    if (dbg_active()) {
      dbg_event(event.kind, event.arg);
      continue;
    }
#endif
    switch (event.kind) {
    case EV_PASS:
      snprintf(output, sizeof output, "PASS:%u", event.arg);
      break;
    case EV_ARRIVE:
      snprintf(output, sizeof output, "ARR:%u", event.arg);
      break;
    case EV_TIMEOUT:
      snprintf(output, sizeof output, "ERR: timeout");
      break;
    case EV_FAULT_HOME:
      snprintf(output, sizeof output, "ERR: fault home timeout");
      break;
    case EV_HOMING:
      snprintf(output, sizeof output, "OK: homing");
      break;
    case EV_STOPPED_UNKNOWN:
      snprintf(output, sizeof output, "POS:?");
      break;
    case EV_FAULT_OVERTRAVEL:
      snprintf(output, sizeof output, "ERR: overtravel");
      break;
    case EV_FAULT_STALL:
      snprintf(output, sizeof output, "ERR: stall");
      break;
    case EV_FAULT_DIRECTION:
      snprintf(output, sizeof output, "ERR: direction");
      break;
    case EV_JOG_COMPLETE:
      console_adc();
      continue;
    default:
      continue;
    }
    write_line(output);
  }
}
void console_watchdog_reset(void) { write_line("ERR: watchdog reset"); }
void console_timer_alloc_failed(void) { write_line("ERR: timer alloc failed"); }
#ifdef LUFTFUGL_MONITOR
void console_debug_write(const char *text) { dbg_out_push(text); }
void console_debug_line(const char *text) {
  dbg_out_push(text);
  dbg_out_push("\r\n");
}
bool console_event_queue_full(void) {
  return (uint8_t)((event_head + 1u) % EVENT_QUEUE_DEPTH) == event_tail;
}
void console_diag_format(char *output, size_t output_size) {
  snprintf(output, output_size,
           "rx_chars=%lu rx_overruns=%lu poll_calls=%lu "
           "poll_max_gap_us=%lu tx_spin_us=%lu",
           (unsigned long)rx_chars, (unsigned long)rx_overruns,
           (unsigned long)poll_calls, (unsigned long)poll_max_gap_us,
           (unsigned long)tx_spin_us);
}
void console_diag_note_tx_spin(uint32_t elapsed_us) {
  tx_spin_us += elapsed_us;
}
#endif
