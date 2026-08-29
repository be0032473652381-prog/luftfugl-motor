#ifndef LUFTFUGL_BUZZER_H
#define LUFTFUGL_BUZZER_H

#include <stdbool.h>
#include <stdint.h>

void buzzer_init(void);
void buzzer_set(bool enabled);
void buzzer_play(unsigned int count);
void buzzer_tone(uint32_t frequency_hz, uint32_t duration_ms);
void buzzer_tone_sequence(uint32_t frequency_hz, uint32_t duration_ms,
                          uint8_t repeat, uint32_t pause_ms);
void buzzer_tone_stop(void);
bool buzzer_tone_sequence_active(void);
void buzzer_tone_sequence_status(uint8_t *current, uint8_t *total,
                                 bool *paused, uint32_t *deadline_remaining_ms);
void buzzer_tick(void);
bool buzzer_enabled(void);

#endif
