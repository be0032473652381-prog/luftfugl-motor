/*
 * Sandbox 2 - echo with periodic bulk output.
 *
 * Sandbox 1 proved RX works with no other activity. This adds the one thing
 * the debug console does that the production console does not: large periodic
 * writes through blocking uart_putc_raw().
 *
 * It reproduces the field-refresh load exactly:
 *   264 bytes every 200 ms  = 22.9 ms of blocked transmit per refresh
 *   the RX FIFO is 32 bytes = 2.78 ms before it overflows
 *
 * WHAT IT DOES
 *   Echoes every character received, and counts them.
 *   Every 200 ms, writes a 264-byte block, blocking, exactly as the renderer
 *   does. Press 'b' to toggle that block on and off.
 *   Every 2 s, reports rx received, echoed, and hardware overrun count.
 *
 * HOW TO READ IT
 *   With bulk OFF, type steadily. Note rx and overruns.
 *   Press 'b' to turn bulk ON. Type the same way.
 *
 *   overruns rise and characters vanish -> blocking starves RX. The fix is an
 *                                          RX interrupt plus non-blocking TX.
 *   no change, everything still echoes  -> blocking is NOT the cause, and the
 *                                          fault is in the console dispatch.
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#define BULK_BYTES   264u    /* one field refresh */
#define BULK_MS      200u    /* 5 Hz */
#define REPORT_MS   2000u

static uint32_t rx_count, echo_count, overruns, bulk_blocks;
static uint32_t worst_block_us;

static void put_str(const char *s)
{
    while (*s) uart_putc_raw(uart0, *s++);
}

/* Read and clear the PL011 overrun flag. */
static void check_overrun(void)
{
    if (uart_get_hw(uart0)->rsr & 0x8u) {   /* OE */
        overruns++;
        uart_get_hw(uart0)->rsr = 0xfu;     /* write clears */
    }
}

int main(void)
{
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart0, false, false);
    uart_set_fifo_enabled(uart0, true);

    put_str("\r\nSANDBOX 2 - echo under bulk output load\r\n");
    put_str("bulk output is OFF. press 'b' to toggle. type to test.\r\n\r\n");

    bool bulk_on = false;
    absolute_time_t next_bulk   = make_timeout_time_ms(BULK_MS);
    absolute_time_t next_report = make_timeout_time_ms(REPORT_MS);

    char filler[BULK_BYTES + 1];
    memset(filler, '.', BULK_BYTES);
    filler[BULK_BYTES] = '\0';

    for (;;) {
        /* --- RX, polled from the main loop exactly as console_poll does --- */
        while (uart_is_readable(uart0)) {
            char c = uart_getc(uart0);
            rx_count++;

            if (c == 'b') {
                bulk_on = !bulk_on;
                put_str(bulk_on ? "\r\n[BULK ON]\r\n" : "\r\n[BULK OFF]\r\n");
                continue;
            }
            uart_putc_raw(uart0, c);
            if (c == '\r') uart_putc_raw(uart0, '\n');
            echo_count++;
        }
        check_overrun();

        /* --- the blocking bulk write, mimicking the frame renderer --- */
        if (bulk_on && absolute_time_diff_us(get_absolute_time(), next_bulk) <= 0) {
            uint32_t t0 = time_us_32();
            put_str("\r\n");
            put_str(filler);          /* 264 bytes, blocking */
            uint32_t dt = time_us_32() - t0;
            if (dt > worst_block_us) worst_block_us = dt;
            bulk_blocks++;
            next_bulk = make_timeout_time_ms(BULK_MS);
        }

        /* --- periodic report --- */
        if (absolute_time_diff_us(get_absolute_time(), next_report) <= 0) {
            char b[128];
            int n = snprintf(b, sizeof b,
                "\r\n[bulk %s  rx %lu  echoed %lu  overruns %lu  blocks %lu  worst block %lu us]\r\n",
                bulk_on ? "ON " : "OFF",
                (unsigned long)rx_count, (unsigned long)echo_count,
                (unsigned long)overruns, (unsigned long)bulk_blocks,
                (unsigned long)worst_block_us);
            for (int i = 0; i < n; i++) uart_putc_raw(uart0, b[i]);
            next_report = make_timeout_time_ms(REPORT_MS);
        }
    }
}
