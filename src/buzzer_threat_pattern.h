#ifndef LUFTFUGL_BUZZER_THREAT_PATTERN_H
#define LUFTFUGL_BUZZER_THREAT_PATTERN_H

#include "config.h"

#ifdef LUFTFUGL_DEBUG
#define THREAT_EVENT_COUNT (3u * (BUZZER_THREAT_PART1_COUNT + \
                            BUZZER_THREAT_PART2_COUNT + BUZZER_THREAT_PART3_COUNT + \
                            3u * BUZZER_THREAT_INTRO_COUNT) - 1u)

typedef struct {
  uint16_t frequency_hz;
  uint16_t duration_ms;
  uint32_t phase_step;
  uint32_t samples;
} threat_event_t;

typedef struct {
  threat_event_t events[THREAT_EVENT_COUNT];
  uint16_t period;
  uint32_t duration_ms;
  unsigned int index;
  unsigned int plays_left;
  uint32_t phase;
  uint32_t remaining;
  uint32_t gap_samples;
  uint32_t gap_remaining;
} threat_synth_t;

bool threat_pattern_init(volatile threat_synth_t *synth, unsigned int count,
                        uint32_t sys_hz);
uint32_t threat_next_sample(volatile threat_synth_t *synth);
bool threat_synth_done(const volatile threat_synth_t *synth);
int32_t threat_wave_sample(uint32_t phase);
#endif

#endif
