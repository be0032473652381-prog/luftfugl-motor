# Play-5: compact play-4

`buzzer crips-5 <1..200>` repeats the complete sequence. `buzzer off` stops it.

All 27 notes retain play-4's ordering, frequencies, durations, ±25 Hz clamped
per-half jitter, full-scale clean sine and phase progression. The two bands
remain 1950–2050 and 4250–4350 Hz. Each phrase still has two introductory notes
followed by 6, 7 or 8 cluster notes, respectively.

Only silence changes: each silent event and the whole-bout repeat pause is
multiplied by 50%, rounded up to the next integer millisecond. The two
between-phrase pauses and repeat pause are then halved once more; short
inter-note and introductory gaps retain their previous play-5 durations.

| Silence | Play-4 | Play-5 |
|---|---:|---:|
| Between introductory notes | 100 ms | 50 ms |
| Introduction to cluster | 90 ms | 45 ms |
| Within cluster 1 | 30–40 ms | 15–20 ms |
| Within cluster 2 | 35–45 ms | 18–23 ms |
| Within cluster 3 | 25–35 ms | 13–18 ms |
| After phrase 1 | 1080–1190 ms | 270–298 ms |
| After phrase 2 | 955–1085 ms | 239–272 ms |
| Between complete bouts | 965–1115 ms | 242–279 ms |

The first generated sequence after reset lasts 2746 ms instead of 4949 ms.
Sound remains 1604 ms; repeat silence becomes 263 ms. Three repeats therefore
last 8764 ms. Other generated compositions vary slightly. Unlike play-4,
phrase onsets are no longer fixed at two-second intervals.

The original composition is generated first, using a private copy of play-4's
PRNG and generation algorithm, then only silence events are shortened. This
preserves every random draw and avoids altering play-4's PRNG state. Matched
invocation numbers after reset generate matching notes in play-4 and play-5.
Each mode repeats its generated pattern exactly for the requested count.

Tuning: `BUZZER_COMPACT_SILENCE_PERCENT` and
`BUZZER_COMPACT_LONG_PAUSE_PERCENT` in `src/config.h`. All other parameters
continue to use play-4's existing `BUZZER_THREAT_*` values. Existing play-4
source files remain unchanged. Play-5 has independent playback/DMA state and
is used by genuine station arrivals at positions 2–5 (1/2/3/4 repeats) in
both builds, and remains reachable from the debug console.

Verification: `test/test_buzzer_compact.c` compares 1000 matched compositions,
checks all silence reductions and unchanged sounding events, compares actual
sounding PWM samples across 10 full compositions, and tests exact repeats for
counts 1, 2 and 200 with terminal silence. Sanitizers and -Wall -Wextra -Werror
are enabled for the host test. Existing play-4 and DDS regression checks pass.

Debug/release builds passed with -Wall -Wextra and no compiler warnings.
OpenOCD identified 4096 KiB flash, programmed and verified the debug image.
Hardware console checks passed help, invalid arguments, complete playback,
count 200 with stop, and switching through all four DDS players in one session.
The debug SDK shared-handler capacity is five: four DDS players plus the
DS3231 GPIO handler. This avoids exhausting the default four-handler pool.
That listening-only release comparison predates station integration; crips-5
is now compiled into release firmware for station arrivals.
Console reset completed after tests. No motion commands were issued.
