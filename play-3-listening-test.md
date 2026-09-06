# Play-3: three-part threat-display listening test

This is the current composition and replaces all earlier play-3 descriptions.
It is debug-only, with no production trigger or station-arrival integration.

```text
buzzer crips-3 3
buzzer off
help buzzer crips-3
```

The count is 1–200 complete three-part sequences. One randomized composition
is generated per invocation and its exact samples are repeated for every
cycle. Count never refers to a part or a burst. Independent play-3 random
state avoids advancing the default bird generator's RNG.

## The complete 444 ms sequence

| Segment | Burst count | Sound per burst | Gap after each burst | Generated frequency band | Segment time |
|---|---:|---:|---:|---|---:|
| Part 1 | 8 | 12 ms: 6 + 6 ms halves | 2 ms | 4250–4350 Hz | 112 ms |
| Break 1 | — | silence | — | — | 50 ms |
| Part 2 | 8 | 12 ms: 6 + 6 ms halves | 2 ms | 1950–2050 Hz | 112 ms |
| Break 2 | — | silence | — | — | 50 ms |
| Part 3 | 12 | 9 ms: 4 + 5 ms halves | 1 ms | 4250–4350 Hz | 120 ms |

Total: `8*(12+2) + 50 + 8*(12+2) + 50 + 12*(9+1) = 444 ms`.
The parts begin at 0, 162, and 324 ms. The two explicit breaks are at
112–162 and 274–324 ms. The final per-burst gaps are included in the segment
times, so the continuous silence at either internal break is 52 ms including
the preceding 2 ms burst gap. This remains within the requested 40–80 ms.

A 100 ms inter-sequence gap follows each complete sequence except the last.
Starts are 544 ms apart. Three calls take 1532 ms before the short DMA
terminal-silence housekeeping interval; 200 take 108.7 seconds. The final
1 ms burst gap is already part of the 444 ms, not part of the external gap.

Part 3 has 50% more bursts, a 10 ms burst cycle versus 14 ms, and 90% sound
occupancy versus about 86% in the first two clusters. That is the programmed
density escalation; it is not a claim of greater perceived loudness. All
parts use the same burst texture. No new oscillator texture was needed.

The 500 ms compile-time composition ceiling leaves room above 444 ms. The
older 650 ms ceiling belongs to the original bird generator's software;
it is not an absolute hardware playback limit. No original ceiling changed.

## Frequency generation

Part-specific constants in `src/config.h` specify the bands above. Each
burst draws a base from its band's inclusive limits. Each half independently
draws within ±25 Hz of that base and clamps to the same band's endpoints:

```c
static uint16_t alarm_burst_pitch(uint16_t base, uint16_t low, uint16_t high) {
  uint16_t frequency = alarm_random(base - BUZZER_ALARM_PITCH_JUMP_HZ,
                                    base + BUZZER_ALARM_PITCH_JUMP_HZ);
  if (frequency < low)
    frequency = low;
  if (frequency > high)
    frequency = high;
  return frequency;
}
```

The cluster uses it twice, with no silent event between the halves:

```c
uint16_t base = alarm_random(low, high);
alarm_event(synth, index, alarm_burst_pitch(base, low, high),
             sound_ms / 2u, sys_hz);
alarm_event(synth, index, alarm_burst_pitch(base, low, high),
             sound_ms - sound_ms / 2u, sys_hz);
alarm_event(synth, index, 0u, gap_ms, sys_hz);
```

Without these clamps the three bands would extend to 4225–4375,
1925–2075, and 4225–4375 Hz. Clamping keeps each played half in its stated
band; endpoint frequencies are consequently more common.

The bands target the user's two measured acoustic peaks around 4300 and
2000 Hz. Parts 1 and 3 use the upper peak; Part 2 uses the lower peak for
contrast. The independent per-half jitter is now ±25 Hz. The burst counts,
timings, clean sine, and denser third part are unchanged.

Observed over 1000 generated sequences:

```text
Part 1 4250..4350 Hz
Part 2 1950..2050 Hz
Part 3 4250..4350 Hz
```

Phase-increment quantization error is under 0.000024 Hz at the current
100 ksample/s DDS rate. Bounds refer to programmed fundamentals, not every
spectral component of PWM or abrupt tone gating. The 4350 Hz maximum
fundamental is below the 50 kHz Nyquist boundary, checked against the actual
sample rate. Louder standard square-wave chirps do not prove these same
bands will sound equally loud with a sine drive; actual loudness remains
for the user's listening test. No claim of measured improvement is made.

## Waveform and command structure

Play-3 remains a full-scale clean sine with no added harmonics, clipping,
or fades. `BUZZER_DDS_GAIN_PERCENT` remains 100. The sine table and play-2
waveform generator have not changed. No waveform redesign was performed.

Play-3 retains the structural sibling parsing pattern:

```c
} else if (!strncmp(arg, "play-3 ", 7u)) {
  long count;
  if (!parse_long(arg + 7u, &count) || count < 1 || count > (long)BUZZER_PLAY_MAX) {
    result(original, "rejected", "usage: buzzer crips-3 1..200");
  } else if (!buzzer_play_3((unsigned int)count)) {
    result(original, "rejected", "DDS unavailable or battery tone active");
  } else {
    result(original, "complete", "chirps-3 three-part test started; 444 ms sequence, 100 ms gaps");
  }
```

