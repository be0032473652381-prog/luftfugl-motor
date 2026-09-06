#include "buzzer_compact_pattern.h"
#include "buzzer_dds_wave.h"
#include <limits.h>

_Static_assert(BUZZER_COMPACT_LONG_PAUSE_PERCENT > 0u &&
               BUZZER_COMPACT_LONG_PAUSE_PERCENT <= 100u, "Long pauses must remain nonzero");
_Static_assert(BUZZER_COMPACT_SILENCE_PERCENT > 0u &&
               BUZZER_COMPACT_SILENCE_PERCENT < 100u, "Silence must shorten but remain nonzero");
_Static_assert(BUZZER_THREAT_INTRO_COUNT == 2u, "Two introduction notes");
_Static_assert(BUZZER_THREAT_PART1_COUNT > 0u && BUZZER_THREAT_PART2_COUNT > 0u &&
               BUZZER_THREAT_PART3_COUNT > 0u, "All phrases must contain notes");

/* Private copy of play-4 composition and RNG: matched invocations produce
 * identical sounding notes. Only post-generation silence durations change.
 * test_buzzer_compact.c compares both generators to guard against drift. */
static uint32_t compact_rng = 0x4B1DCA11u;

static uint16_t compact_random(uint16_t low, uint16_t high) {
  compact_rng ^= compact_rng << 13;
  compact_rng ^= compact_rng >> 17;
  compact_rng ^= compact_rng << 5;
  return (uint16_t)(low + compact_rng % (high - low + 1u));
}

int32_t compact_wave_sample(uint32_t phase) {
  /* Clean, full-scale sine for resonance listening; no added harmonics. */
  return buzzer_dds_sine[phase >> 24];
}

static uint16_t compact_burst_pitch(uint16_t base, uint16_t low, uint16_t high) {
  uint16_t frequency = compact_random(base - BUZZER_THREAT_PITCH_JUMP_HZ,
                                    base + BUZZER_THREAT_PITCH_JUMP_HZ);
  /* Preserve independent +/-25 Hz draws, but constrain each played half
   * to this part's specified band, including the deliberately lower parts. */
  if (frequency < low)
    frequency = low;
  if (frequency > high)
    frequency = high;
  return frequency;
}

static void compact_event(volatile compact_synth_t *synth, unsigned int *index,
                         uint16_t frequency, uint16_t ms, uint32_t sys_hz) {
  volatile compact_event_t *event = &synth->events[(*index)++];
  synth->duration_ms += ms;
  event->frequency_hz = frequency;
  event->duration_ms = ms;
  event->phase_step = (uint32_t)(((uint64_t)frequency << 32) *
                                synth->period / sys_hz);
  event->samples = (uint32_t)((uint64_t)ms * sys_hz /
                              ((uint64_t)synth->period * 1000u));
}

static void compact_note(volatile compact_synth_t *synth, unsigned int *index,
                        uint16_t low, uint16_t high, uint16_t ms, uint32_t sys_hz) {
  uint16_t base = compact_random(low, high);
  compact_event(synth, index, compact_burst_pitch(base, low, high), ms / 2u, sys_hz);
  compact_event(synth, index, compact_burst_pitch(base, low, high), ms - ms / 2u, sys_hz);
}

