#include "encoder.h"
void encoder_init(void) {}
void encoder_tick(void) {}
uint16_t encoder_raw(void) { return 0; }
uint16_t encoder_average(void) { return 0; }
position_t encoder_instant(void) { return POS_UNKNOWN; }
position_t encoder_confirmed(void) { return POS_UNKNOWN; }
bool encoder_take_change(position_t *out) { (void)out; return false; }
