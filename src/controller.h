#ifndef LUFTFUGL_CONTROLLER_H
#define LUFTFUGL_CONTROLLER_H
#include "config.h"
void controller_init(void);
move_result_t controller_request(request_kind_t kind, position_t arg);
void controller_tick(void);
sys_state_t controller_state(void);
position_t controller_position(void);
position_t controller_target(void);
#endif
