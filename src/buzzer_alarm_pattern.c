#include "buzzer_alarm_pattern.h"
#include "buzzer_dds_wave.h"
#include <limits.h>

_Static_assert(ALARM_CALL_MS <= BUZZER_ALARM_CALL_MAX_MS,
               "All three parts must fit without truncation");
_Static_assert(BUZZER_ALARM_PART1_COUNT > 0 && BUZZER_ALARM_PART2_COUNT > 0 &&
               BUZZER_ALARM_PART3_COUNT > 0, "All three parts are required");
_Static_assert(BUZZER_ALARM_BURST_MS >= 2u && BUZZER_ALARM_CLIMAX_BURST_MS >= 2u,
               "Both burst halves need duration");
_Static_assert(BUZZER_ALARM_REPEAT_GAP_MS > 0u, "Repeats require a gap");

/* Independent from build_bird(): listening tests do not advance its RNG. */
static uint32_t alarm_rng = 0xA5A5A5A5u;

static uint16_t alarm_random(uint16_t low, uint16_t high) {
  alarm_rng ^= alarm_rng << 13;
  alarm_rng ^= alarm_rng >> 17;
  alarm_rng ^= alarm_rng << 5;
  return (uint16_t)(low + alarm_rng % (high - low + 1u));
}

int32_t alarm_wave_sample(uint32_t phase) {
  /* Clean, full-scale sine for resonance listening; no added harmonics. */
  return buzzer_dds_sine[phase >> 24];
}

static uint16_t alarm_burst_pitch(uint16_t base, uint16_t low, uint16_t high) {
  uint16_t frequency = alarm_random(base - BUZZER_ALARM_PITCH_JUMP_HZ,
                                    base + BUZZER_ALARM_PITCH_JUMP_HZ);
  /* Preserve independent +/-25 Hz draws, but constrain each played half
   * to this part's specified band, including the deliberately lower parts. */
  if (frequency < low)
    frequency = low;
  if (frequency > high)
    frequency = high;
  return frequency;
}

static void alarm_event(volatile alarm_synth_t *synth, unsigned int *index,
                         uint16_t frequency, uint16_t ms, uint32_t sys_hz) {
  volatile alarm_event_t *event = &synth->events[(*index)++];
  event->frequency_hz = frequency;
  event->duration_ms = ms;
  event->phase_step = (uint32_t)(((uint64_t)frequency << 32) *
                                synth->period / sys_hz);
  event->samples = (uint32_t)((uint64_t)ms * sys_hz /
                              ((uint64_t)synth->period * 1000u));
}

static void alarm_cluster(volatile alarm_synth_t *synth, unsigned int *index,
                          unsigned int count, uint16_t low, uint16_t high,
                          uint16_t sound_ms, uint16_t gap_ms, uint32_t sys_hz) {
  for (unsigned int i = 0u; i < count; ++i) {
    uint16_t base = alarm_random(low, high);
    alarm_event(synth, index, alarm_burst_pitch(base, low, high),
                 sound_ms / 2u, sys_hz);
    alarm_event(synth, index, alarm_burst_pitch(base, low, high),
                 sound_ms - sound_ms / 2u, sys_hz);
    alarm_event(synth, index, 0u, gap_ms, sys_hz);
  }
}

bool alarm_pattern_init(volatile alarm_synth_t *synth, unsigned int count,
                        uint32_t sys_hz) {
  if (count < 1u || count > BUZZER_PLAY_MAX || !sys_hz)
    return false;
  uint32_t period = (sys_hz + BUZZER_ALARM_CARRIER_HZ - 1u) /
                    BUZZER_ALARM_CARRIER_HZ;
  if (period < 2u || period > UINT16_MAX)
    return false;
  /* Check the clean sine fundamental against actual sample-rate Nyquist. */
  uint32_t max_hz = BUZZER_ALARM_PART1_MAX_HZ;
  if (BUZZER_ALARM_PART2_MAX_HZ > max_hz)
    max_hz = BUZZER_ALARM_PART2_MAX_HZ;
  if (BUZZER_ALARM_PART3_MAX_HZ > max_hz)
    max_hz = BUZZER_ALARM_PART3_MAX_HZ;
  if ((uint64_t)max_hz * 2u * period >= sys_hz)
    return false;
  synth->period = (uint16_t)period;
  unsigned int index = 0u;
  alarm_cluster(synth, &index, BUZZER_ALARM_PART1_COUNT,
                  BUZZER_ALARM_PART1_MIN_HZ, BUZZER_ALARM_PART1_MAX_HZ,
                  BUZZER_ALARM_BURST_MS, BUZZER_ALARM_NOTE_GAP_MS, sys_hz);
  alarm_event(synth, &index, 0u, BUZZER_ALARM_BREAK_MS, sys_hz);
  alarm_cluster(synth, &index, BUZZER_ALARM_PART2_COUNT,
                  BUZZER_ALARM_PART2_MIN_HZ, BUZZER_ALARM_PART2_MAX_HZ,
                  BUZZER_ALARM_BURST_MS, BUZZER_ALARM_NOTE_GAP_MS, sys_hz);
  alarm_event(synth, &index, 0u, BUZZER_ALARM_BREAK_MS, sys_hz);
  alarm_cluster(synth, &index, BUZZER_ALARM_PART3_COUNT,
                  BUZZER_ALARM_PART3_MIN_HZ, BUZZER_ALARM_PART3_MAX_HZ,
                  BUZZER_ALARM_CLIMAX_BURST_MS, BUZZER_ALARM_CLIMAX_GAP_MS, sys_hz);
  synth->index = 0u;
  synth->phase = 0u;
  synth->plays_left = count;
  synth->remaining = synth->events[0].samples;
  synth->gap_samples = (uint32_t)((uint64_t)BUZZER_ALARM_REPEAT_GAP_MS *
                                  sys_hz / ((uint64_t)period * 1000u));
  synth->gap_remaining = 0u;
  return true;
}

