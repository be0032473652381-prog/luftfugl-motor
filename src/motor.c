#include "motor.h"
void motor_init(void) {}
void motor_enable(void) {}
void motor_disable(void) {}
void motor_drive(direction_t dir, uint8_t duty) { (void)dir; (void)duty; }
void motor_brake(void) {}
void motor_coast(void) {}
direction_t motor_direction(void) { return DIR_STOP; }
uint8_t motor_duty(void) { return 0; }
