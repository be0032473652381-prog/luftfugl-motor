#include "config.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/time.h"

#include <stdio.h>

enum { SAMPLE_PERIOD_MS = 100 };

int main(void) {
  char line[48];

  uart_init(uart1, UART_BAUD);
  gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
  uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
  uart_set_hw_flow(uart1, false, false);

  adc_init();
  adc_gpio_init(PIN_SENSE);
  adc_select_input(ADC_CHANNEL);

  uart_puts(uart1, "\r\nADC0 sandbox: GP26, raw 0..4095\r\n");
  for (;;) {
    uint16_t raw = adc_read();
    int length = snprintf(line, sizeof line, "ADC0 raw %u\r\n", raw);
    if (length > 0)
      uart_write_blocking(uart1, (const uint8_t *)line, (size_t)length);
    sleep_ms(SAMPLE_PERIOD_MS);
  }
}