Its existing separate DMA player is unchanged in this revision. It generates
the whole sequence once, retains phase across pitch changes within a call,
and resets phase only when replaying that same complete sequence. Existing
stop, completion, and battery-tone priority handling are retained.

## Verification

Both debug and release builds passed under `-Wall -Wextra` with no compiler
warnings. Production `luftfugl.bin` remains byte-identical to the pre-play-3
binary. The new source files are linked only in debug builds.

The grep-equivalent call-site check was:

```sh
rg -n 'build_bird\(|buzzer_play_2\(|buzzer_play_3\(|station_arrival_bird_plays|buzzer_play\(bird_plays' src
```

Only the debug console calls `buzzer_play_3`. The station trigger remains
`buzzer_play(bird_plays)` in `src/console.c`. Exact function comparisons
against pre-play-3 commit `6987321` verified unchanged `build_bird`,
`append_event`, `rng_next`, `rng_range`, `buzzer_play_2`, `dds_next_sample`,
`dds_fill`, `dds_dma_irq`, and `dds_stop`. The play-2 parser is also unchanged.
In this three-part revision the entire files `src/buzzer.c`, `src/console.c`,
`src/main.c`, `src/buzzer_dds_wave.h`, and `src/buzzer_alarm.c` are unchanged.
Grep alone would not establish this; the exact comparisons supplement it.

Host verification passed with AddressSanitizer and UndefinedBehaviorSanitizer:

- 1000 generated sequences, exactly 86 events, all three frequency bands.
- Eight/eight/twelve bursts, independent halves, and 1 ms climax gaps.
- Two explicit 50 ms breaks at the expected sequence positions.
- Exactly 44400 samples per sequence and 10000 per repeat gap at 100 kHz.
- Exact whole-sequence waveform repetition at counts 1, 2, 11, 199, and 200.
- Clean sine, zero DC, bounded compare values, and terminal silence.
- Existing play-2 host tests passed unchanged.

Reproduce:

```sh
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined \
  -DLUFTFUGL_DEBUG=1 -Isrc test/test_buzzer_alarm.c \
  src/buzzer_alarm_pattern.c -lm -o /tmp/luftfugl-test-alarm
/tmp/luftfugl-test-alarm
python3 test/test_buzzer_dds.py
```

OpenOCD identified 4096 KiB flash and reported `Verified OK`. A console reset
was sent over the stable probe serial path after flashing. No motor commands
were issued. Physical waveform and acoustic response need user evaluation.

Live debug-console verification after this flash passed: counts 0, 201, and
`2x` rejected; `help buzzer crips-3` displayed the three-part structure;
`buzzer crips-3 3` stayed active beyond the first 444 ms sequence and later
reported off; `buzzer crips-3 200` was accepted and stopped with `buzzer off`.
The terminal was resumed and the console reset to restore its menu.

## Clean-sine sounding-time optimization

The standard player uses complementary 50% square-wave drive directly at
the note frequency. Play-3 uses 100 kHz PWM encoding of a full-scale sine;
its maximum commanded pulse is 1249/1250 (99.92%) at the current clock.
There is no meaningful unused clean-sine peak amplitude to turn up. At equal
peak voltage an ideal square wave has sqrt(2) times a sine's RMS voltage;
this is an electrical comparison, not a prediction of acoustic loudness.

A host run of the actual default generator over 1000 compositions gave:
mean 650 ms total, mean 508 ms sounding, frequencies 3301–9195 Hz, and no
sounding events in the 2700–3000 Hz band. The user's louder standard chirps
therefore show that frequency placement alone is insufficient to explain
the difference. The standard waveform, longer sustained whistles, and
proportion of sounding time are other differences.

Play-3 now substitutes sound for some intra-burst silence while preserving
all frequencies, burst counts, 50 ms breaks, the 444 ms complete sequence,
and the 100 ms repeat gap. Parts 1/2 change from 10+4 to 12+2 ms; Part 3
changes from 8+2 to 9+1 ms. Sounding time rises from 256 to 300 ms (17.19%).
At equal steady sine amplitude this represents only about +0.69 dB in
whole-cycle mean-square voltage, not a verified acoustic gain. A large
loudness gap cannot be promised closed by this adjustment. The clean-sine
requirement is preserved and no square-wave or clipping mode was introduced.

Updated host tests verify exactly 300 ms sounding while all three-part
frequency, timing, waveform, and repetition checks continue to pass.

## Measured-peak band revision

The play-3 frequency constants now specify Parts 1/3 at 4250–4350 Hz and
Part 2 at 1950–2050 Hz, with independent ±25 Hz half-burst jitter clamped
to the part's band. The 444 ms sequence, 300 ms sounding time, 50 ms breaks,
100 ms repeat gap, and full-scale clean sine remain unchanged. Tests over
1000 compositions verified every sounding event within those bands and
half-pair differences no greater than 50 Hz. Exact repetitions through
200 cycles and matched fixed-tone renderers also passed.
