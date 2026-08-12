#ifndef LUFTFUGL_MOTOR_H
#define LUFTFUGL_MOTOR_H
#include <stdint.h>
#include "config.h"
void motor_init(void);
void motor_enable(void);
void motor_disable(void);
void motor_drive(direction_t dir, uint8_t duty);
void motor_brake(void);
void motor_coast(void);
direction_t motor_direction(void);
uint8_t motor_duty(void);
#ifdef LUFTFUGL_DEBUG
void motor_set_inhibit(bool on);
bool motor_inhibited(void);
#endif
#endif
