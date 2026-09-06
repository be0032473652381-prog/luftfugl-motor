#ifndef LUFTFUGL_BUZZER_ALARM_H
#define LUFTFUGL_BUZZER_ALARM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef LUFTFUGL_DEBUG
/* Listening test only; the debug console is the only playback caller. */
bool buzzer_play_3(unsigned int count);
/* Canonical chirp name; preserves the existing generator and callers. */
static inline bool buzzer_crips_3(unsigned int count) {
  return buzzer_play_3(count);
}
bool buzzer_tone_3(uint32_t frequency_hz, uint32_t duration_ms);
bool buzzer_play_3_active(void);
bool buzzer_play_3_underrun(void);
void buzzer_play_3_stop(void);
void buzzer_play_3_tick(void);
#endif

#endif
