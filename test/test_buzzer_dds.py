#!/usr/bin/env python3
"""Host checks for the firmware's actual DDS renderer and bird scheduler."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/buzzer.c').read_text()
state = source[source.index('#define BIRD_MAX_MS'):source.index('#ifdef LUFTFUGL_DEBUG\n/* Two')]
renderer = source[source.index('enum { DDS_BUFFER_SAMPLES'):source.index('static void dds_dma_irq')]
bird = source[source.index('static uint32_t rng_next'):source.index('static void buzzer_apply_frequency')]
checks = r'''
int main(void) {
  const uint16_t period = 1250;
  const uint32_t silent = period | ((uint32_t)period << 16);
  for (unsigned i = 0; i < 256; ++i) {
    uint32_t cc = buzzer_dds_compare(i << 24, period);
    unsigned a = cc & 65535, b = cc >> 16;
    assert(a <= period && b <= period);
    assert(a == period || b == period);
    double expected = sin(2.0 * 3.141592653589793 * i / 256) *
                      BUZZER_DDS_GAIN_PERCENT / 100.0;
    expected = fmax(-1.0, fmin(1.0, expected));
    assert(fabs(((double)a - b) / period - expected) < 0.001);
    uint32_t opposite = buzzer_dds_compare((i + 128u) << 24, period);
    assert((cc & 65535) == (opposite >> 16));
    assert((cc >> 16) == (opposite & 65535));
  }
  assert(buzzer_dds_compare(0, period) == silent);
  for (unsigned trial = 0; trial < 20; ++trial) {
    build_bird();
    assert(event_count && sequence_ms <= BIRD_MAX_MS);
    dds_period = period;
    dds_count = event_count;
    for (unsigned i = 0; i < event_count; ++i) {
      dds_lengths[i] = events[i].duration_ms * 100;
      dds_steps[i] = ((uint64_t)events[i].frequency << 32) / 100000;
      dds_on[i] = events[i].on;
    }
    dds_gap = BIRD_GAP_MS * 100;
    dds_index = dds_phase = dds_gap_remaining = 0;
    dds_remaining = dds_lengths[0];
    const unsigned counts[] = {1, 10, 11, 199, 200};
    unsigned repeats = counts[trial % 5];
    dds_plays = repeats;
    uint32_t phase = 0;
    for (unsigned play = 0; play < repeats; ++play) {
      if (play)
        for (unsigned n = 0; n < dds_gap; ++n)
          assert(dds_next_sample() == silent);
      for (unsigned i = 0; i < event_count; ++i) {
        for (unsigned n = 0; n < dds_lengths[i]; ++n) {
          uint32_t expected = silent;
          if (events[i].on) {
            expected = buzzer_dds_compare(phase, period);
            phase += dds_steps[i];
          }
          assert(dds_next_sample() == expected);
        }
      }
    }
    assert(dds_index == dds_count && dds_plays == 1);
    for (unsigned n = 0; n < 512; ++n)
      assert(dds_next_sample() == silent);
    dds_fill(0);
    assert(dds_terminal[0]);
  }
  puts("PASS: configured sine accuracy, polarity, zero braking, phase continuity, event timing, 1, 10, 11, 199, 200 repeats, gaps, terminal silence");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'dds_test.c'
    c.write_text('#include <stdbool.h>\n#include <assert.h>\n#include <math.h>\n#include <stdio.h>\ntypedef unsigned int uint;\n#include "buzzer_dds_wave.h"\n' + state + renderer + bird + checks)
    exe = Path(tmp) / 'dds_test'
    subprocess.run(['cc', '-std=c11', '-fsanitize=undefined,address', '-g', '-I', str(root / 'src'), str(c), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
