#include "buzzer_alarm_pattern.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static uint32_t reference[ALARM_CALL_MS * 100u];

int main(void) {
  alarm_synth_t synth;
  assert(!alarm_pattern_init(&synth, 0u, 125000000u));
  assert(!alarm_pattern_init(&synth, 201u, 125000000u));
  assert(!alarm_pattern_init(&synth, UINT_MAX, 125000000u));
  assert(!alarm_pattern_init(&synth, 1u, 0u));
  /* Reject a system clock too low to configure the PWM carrier. */
  assert(!alarm_pattern_init(&synth, 1u, 60000u));
  unsigned int min_hz[3] = {UINT_MAX, UINT_MAX, UINT_MAX};
  unsigned int max_hz[3] = {0u, 0u, 0u};
  for (unsigned int trial = 0u; trial < 1000u; ++trial) {
    assert(alarm_pattern_init(&synth, 1u, 125000000u));
    assert(synth.period == 1250u);
    assert(ALARM_EVENT_COUNT == 86u);
    unsigned int ms = 0u, sounding_ms = 0u;
    for (unsigned int i = 0u; i < ALARM_EVENT_COUNT; ++i) {
      alarm_event_t event = synth.events[i];
      assert(event.samples == event.duration_ms * 100u);
      ms += event.duration_ms;
      if (event.frequency_hz) sounding_ms += event.duration_ms;
      if (i == 24u || i == 49u) {
        assert(event.frequency_hz == 0u && event.duration_ms == 50u);
        assert(ms == (i == 24u ? 162u : 324u));
        continue;
      }
      unsigned int part = i < 24u ? 0u : i < 49u ? 1u : 2u;
      unsigned int relative = i - (part == 0u ? 0u : part == 1u ? 25u : 50u);
      unsigned int half = relative % 3u;
      if (half == 2u) {
        assert(event.frequency_hz == 0u);
        assert(event.duration_ms == (part == 2u ? 1u : 2u));
        continue;
      }
      const unsigned int low[] = {4250u, 1950u, 4250u};
      const unsigned int high[] = {4350u, 2050u, 4350u};
      assert(event.frequency_hz >= low[part] && event.frequency_hz <= high[part]);
      assert(event.duration_ms == (part == 2u ? (half == 0u ? 4u : 5u) : 6u));
      if (half == 1u) {
        int delta = (int)event.frequency_hz - synth.events[i - 1u].frequency_hz;
        assert(delta >= -50 && delta <= 50);
      }
      if (event.frequency_hz < min_hz[part]) min_hz[part] = event.frequency_hz;
      if (event.frequency_hz > max_hz[part]) max_hz[part] = event.frequency_hz;
      double actual_hz = event.phase_step * (100000.0 / 4294967296.0);
      assert(fabs(actual_hz - event.frequency_hz) < 100000.0 / 4294967296.0);
    }
    assert(ms == 444u && ms <= BUZZER_ALARM_CALL_MAX_MS);
    assert(sounding_ms == 300u);
  }
  printf("Observed over 1000 calls: Part 1 %u..%u Hz; Part 2 %u..%u Hz; Part 3 %u..%u Hz\n",
         min_hz[0], max_hz[0], min_hz[1], max_hz[1], min_hz[2], max_hz[2]);
  assert(min_hz[0] == 4250u && max_hz[0] == 4350u);
  assert(min_hz[1] == 1950u && max_hz[1] == 2050u);
  assert(min_hz[2] == 4250u && max_hz[2] == 4350u);
  double dc = 0, first = 0, third = 0;
  for (unsigned int i = 0u; i < 256u; ++i) {
    double angle = 2.0 * 3.141592653589793 * i / 256.0;
    int32_t sample = alarm_wave_sample(i << 24);
    assert(sample >= -32767 && sample <= 32767);
    assert(sample == -alarm_wave_sample((i + 128u) << 24));
    double expected = sin(angle);
    assert(fabs(sample / 32767.0 - expected) < 0.0002);
    dc += sample;
    first += sample * sin(angle);
    third += sample * sin(3 * angle);
  }
  assert(fabs(dc) < 0.001);
  assert(fabs(third / first) < 0.0001);
  const unsigned int counts[] = {1, 2, 11, 199, 200};
  for (unsigned int c = 0u; c < sizeof counts / sizeof counts[0]; ++c) {
    assert(alarm_pattern_init(&synth, counts[c], 125000000u));
    uint32_t silent = synth.period | ((uint32_t)synth.period << 16);
    assert(synth.gap_samples == 10000u);
    for (unsigned int play = 0u; play < counts[c]; ++play) {
      if (play)
        for (unsigned int i = 0u; i < synth.gap_samples; ++i)
          assert(alarm_next_sample(&synth) == silent);
      unsigned int offset = 0u;
      for (unsigned int event = 0u; event < ALARM_EVENT_COUNT; ++event) {
        for (unsigned int n = 0u; n < synth.events[event].samples; ++n) {
          uint32_t cc = alarm_next_sample(&synth);
          assert((cc & 65535u) <= synth.period && (cc >> 16) <= synth.period);
          assert((cc & 65535u) == synth.period || (cc >> 16) == synth.period);
          if (!synth.events[event].frequency_hz)
            assert(cc == silent);
          if (!play)
            reference[offset] = cc;
          else
            assert(reference[offset] == cc);
          ++offset;
        }
      }
      assert(offset == 44400u);
    }
    assert(alarm_synth_done(&synth));
    for (unsigned int i = 0u; i < 512u; ++i)
      assert(alarm_next_sample(&synth) == silent);
  }
  puts("PASS: 1000 compositions, all three parts, 444 ms calls, two 50 ms breaks, denser 12-burst climax, 300 ms sounding, 100 ms gaps, frequency bounds, invalid-clock rejection, zero DC, clean sine, exact waveform repeats through 200 calls, terminal silence");
}
