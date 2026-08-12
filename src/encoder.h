#ifndef LUFTFUGL_ENCODER_H
#define LUFTFUGL_ENCODER_H
#include "config.h"
#include <stdbool.h>
#include <stdint.h>
void encoder_init(void);
void encoder_tick(void);
uint16_t encoder_raw(void);
uint16_t encoder_average(void);
position_t encoder_instant(void);
position_t encoder_confirmed(void);
bool encoder_take_change(position_t *out);
bool encoder_in_safe_range(void);
int16_t encoder_error_to(position_t target);
uint16_t encoder_nominal(position_t position);
void encoder_set_nominal(position_t position, uint16_t adc);
void encoder_reset_nominals(void);
#endif
