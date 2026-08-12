#ifndef LUFTFUGL_CONTROLLER_H
#define LUFTFUGL_CONTROLLER_H
#include "config.h"
void controller_init(void);
move_result_t controller_request(request_kind_t kind, position_t arg);
jog_result_t controller_request_jog(int16_t delta, uint16_t *from_adc);
move_result_t controller_request_setpos(position_t position, uint16_t adc);
move_result_t controller_request_reset_positions(void);
void controller_tick(void);
sys_state_t controller_state(void);
position_t controller_position(void);
position_t controller_target(void);
#endif
