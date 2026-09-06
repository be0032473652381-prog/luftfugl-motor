#include "buzzer_threat_pattern.h"
#include "buzzer_dds_wave.h"
#include <limits.h>

_Static_assert(BUZZER_THREAT_INTRO_COUNT == 2u, "Two introduction notes");
_Static_assert(BUZZER_THREAT_PART1_COUNT > 0u && BUZZER_THREAT_PART2_COUNT > 0u &&
               BUZZER_THREAT_PART3_COUNT > 0u, "All phrases must contain notes");

/* Independent from build_bird(): listening tests do not advance its RNG. */
static uint32_t threat_rng = 0x4B1DCA11u;

static uint16_t threat_random(uint16_t low, uint16_t high) {
  threat_rng ^= threat_rng << 13;
  threat_rng ^= threat_rng >> 17;
  threat_rng ^= threat_rng << 5;
  return (uint16_t)(low + threat_rng % (high - low + 1u));
}

int32_t threat_wave_sample(uint32_t phase) {
  /* Clean, full-scale sine for resonance listening; no added harmonics. */
  return buzzer_dds_sine[phase >> 24];
}

static uint16_t threat_burst_pitch(uint16_t base, uint16_t low, uint16_t high) {
  uint16_t frequency = threat_random(base - BUZZER_THREAT_PITCH_JUMP_HZ,
                                    base + BUZZER_THREAT_PITCH_JUMP_HZ);
  /* Preserve independent +/-25 Hz draws, but constrain each played half
   * to this part's specified band, including the deliberately lower parts. */
  if (frequency < low)
    frequency = low;
  if (frequency > high)
    frequency = high;
  return frequency;
}

static void threat_event(volatile threat_synth_t *synth, unsigned int *index,
                         uint16_t frequency, uint16_t ms, uint32_t sys_hz) {
  volatile threat_event_t *event = &synth->events[(*index)++];
  synth->duration_ms += ms;
  event->frequency_hz = frequency;
  event->duration_ms = ms;
  event->phase_step = (uint32_t)(((uint64_t)frequency << 32) *
                                synth->period / sys_hz);
  event->samples = (uint32_t)((uint64_t)ms * sys_hz /
                              ((uint64_t)synth->period * 1000u));
}

static void threat_note(volatile threat_synth_t *synth, unsigned int *index,
                        uint16_t low, uint16_t high, uint16_t ms, uint32_t sys_hz) {
  uint16_t base = threat_random(low, high);
  threat_event(synth, index, threat_burst_pitch(base, low, high), ms / 2u, sys_hz);
  threat_event(synth, index, threat_burst_pitch(base, low, high), ms - ms / 2u, sys_hz);
}

bool threat_pattern_init(volatile threat_synth_t *synth, unsigned int count,
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
      threat_note(synth, &index, BUZZER_THREAT_HIGH_MIN_HZ,
                  BUZZER_THREAT_HIGH_MAX_HZ, BUZZER_THREAT_INTRO_MS, sys_hz);
      threat_event(synth, &index, 0u, n + 1u == BUZZER_THREAT_INTRO_COUNT ?
          BUZZER_THREAT_TRANSITION_MS : BUZZER_THREAT_INTRO_GAP_MS, sys_hz);
    }
    uint16_t low = part == 1u ? BUZZER_THREAT_LOW_MIN_HZ : BUZZER_THREAT_HIGH_MIN_HZ;
    uint16_t high = part == 1u ? BUZZER_THREAT_LOW_MAX_HZ : BUZZER_THREAT_HIGH_MAX_HZ;
    for (unsigned int n = 0u; n < counts[part]; ++n) {
      uint16_t ms = threat_random(part == 2u ? BUZZER_THREAT_PEAK_NOTE_MIN_MS :
                                    BUZZER_THREAT_NOTE_MIN_MS,
                                  part == 2u ? BUZZER_THREAT_PEAK_NOTE_MAX_MS :
                                    BUZZER_THREAT_NOTE_MAX_MS);
      threat_note(synth, &index, low, high, ms, sys_hz);
      if (n + 1u < counts[part]) {
        uint16_t gap = threat_random(part == 2u ? BUZZER_THREAT_PEAK_GAP_MIN_MS :
            part == 1u ? BUZZER_THREAT_GAP2_MIN_MS : BUZZER_THREAT_GAP1_MIN_MS,
            part == 2u ? BUZZER_THREAT_PEAK_GAP_MAX_MS :
            part == 1u ? BUZZER_THREAT_GAP2_MAX_MS : BUZZER_THREAT_GAP1_MAX_MS);
        threat_event(synth, &index, 0u, gap, sys_hz);
      }
    }
    uint32_t next_onset = (part + 1u) * BUZZER_THREAT_PHRASE_ONSET_MS;
    /* No software 650 ms truncation: validate each entire phrase fits. */
    if (synth->duration_ms >= next_onset)
      return false;
    if (part < 2u)
      threat_event(synth, &index, 0u, (uint16_t)(next_onset - synth->duration_ms), sys_hz);
  }
  if (index != THREAT_EVENT_COUNT)
    return false;
  synth->index = 0u;
  synth->phase = 0u;
  synth->plays_left = count;
  synth->remaining = synth->events[0].samples;
  /* Across repeats, preserve the same two-second phrase onset rhythm. */
  synth->gap_samples = (uint32_t)((uint64_t)(3u * BUZZER_THREAT_PHRASE_ONSET_MS -
      synth->duration_ms) * sys_hz / ((uint64_t)period * 1000u));
  synth->gap_remaining = 0u;
  return true;
}

bool threat_synth_done(const volatile threat_synth_t *synth) {
  return synth->index == THREAT_EVENT_COUNT && synth->plays_left <= 1u;
}

uint32_t threat_next_sample(volatile threat_synth_t *synth) {
  uint32_t a = synth->period;
  uint32_t b = synth->period;
  uint32_t silent = a | (b << 16);
  if (synth->gap_remaining) {
    --synth->gap_remaining;
    return silent;
  }
  if (synth->index == THREAT_EVENT_COUNT) {
    if (synth->plays_left <= 1u)
      return silent;
    --synth->plays_left;
    synth->index = 0u;
    synth->phase = 0u; /* Repeat the exact waveform, not just the pitch list. */
    synth->remaining = synth->events[0].samples;
    synth->gap_remaining = synth->gap_samples - 1u;
    return silent;
  }
  const volatile threat_event_t *event = &synth->events[synth->index];
  if (event->frequency_hz) {
    int32_t sample = threat_wave_sample(synth->phase);
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
    if (synth->index < THREAT_EVENT_COUNT)
      synth->remaining = synth->events[synth->index].samples;
  }
  return a | (b << 16);
}
