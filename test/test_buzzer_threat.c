#include "buzzer_threat_pattern.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* Record actual PWM output, so count means exact complete-bout repetitions. */
static uint32_t reference[510000u];

int main(void) {
  threat_synth_t s;
  assert(!threat_pattern_init(&s, 0u, 125000000u));
  assert(!threat_pattern_init(&s, 201u, 125000000u));
  assert(!threat_pattern_init(&s, UINT_MAX, 125000000u));
  assert(!threat_pattern_init(&s, 1u, 0u));
  assert(!threat_pattern_init(&s, 1u, 60000u));
  unsigned int min_ms = UINT_MAX, max_ms = 0u;
  unsigned int min_hz[2] = {UINT_MAX, UINT_MAX}, max_hz[2] = {0u, 0u};
  for (unsigned int trial = 0u; trial < 1000u; ++trial) {
    assert(threat_pattern_init(&s, 1u, 125000000u));
    assert(s.period == 1250u && THREAT_EVENT_COUNT == 80u);
    unsigned int index = 0u, elapsed = 0u, sound = 0u;
    for (unsigned int part = 0u; part < 3u; ++part) {
      assert(elapsed == part * 2000u);
      unsigned int notes = 8u + part;
      for (unsigned int n = 0u; n < notes; ++n) {
        unsigned int band = n >= 2u && part == 1u ? 1u : 0u;
        unsigned int duration = 0u;
        for (unsigned int half = 0u; half < 2u; ++half) {
          threat_event_t e = s.events[index++];
          assert(e.frequency_hz >= (band ? 1950u : 4250u));
          assert(e.frequency_hz <= (band ? 2050u : 4350u));
          assert(e.samples == 100u * e.duration_ms);
          assert(e.samples > 0u);
          if (half) assert(abs((int)e.frequency_hz - s.events[index - 2u].frequency_hz) <= 50);
          if (e.frequency_hz < min_hz[band]) min_hz[band] = e.frequency_hz;
          if (e.frequency_hz > max_hz[band]) max_hz[band] = e.frequency_hz;
          duration += e.duration_ms;
        }
        assert(n < 2u ? duration == 100u : part == 2u ?
               duration >= 40u && duration <= 50u : duration >= 45u && duration <= 55u);
        sound += duration;
        elapsed += duration;
        if (n + 1u == notes && part == 2u) continue;
        threat_event_t gap = s.events[index++];
        assert(gap.frequency_hz == 0u && gap.phase_step == 0u);
        assert(gap.samples == gap.duration_ms * 100u);
        if (n == 0u) assert(gap.duration_ms == 100u);
        else if (n == 1u) assert(gap.duration_ms == 90u);
        else if (n + 1u == notes) assert(elapsed + gap.duration_ms == (part + 1u) * 2000u);
        else {
          unsigned int low = part == 2u ? 25u : part == 1u ? 35u : 30u;
          assert(gap.duration_ms >= low && gap.duration_ms <= low + 10u);
        }
        if (!trial && n + 1u == notes)
          printf("First generated bout: phrase %u ends %u ms; following silence %u ms\n",
                 part + 1u, elapsed, gap.duration_ms);
        elapsed += gap.duration_ms;
      }
    }
    assert(index == THREAT_EVENT_COUNT);
    assert(elapsed == s.duration_ms && elapsed >= 4885u && elapsed <= 5035u);
    assert(sound >= 1505u && sound <= 1715u);
    assert(elapsed * 100u + s.gap_samples == 600000u);
    if (!trial) printf("First generated bout: end %u ms, sounding %u ms, repeat silence %u ms\n",
                       elapsed, sound, s.gap_samples / 100u);
    if (elapsed < min_ms) min_ms = elapsed;
    if (elapsed > max_ms) max_ms = elapsed;
  }
  assert(min_hz[0] == 4250u && max_hz[0] == 4350u);
  assert(min_hz[1] == 1950u && max_hz[1] == 2050u);
  printf("1000 compositions: observed duration %u..%u ms; frequencies %u..%u / %u..%u Hz\n",
         min_ms, max_ms, min_hz[0], max_hz[0], min_hz[1], max_hz[1]);
  const unsigned int counts[] = {1u, 2u, 200u};
  for (unsigned int c = 0u; c < sizeof counts / sizeof counts[0]; ++c) {
    assert(threat_pattern_init(&s, counts[c], 125000000u));
    uint32_t silent = s.period | ((uint32_t)s.period << 16);
    for (unsigned int repeat = 0u; repeat < counts[c]; ++repeat) {
      if (repeat)
        for (unsigned int i = 0u; i < s.gap_samples; ++i)
          assert(threat_next_sample(&s) == silent);
      unsigned int offset = 0u;
      for (unsigned int e = 0u; e < THREAT_EVENT_COUNT; ++e)
        for (unsigned int n = 0u; n < s.events[e].samples; ++n) {
          uint32_t sample = threat_next_sample(&s);
          assert((sample & 65535u) <= s.period && (sample >> 16) <= s.period);
          assert((sample & 65535u) == s.period || (sample >> 16) == s.period);
          if (!s.events[e].frequency_hz) assert(sample == silent);
          if (!repeat) reference[offset] = sample;
          else assert(reference[offset] == sample);
          ++offset;
        }
      assert(offset == s.duration_ms * 100u);
    }
    assert(threat_synth_done(&s));
    for (unsigned int n = 0u; n < 512u; ++n) assert(threat_next_sample(&s) == silent);
  }
  puts("PASS: three complete phrases, two resonance bands, per-half jitter, note/gap bounds, 2 s phrase onsets, 6 s repetition cycle, counts 1/2/200 sample-identical, bounded PWM and terminal silence");
}
