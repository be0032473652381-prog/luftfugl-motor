#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <stdio.h>

int main(void)
{
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    uint32_t n = 0;
    for (;;) {
        char b[32];
        int len = snprintf(b, sizeof b, "alive %lu\r\n", (unsigned long)n++);
        for (int i = 0; i < len; i++) uart_putc_raw(uart0, b[i]);
        sleep_ms(1000);
    }
}
