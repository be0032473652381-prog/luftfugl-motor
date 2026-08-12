#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <stdio.h>

int main(void)
{
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_init(1);
    gpio_set_dir(1, GPIO_IN);
    gpio_pull_up(1);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart0, false, false);
    uart_set_fifo_enabled(uart0, true);

    static const char banner[] = "gp1 pad transition test ready\r\n";
    for (size_t i = 0; i < sizeof banner - 1u; ++i)
        uart_putc_raw(uart0, banner[i]);

    bool previous = gpio_get(1);
    uint32_t transitions = 0;
    absolute_time_t report_at = make_timeout_time_ms(1000);
    for (;;) {
        bool level = gpio_get(1);
        if (level != previous) {
            previous = level;
            ++transitions;
        }
        if (time_reached(report_at)) {
            char b[64];
            int len = snprintf(b, sizeof b, "gp1 level=%u transitions=%lu\r\n",
                               level ? 1u : 0u, (unsigned long)transitions);
            for (int i = 0; i < len; i++)
                uart_putc_raw(uart0, b[i]);
            report_at = delayed_by_ms(report_at, 1000);
        }
    }
}
