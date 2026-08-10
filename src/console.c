#include "console.h"
void console_init(void) {}
void console_poll(void) {}
void console_push_event(event_kind_t kind, uint8_t arg) { (void)kind; (void)arg; }
void console_drain_events(void) {}
void console_watchdog_reset(void) {}
