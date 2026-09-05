#ifndef LUFTFUGL_EVENT_TIMER_H
#define LUFTFUGL_EVENT_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void event_timer_init(void);
void event_timer_poll(void);
bool event_timer_start(char *detail, size_t size);
bool event_timer_stop(char *detail, size_t size);
bool event_timer_set_interval(uint32_t seconds, bool persist, char *detail,
                              size_t size);
bool event_timer_format_countdown(char *detail, size_t size);
bool event_timer_running(void);
bool event_timer_alert_active(void);

#endif
