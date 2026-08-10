#ifndef LUFTFUGL_CONSOLE_H
#define LUFTFUGL_CONSOLE_H
#include <stdint.h>
#include "config.h"
void console_init(void);
void console_poll(void);
void console_push_event(event_kind_t kind, uint8_t arg);
void console_drain_events(void);
void console_watchdog_reset(void);
#ifdef LUFTFUGL_DEBUG
void console_debug_write(const char *text);
void console_debug_line(const char *text);
#endif
#endif
