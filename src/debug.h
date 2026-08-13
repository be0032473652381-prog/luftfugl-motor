#ifndef LUFTFUGL_DEBUG_H
#define LUFTFUGL_DEBUG_H

#include "config.h"
#include <stdbool.h>
#include <stdint.h>

void dbg_init(void);
void dbg_enter(void);
void dbg_enter_plain(void);
void dbg_exit(void);
bool dbg_active(void);
bool dbg_plain_mode(void);
void dbg_handle_key(char c);
#ifdef LUFTFUGL_TRACE_INPUT
void dbg_trace_input_in(char c);
void dbg_trace_input_out(char c, const char *consumed_by, const char *line);
#endif
void dbg_poll(void);
void dbg_render(void);
void dbg_fields_refresh(void);
void dbg_log_push(const char *text);
void dbg_event(event_kind_t kind, uint8_t arg);
void dbg_out_push(const char *text);
void dbg_out_drain(void);
bool dbg_out_pending(void);
bool dbg_motor_armed(void);

#endif
