#!/usr/bin/env python3
"""Compare the actual play-2 and play-3 sample renderers at fixed tones."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'src/buzzer.c').read_text()
state = s[s.index('#define BIRD_MAX_MS'):s.index('#ifdef LUFTFUGL_DEBUG\n/* Two')]
renderer = s[s.index('enum { DDS_BUFFER_SAMPLES'):s.index('static void dds_dma_irq')]
checks = r'''
int main(void) {
  alarm_synth_t alarm;
  assert(!alarm_tone_init(&alarm, 99, 1000, 125000000));
  assert(!alarm_tone_init(&alarm, 10001, 1000, 125000000));
  assert(!alarm_tone_init(&alarm, 6000, 999, 125000000));
  assert(!alarm_tone_init(&alarm, 6000, 5001, 125000000));
  const unsigned frequencies[] = {100, 2800, 3600, 6000, 8300, 10000};
  const unsigned durations[] = {1000, 3000, 5000};
  for (unsigned f = 0; f < sizeof frequencies / sizeof frequencies[0]; ++f) {
    for (unsigned d = 0; d < sizeof durations / sizeof durations[0]; ++d) {
      unsigned hz = frequencies[f], ms = durations[d];
      assert(alarm_tone_init(&alarm, hz, ms, 125000000));
      dds_period = 1250;
      dds_count = 1;
      dds_lengths[0] = ms * 100;
      dds_steps[0] = ((uint64_t)hz << 32) * 1250 / 125000000;
      dds_on[0] = true;
      dds_index = dds_phase = dds_gap_remaining = 0;
      dds_remaining = dds_lengths[0];
      dds_plays = 1;
      uint32_t silent = 1250u | (1250u << 16);
      unsigned total_ms = 0;
      for (unsigned i = 0; i < ALARM_EVENT_COUNT; ++i) {
        assert(alarm.events[i].frequency_hz == hz);
        assert(alarm.events[i].samples > 0);
        total_ms += alarm.events[i].duration_ms;
      }
      assert(total_ms == ms);
      for (unsigned n = 0; n < ms * 100; ++n) {
        assert(!alarm_synth_done(&alarm));
        assert(alarm_next_sample(&alarm) == dds_next_sample());
      }
      assert(alarm_synth_done(&alarm));
      assert(dds_index == dds_count);
      for (unsigned n = 0; n < 1024; ++n) {
        assert(alarm_next_sample(&alarm) == silent);
        assert(dds_next_sample() == silent);
      }
    }
  }
  puts("PASS: tone-2 and tone-3 generate identical compare words at 100, 2800, 3600, 6000, 8300, 10000 Hz for 1, 3, 5 s; exact lengths, no gaps, terminal silence, invalid input rejection");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'compare.c'
    c.write_text('#include <stdbool.h>\n#include <assert.h>\n#include <stdio.h>\ntypedef unsigned int uint;\n#include "buzzer_dds_wave.h"\n#include "buzzer_alarm_pattern.h"\n' + state + renderer + checks)
    exe = Path(tmp) / 'compare'
    subprocess.run(['cc', '-std=c11', '-O2', '-fsanitize=address,undefined', '-DLUFTFUGL_DEBUG=1', '-I', str(root/'src'), str(c), str(root/'src/buzzer_alarm_pattern.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
