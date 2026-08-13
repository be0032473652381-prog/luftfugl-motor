#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

int main(void)
{
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    uart_set_fifo_enabled(uart0, true);
    const char *m = "\r\nUART ECHO TEST\r\n";
    while (*m) uart_putc_raw(uart0, *m++);
    uint32_t n = 0;
    for (;;) {
        if (uart_is_readable(uart0)) {
            char c = uart_getc(uart0);
            n++;
            uart_putc_raw(uart0, c);
            if (c == '\r') uart_putc_raw(uart0, '\n');
        }
    }
}