bool compact_pattern_init(volatile compact_synth_t *synth, unsigned int count,
                         uint32_t sys_hz) {
  if (count < 1u || count > BUZZER_PLAY_MAX || !sys_hz)
    return false;
  uint32_t period = (sys_hz + BUZZER_THREAT_CARRIER_HZ - 1u) /
                    BUZZER_THREAT_CARRIER_HZ;
  if (period < 2u || period > UINT16_MAX ||
      (uint64_t)BUZZER_THREAT_HIGH_MAX_HZ * 2u * period >= sys_hz)
    return false;
  synth->period = (uint16_t)period;
  synth->duration_ms = 0u;
  unsigned int index = 0u;
  const unsigned int counts[] = {BUZZER_THREAT_PART1_COUNT,
      BUZZER_THREAT_PART2_COUNT, BUZZER_THREAT_PART3_COUNT};
  for (unsigned int part = 0u; part < 3u; ++part) {
    /* Intro timing follows measured F-like note organization. The narrow
     * sine bands are an engineering substitution, not natural F/D spectra. */
    for (unsigned int n = 0u; n < BUZZER_THREAT_INTRO_COUNT; ++n) {
      compact_note(synth, &index, BUZZER_THREAT_HIGH_MIN_HZ,
                  BUZZER_THREAT_HIGH_MAX_HZ, BUZZER_THREAT_INTRO_MS, sys_hz);
      compact_event(synth, &index, 0u, n + 1u == BUZZER_THREAT_INTRO_COUNT ?
          BUZZER_THREAT_TRANSITION_MS : BUZZER_THREAT_INTRO_GAP_MS, sys_hz);
    }
    uint16_t low = part == 1u ? BUZZER_THREAT_LOW_MIN_HZ : BUZZER_THREAT_HIGH_MIN_HZ;
    uint16_t high = part == 1u ? BUZZER_THREAT_LOW_MAX_HZ : BUZZER_THREAT_HIGH_MAX_HZ;
    for (unsigned int n = 0u; n < counts[part]; ++n) {
      uint16_t ms = compact_random(part == 2u ? BUZZER_THREAT_PEAK_NOTE_MIN_MS :
                                    BUZZER_THREAT_NOTE_MIN_MS,
                                  part == 2u ? BUZZER_THREAT_PEAK_NOTE_MAX_MS :
                                    BUZZER_THREAT_NOTE_MAX_MS);
      compact_note(synth, &index, low, high, ms, sys_hz);
      if (n + 1u < counts[part]) {
        uint16_t gap = compact_random(part == 2u ? BUZZER_THREAT_PEAK_GAP_MIN_MS :
            part == 1u ? BUZZER_THREAT_GAP2_MIN_MS : BUZZER_THREAT_GAP1_MIN_MS,
            part == 2u ? BUZZER_THREAT_PEAK_GAP_MAX_MS :
            part == 1u ? BUZZER_THREAT_GAP2_MAX_MS : BUZZER_THREAT_GAP1_MAX_MS);
        compact_event(synth, &index, 0u, gap, sys_hz);
      }
    }
    uint32_t next_onset = (part + 1u) * BUZZER_THREAT_PHRASE_ONSET_MS;
    /* No software 650 ms truncation: validate each entire phrase fits. */
    if (synth->duration_ms >= next_onset)
      return false;
    if (part < 2u)
      compact_event(synth, &index, 0u, (uint16_t)(next_onset - synth->duration_ms), sys_hz);
  }
  if (index != COMPACT_EVENT_COUNT)
    return false;
  synth->index = 0u;
  synth->phase = 0u;
  synth->plays_left = count;
  synth->remaining = synth->events[0].samples;
  /* Compute play-4's original repeat pause; it is shortened below. */
  synth->gap_samples = (uint32_t)((uint64_t)(3u * BUZZER_THREAT_PHRASE_ONSET_MS -
      synth->duration_ms) * sys_hz / ((uint64_t)period * 1000u));
  synth->gap_remaining = 0u;
  /* Generate the complete original composition first, preserving every RNG
   * draw and note. Compress only zero-frequency events and the repeat pause. */
  uint32_t repeat_ms = 3u * BUZZER_THREAT_PHRASE_ONSET_MS - synth->duration_ms;
  synth->duration_ms = 0u;
  const unsigned int first_pause = 3u * (BUZZER_THREAT_INTRO_COUNT +
      BUZZER_THREAT_PART1_COUNT) - 1u;
  const unsigned int second_pause = first_pause + 3u *
      (BUZZER_THREAT_INTRO_COUNT + BUZZER_THREAT_PART2_COUNT);
  for (unsigned int i = 0u; i < COMPACT_EVENT_COUNT; ++i) {
    volatile compact_event_t *event = &synth->events[i];
    if (!event->frequency_hz) {
      event->duration_ms = (uint16_t)((event->duration_ms *
          BUZZER_COMPACT_SILENCE_PERCENT + 99u) / 100u);
      /* Only the boundaries between complete phrases get this extra cut. */
      if (i == first_pause || i == second_pause)
        event->duration_ms = (uint16_t)((event->duration_ms *
            BUZZER_COMPACT_LONG_PAUSE_PERCENT + 99u) / 100u);
      event->samples = (uint32_t)((uint64_t)event->duration_ms * sys_hz /
                                  ((uint64_t)period * 1000u));
    }
    synth->duration_ms += event->duration_ms;
  }
  repeat_ms = (repeat_ms * BUZZER_COMPACT_SILENCE_PERCENT + 99u) / 100u;
  repeat_ms = (repeat_ms * BUZZER_COMPACT_LONG_PAUSE_PERCENT + 99u) / 100u;
  synth->gap_samples = (uint32_t)((uint64_t)repeat_ms * sys_hz /
                                  ((uint64_t)period * 1000u));
  return true;
}

bool compact_synth_done(const volatile compact_synth_t *synth) {
  return synth->index == COMPACT_EVENT_COUNT && synth->plays_left <= 1u;
}

uint32_t compact_next_sample(volatile compact_synth_t *synth) {
  uint32_t a = synth->period;
  uint32_t b = synth->period;
  uint32_t silent = a | (b << 16);
  if (synth->gap_remaining) {
    --synth->gap_remaining;
    return silent;
  }
  if (synth->index == COMPACT_EVENT_COUNT) {
    if (synth->plays_left <= 1u)
      return silent;
    --synth->plays_left;
    synth->index = 0u;
    synth->phase = 0u; /* Repeat the exact waveform, not just the pitch list. */
    synth->remaining = synth->events[0].samples;
    synth->gap_remaining = synth->gap_samples - 1u;
    return silent;
  }
  const volatile compact_event_t *event = &synth->events[synth->index];
  if (event->frequency_hz) {
    int32_t sample = compact_wave_sample(synth->phase);
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
    if (synth->index < COMPACT_EVENT_COUNT)
      synth->remaining = synth->events[synth->index].samples;
  }
  return a | (b << 16);
}
