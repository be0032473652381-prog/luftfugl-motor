#ifndef LUFTFUGL_BUZZER_COMPACT_PATTERN_H
#define LUFTFUGL_BUZZER_COMPACT_PATTERN_H

#include "config.h"

#define COMPACT_EVENT_COUNT (3u * (BUZZER_THREAT_PART1_COUNT + \
                            BUZZER_THREAT_PART2_COUNT + BUZZER_THREAT_PART3_COUNT + \
                            3u * BUZZER_THREAT_INTRO_COUNT) - 1u)

typedef struct {
  uint16_t frequency_hz;
  uint16_t duration_ms;
  uint32_t phase_step;
  uint32_t samples;
} compact_event_t;

typedef struct {
  compact_event_t events[COMPACT_EVENT_COUNT];
  uint16_t period;
  uint32_t duration_ms;
  unsigned int index;
  unsigned int plays_left;
  uint32_t phase;
  uint32_t remaining;
  uint32_t gap_samples;
  uint32_t gap_remaining;
} compact_synth_t;

bool compact_pattern_init(volatile compact_synth_t *synth, unsigned int count,
                        uint32_t sys_hz);
uint32_t compact_next_sample(volatile compact_synth_t *synth);
bool compact_synth_done(const volatile compact_synth_t *synth);
int32_t compact_wave_sample(uint32_t phase);

#endif
