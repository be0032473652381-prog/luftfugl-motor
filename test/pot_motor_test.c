#include "config.h"
#include "motor.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/time.h"

#include <stdbool.h>
#include <stdio.h>

enum {
  POT_MAX = 4000,
  POT_CENTER = POT_MAX / 2,
  SAMPLE_PERIOD_MS = 100
};

static uint8_t scaled_duty(uint16_t distance) {
  uint16_t active = 0u;
  if (distance > (uint16_t)POS_WINDOW)
    active = (uint16_t)(distance - (uint16_t)POS_WINDOW);
  uint16_t span = POT_CENTER - POS_WINDOW;
  if (active > span)
    active = span;
  return (uint8_t)(DUTY_MIN +
                   ((uint32_t)active * (DUTY_NORMAL - DUTY_MIN)) / span);
}

int main(void) {
  char line[80];
  bool armed = false;

  uart_init(uart1, UART_BAUD);
  gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
  uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
  uart_set_hw_flow(uart1, false, false);

  adc_init();
  adc_gpio_init(PIN_SENSE);
  adc_select_input(ADC_CHANNEL);

  motor_init();
  motor_enable();
  motor_brake();

  uart_puts(uart1,
            "\r\nPot motor sandbox: center pot to arm; center always brakes\r\n");
  for (;;) {
    uint16_t raw = adc_read();
    uint16_t limited = raw > POT_MAX ? POT_MAX : raw;
    int32_t offset = (int32_t)limited - POT_CENTER;
    uint16_t distance = (uint16_t)(offset < 0 ? -offset : offset);
    const char *state = "BRAKE";
    uint8_t duty = 0u;

    if (!armed) {
      motor_brake();
      state = "WAIT_CENTER";
      if (distance <= POS_WINDOW) {
        armed = true;
        state = "ARMED_BRAKE";
      }
    } else if (distance <= POS_WINDOW) {
      motor_brake();
    } else {
      duty = scaled_duty(distance);
      if (offset > 0) {
        motor_drive(DIR_FWD, duty);
        state = "FWD";
      } else {
        motor_drive(DIR_REV, duty);
        state = "REV";
      }
    }

    int length = snprintf(line, sizeof line, "ADC %u  %-11s duty %u\r\n",
                          raw, state, duty);
    if (length > 0)
      uart_write_blocking(uart1, (const uint8_t *)line, (size_t)length);
    sleep_ms(SAMPLE_PERIOD_MS);
  }
}
