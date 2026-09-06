# Fixed-tone comparison: play-2 versus play-3

The debug console now provides two continuous-tone commands:

```text
buzzer tone-2 6000 3
buzzer tone-3 6000 3
```

Run them separately. Both mean a full-scale 6000 Hz sine for three seconds.
They accept 100–10000 Hz and 1–5 whole seconds, using the project's existing
frequency/duration bounds. `buzzer off` stops either test. Page 6 lists both;
`help buzzer tone-2` and `help buzzer tone-3` show the syntax.

Keep the same RC filter, supply, and scope setup for both measurements.
Measure across the buzzer during the steady sounding interval, excluding
startup and shutdown. Record Vpp and RMS for each command. Listen to the
matched tones as well. The comparison can then be repeated at 3600 and
8300 Hz using the same duration, to cover the low and high play-3 bands.

These tests contain no pitch changes, burst gaps, whistles, repeat gaps,
or amplitude fades. Tone-2 uses play-2's actual sample renderer, buffers,
DMA channels, PWM settings, and IRQ. Tone-3 uses play-3's corresponding
actual engine. The temporary diagnostic entry points mirror their hardware
startup routines to preserve the original play-2/play-3 entry points.
Tone-3 represents its tone with same-frequency, phase-continuous events;
there is no gap at those internal boundaries.

The regular `buzzer play`, `buzzer crips-2`, and `buzzer crips-3` commands and
compositions remain unchanged. Fixed battery tones retain priority. This is
only a debug measurement tool, with no production trigger or saved setting.

## Verification

`python3 test/test_buzzer_tone_compare.py` compares the actual two sample
renderers. At 100, 2800, 3600, 6000, 8300, and 10000 Hz for durations of
1, 3, and 5 seconds, they produced identical 32-bit PWM compare words at
every sample. Exact duration, continuous phase, no internal gaps, terminal
silence, and invalid-input rejection passed with sanitizers enabled.

The digital result establishes that neither waveform calculation deliberately
attenuates the fixed tone. It does not measure the analogue buzzer voltage
or acoustic loudness, and cannot alone rule out hardware/timing differences.

Existing play-2 and three-part play-3 host tests passed. Exact source
comparisons verified the original `buzzer_play_2`, `buzzer_play_3`,
`build_bird`, `alarm_pattern_init`, both sample renderers, their IRQ handlers,
and the sine table unchanged. The production binary remains byte-identical.
Both firmware builds passed with `-Wall -Wextra` and no compiler warnings.
OpenOCD reported 4096 KiB flash and `Verified OK`; the debug image was reset
through the probe's stable serial path. No motor commands were issued.
