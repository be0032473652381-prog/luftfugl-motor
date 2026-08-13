#ifndef LUFTFUGL_CONSOLE_H
#define LUFTFUGL_CONSOLE_H
#include <stddef.h>
#include <stdint.h>
#include "config.h"
void console_init(void);
void console_poll(void);
void console_push_event(event_kind_t kind, uint8_t arg);
void console_drain_events(void);
void console_watchdog_reset(void);
void console_timer_alloc_failed(void);
#ifdef LUFTFUGL_MONITOR
void console_debug_write(const char *text);
void console_debug_line(const char *text);
bool console_event_queue_full(void);
void console_diag_format(char *output, size_t output_size);
void console_diag_note_tx_spin(uint32_t elapsed_us);
void console_diag_note_debug_tx(void);
#endif
#endif
