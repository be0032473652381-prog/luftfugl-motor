#include "buzzer_threat_pattern.h"
#include "buzzer_compact_pattern.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static uint32_t reference[400000u];

int main(void) {
  threat_synth_t original;
  compact_synth_t compact;
  assert(!compact_pattern_init(&compact, 0u, 125000000u));
  assert(!compact_pattern_init(&compact, 201u, 125000000u));
  assert(!compact_pattern_init(&compact, UINT_MAX, 125000000u));
  assert(!compact_pattern_init(&compact, 1u, 0u));
  assert(!compact_pattern_init(&compact, 1u, 60000u));
  unsigned int smallest = UINT_MAX, largest = 0u;
  for (unsigned int trial = 0u; trial < 1000u; ++trial) {
    assert(threat_pattern_init(&original, 1u, 125000000u));
    assert(compact_pattern_init(&compact, 1u, 125000000u));
    assert(COMPACT_EVENT_COUNT == THREAT_EVENT_COUNT);
    unsigned int sound = 0u, ms = 0u;
    for (unsigned int i = 0u; i < COMPACT_EVENT_COUNT; ++i) {
      threat_event_t a = original.events[i];
      compact_event_t b = compact.events[i];
      assert(a.frequency_hz == b.frequency_hz && a.phase_step == b.phase_step);
      if (a.frequency_hz) {
        assert(a.duration_ms == b.duration_ms && a.samples == b.samples);
        sound += b.duration_ms;
      } else {
        unsigned int expected = (a.duration_ms + 1u) / 2u;
        if (i == 23u || i == 50u) expected = (expected + 1u) / 2u;
        assert(b.duration_ms == expected);
        assert(b.samples == b.duration_ms * 100u);
      }
      ms += b.duration_ms;
      /* Compare the actual sounding PWM waveform, including phases across
       * half-notes and silences. This detects changes beyond mere metadata. */
      if (trial < 10u) {
        if (a.frequency_hz)
          for (unsigned int n = 0u; n < a.samples; ++n)
            assert(threat_next_sample(&original) == compact_next_sample(&compact));
        else {
          uint32_t silent = original.period | ((uint32_t)original.period << 16);
          for (unsigned int n = 0u; n < a.samples; ++n)
            assert(threat_next_sample(&original) == silent);
          for (unsigned int n = 0u; n < b.samples; ++n)
            assert(compact_next_sample(&compact) == silent);
        }
      }
    }
    assert(ms == compact.duration_ms && ms < original.duration_ms);
    assert(compact.gap_samples == (((original.gap_samples / 100u + 1u) / 2u + 1u) / 2u) * 100u);
    assert(sound >= 1505u && sound <= 1715u);
    if (ms < smallest) smallest = ms;
    if (ms > largest) largest = ms;
    if (!trial) printf("First bout: play-4 %u ms, play-5 %u ms, sound unchanged %u ms, repeat silence %u ms\n",
                       original.duration_ms, ms, sound, compact.gap_samples / 100u);
  }
  const unsigned int counts[] = {1u, 2u, 200u};
  for (unsigned int c = 0u; c < sizeof counts / sizeof counts[0]; ++c) {
    assert(compact_pattern_init(&compact, counts[c], 125000000u));
    assert(compact.duration_ms * 100u <= sizeof reference / sizeof reference[0]);
    uint32_t silent = compact.period | ((uint32_t)compact.period << 16);
    for (unsigned int repeat = 0u; repeat < counts[c]; ++repeat) {
      if (repeat)
        for (unsigned int n = 0u; n < compact.gap_samples; ++n)
          assert(compact_next_sample(&compact) == silent);
      for (unsigned int n = 0u; n < compact.duration_ms * 100u; ++n) {
        uint32_t sample = compact_next_sample(&compact);
        if (!repeat) reference[n] = sample;
        else assert(reference[n] == sample);
      }
    }
    assert(compact_synth_done(&compact));
    for (unsigned int n = 0u; n < 512u; ++n)
      assert(compact_next_sample(&compact) == silent);
  }
  printf("PASS: 1000 matched compositions (%u..%u ms), all sounding events unchanged, actual sounding samples identical, short gaps unchanged, phrase/repeat pauses halved again with ms rounding, 1/2/200 exact repeats, terminal silence\n", smallest, largest);
}
