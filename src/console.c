#include "console.h"

#include "controller.h"
#include "encoder.h"
#include "hardware/gpio.h"
#include "hardware/structs/uart.h"
#include "hardware/uart.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  event_kind_t kind;
  uint8_t arg;
} event_t;

static char line_buffer[CONSOLE_LINE_MAX + 1u];
static uint8_t line_length;
static bool discard_line;
static bool swallow_lf;
static volatile event_t event_queue[EVENT_QUEUE_DEPTH];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;

static void write_text(const char *text) {
  while (*text)
    uart_putc_raw(uart0, *text++);
}

static void write_line(const char *text) {
  write_text(text);
  write_text("\r\n");
}

static void console_adc(void) {
  char output[16];
  snprintf(output, sizeof output, "ADC %u", encoder_average());
  write_line(output);
}

static void handle_jog(const char *argument) {
  char *end;
  char output[80];
  long delta;
  uint16_t from_adc = encoder_average();

  if (!argument || !*argument) {
    write_line("?");
    return;
  }
  delta = strtol(argument, &end, 10);
  if (end == argument || *end || delta < -JOG_MAX_COUNTS ||
      delta > JOG_MAX_COUNTS ||
      (delta > -JOG_MIN_COUNTS && delta < JOG_MIN_COUNTS)) {
    write_line("?");
    return;
  }

  switch (controller_request_jog((int16_t)delta, &from_adc)) {
  case JOG_OK:
    snprintf(output, sizeof output, "JOG %+ld from %u", delta, from_adc);
    write_line(output);
    break;
  case JOG_BUSY:
    write_line("BUSY");
    break;
  case JOG_ENDSTOP:
    snprintf(output, sizeof output,
             "LIMIT         %u %+ld would leave the safe range %u..%u",
             from_adc, delta, ADC_SAFE_MIN, ADC_SAFE_MAX);
    write_line(output);
    break;
  case JOG_FAULT:
  case JOG_OVERTRAVEL:
    write_line("FAULT         clear with a reset");
    break;
  default:
    write_line("?");
    break;
  }
}

static void console_handle_line(char *line) {
  char *argument = strchr(line, ' ');

  if (argument) {
    *argument++ = '\0';
    while (*argument == ' ')
      ++argument;
  }
  if (!strcmp(line, "adc") && !argument)
    console_adc();
  else if (!strcmp(line, "jog") && argument)
    handle_jog(argument);
  else
    write_line("?");
}

static void submit_line(void) {
  write_text("\r\n");
  if (discard_line) {
    write_line("?");
  } else if (line_length) {
    line_buffer[line_length] = '\0';
    console_handle_line(line_buffer);
  }
  line_length = 0u;
  discard_line = false;
}

void console_init(void) {
  char output[24];

  uart_init(uart0, UART_BAUD);
  gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
  gpio_pull_up(PIN_UART_RX);
  uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
  uart_set_hw_flow(uart0, false, false);
  uart_set_fifo_enabled(uart0, true);
  uart_get_hw(uart0)->rsr = 0u;
  line_length = 0u;
  discard_line = false;
  swallow_lf = false;
  event_head = 0u;
  event_tail = 0u;
  snprintf(output, sizeof output, "luftfugl %s", FW_VERSION);
  write_line(output);
  console_adc();
}

void console_poll(void) {
  if (uart_get_hw(uart0)->rsr)
    uart_get_hw(uart0)->rsr = 0u;

  while (uart_is_readable(uart0)) {
    char c = (char)uart_getc(uart0);

    if (c == '\n' && swallow_lf) {
      swallow_lf = false;
      continue;
    }
    swallow_lf = false;
    if (c == '\r' || c == '\n') {
      submit_line();
      swallow_lf = c == '\r';
    } else if (c == '\b' || c == 0x7f) {
      if (!discard_line && line_length) {
        --line_length;
        write_text("\b \b");
      }
    } else if (isprint((unsigned char)c) && !discard_line) {
      if (line_length < CONSOLE_LINE_MAX) {
        line_buffer[line_length++] = c;
        uart_putc_raw(uart0, c);
      } else {
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
    char output[24];
    event_tail = (uint8_t)((event_tail + 1u) % EVENT_QUEUE_DEPTH);

    switch (event.kind) {
    case EV_JOG_COMPLETE:
      snprintf(output, sizeof output, "DONE %u", encoder_average());
      write_line(output);
      break;
    case EV_TIMEOUT:
      snprintf(output, sizeof output, "TIMEOUT %u", encoder_average());
      write_line(output);
      break;
    case EV_FAULT_OVERTRAVEL:
      snprintf(output, sizeof output, "OVERTRAVEL %u", encoder_average());
      write_line(output);
      break;
    default:
      break;
    }
  }
}

void console_watchdog_reset(void) {}

void console_timer_alloc_failed(void) { write_line("FAULT"); }
