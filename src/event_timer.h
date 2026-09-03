#ifndef LUFTFUGL_EVENT_TIMER_H
#define LUFTFUGL_EVENT_TIMER_H

#include <stdbool.h>
#include <stddef.h>

void event_timer_init(void);
void event_timer_poll(void);
bool event_timer_stop(char *detail, size_t size);

#endif
