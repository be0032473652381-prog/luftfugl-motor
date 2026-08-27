#ifndef LUFTFUGL_BUZZER_H
#define LUFTFUGL_BUZZER_H

#include <stdbool.h>

void buzzer_init(void);
void buzzer_set(bool enabled);
void buzzer_play(unsigned int count);
void buzzer_tick(void);
bool buzzer_enabled(void);

#endif
