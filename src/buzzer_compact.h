#ifndef LUFTFUGL_BUZZER_COMPACT_H
#define LUFTFUGL_BUZZER_COMPACT_H

#include <stdbool.h>
#include <stdint.h>

/* Crips-5 is used by station arrivals and the debug console. */
bool buzzer_play_5(unsigned int count);
/* Canonical chirp name; preserves the existing generator and callers. */
static inline bool buzzer_crips_5(unsigned int count) {
  return buzzer_play_5(count);
}
bool buzzer_play_5_active(void);
bool buzzer_play_5_underrun(void);
void buzzer_play_5_stop(void);
void buzzer_play_5_tick(void);

#endif
