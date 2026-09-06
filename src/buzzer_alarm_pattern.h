#ifndef LUFTFUGL_BUZZER_ALARM_PATTERN_H
#define LUFTFUGL_BUZZER_ALARM_PATTERN_H

#include "config.h"

#ifdef LUFTFUGL_DEBUG
#define ALARM_EVENT_COUNT (3u * (BUZZER_ALARM_PART1_COUNT + \
                           BUZZER_ALARM_PART2_COUNT + BUZZER_ALARM_PART3_COUNT) + 2u)
#define ALARM_CALL_MS ((BUZZER_ALARM_PART1_COUNT + BUZZER_ALARM_PART2_COUNT) * \
                       (BUZZER_ALARM_BURST_MS + BUZZER_ALARM_NOTE_GAP_MS) + \
                       BUZZER_ALARM_PART3_COUNT * \
                       (BUZZER_ALARM_CLIMAX_BURST_MS + BUZZER_ALARM_CLIMAX_GAP_MS) + \
                       2u * BUZZER_ALARM_BREAK_MS)

typedef struct {
  uint16_t frequency_hz;
  uint16_t duration_ms;
  uint32_t phase_step;
  uint32_t samples;
} alarm_event_t;

typedef struct {
  alarm_event_t events[ALARM_EVENT_COUNT];
  uint16_t period;
  unsigned int index;
  unsigned int plays_left;
  uint32_t phase;
  uint32_t remaining;
  uint32_t gap_samples;
  uint32_t gap_remaining;
} alarm_synth_t;

bool alarm_pattern_init(volatile alarm_synth_t *synth, unsigned int count,
                        uint32_t sys_hz);
bool alarm_tone_init(volatile alarm_synth_t *synth, uint32_t frequency_hz,
                     uint32_t duration_ms, uint32_t sys_hz);
uint32_t alarm_next_sample(volatile alarm_synth_t *synth);
bool alarm_synth_done(const volatile alarm_synth_t *synth);
int32_t alarm_wave_sample(uint32_t phase);
#endif

#endif
