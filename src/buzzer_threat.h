#ifndef LUFTFUGL_BUZZER_THREAT_H
#define LUFTFUGL_BUZZER_THREAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef LUFTFUGL_DEBUG
/* Listening test only; the debug console is the only playback caller. */
bool buzzer_play_4(unsigned int count);
/* Canonical chirp name; preserves the existing generator and callers. */
static inline bool buzzer_crips_4(unsigned int count) {
  return buzzer_play_4(count);
}
bool buzzer_play_4_active(void);
bool buzzer_play_4_underrun(void);
void buzzer_play_4_stop(void);
void buzzer_play_4_tick(void);
#endif

#endif