bool alarm_tone_init(volatile alarm_synth_t *synth, uint32_t frequency_hz,
                     uint32_t duration_ms, uint32_t sys_hz) {
  if (frequency_hz < BATTERY_ALERT_FREQUENCY_MIN_HZ ||
      frequency_hz > BATTERY_ALERT_FREQUENCY_MAX_HZ ||
      duration_ms < BATTERY_CHIRP_DURATION_MIN_S * 1000u ||
      duration_ms > BATTERY_CHIRP_DURATION_MAX_S * 1000u || !sys_hz)
    return false;
  uint32_t period = (sys_hz + BUZZER_ALARM_CARRIER_HZ - 1u) /
                    BUZZER_ALARM_CARRIER_HZ;
  if (period < 2u || period > UINT16_MAX)
    return false;
  /* Check the clean sine fundamental against actual sample-rate Nyquist. */
  if ((uint64_t)frequency_hz * 2u * period >= sys_hz)
    return false;
  synth->period = (uint16_t)period;
  unsigned int index = 0u;
  /* Keep the existing fixed-size event renderer: all segments have the
   * same frequency, retain phase, and contain no silent samples or breaks. */
  for (unsigned int i = 0u; i < ALARM_EVENT_COUNT; ++i) {
    uint16_t ms = (uint16_t)(duration_ms / ALARM_EVENT_COUNT +
                             (i < duration_ms % ALARM_EVENT_COUNT));
    alarm_event(synth, &index, (uint16_t)frequency_hz, ms, sys_hz);
  }
  synth->index = 0u;
  synth->phase = 0u;
  synth->plays_left = 1u;
  synth->remaining = synth->events[0].samples;
  synth->gap_samples = (uint32_t)((uint64_t)BUZZER_ALARM_REPEAT_GAP_MS *
                                  sys_hz / ((uint64_t)period * 1000u));
  synth->gap_remaining = 0u;
  return true;
}

bool alarm_synth_done(const volatile alarm_synth_t *synth) {
  return synth->index == ALARM_EVENT_COUNT && synth->plays_left <= 1u;
}

uint32_t alarm_next_sample(volatile alarm_synth_t *synth) {
  uint32_t a = synth->period;
  uint32_t b = synth->period;
  uint32_t silent = a | (b << 16);
  if (synth->gap_remaining) {
    --synth->gap_remaining;
    return silent;
  }
  if (synth->index == ALARM_EVENT_COUNT) {
    if (synth->plays_left <= 1u)
      return silent;
    --synth->plays_left;
    synth->index = 0u;
    synth->phase = 0u; /* Repeat the exact waveform, not just the pitch list. */
    synth->remaining = synth->events[0].samples;
    synth->gap_remaining = synth->gap_samples - 1u;
    return silent;
  }
  const volatile alarm_event_t *event = &synth->events[synth->index];
  if (event->frequency_hz) {
    int32_t sample = alarm_wave_sample(synth->phase);
    uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
    if (magnitude > 32767u)
      magnitude = 32767u;
    uint32_t pulse = (magnitude * synth->period) >> 15;
    if (sample >= 0)
      b -= pulse;
    else
      a -= pulse;
    synth->phase += event->phase_step;
  }
  if (--synth->remaining == 0u) {
    ++synth->index;
    if (synth->index < ALARM_EVENT_COUNT)
      synth->remaining = synth->events[synth->index].samples;
  }
  return a | (b << 16);
}
