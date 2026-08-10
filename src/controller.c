#include "controller.h"
void controller_init(void) {}
move_result_t controller_request(request_kind_t kind, position_t arg) { (void)kind; (void)arg; return MOVE_OK; }
void controller_tick(void) {}
sys_state_t controller_state(void) { return ST_BOOT; }
position_t controller_position(void) { return POS_UNKNOWN; }
position_t controller_target(void) { return POS_UNKNOWN; }
