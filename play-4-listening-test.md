# Play-4: longer bird-inspired listening test

Use `buzzer crips-4 1`, or `buzzer crips-4 3` for three complete bouts.
`buzzer off` stops immediately. Count is 1–200, parsed and dispatched like
play-2/play-3. Debug only: no station, battery, or production trigger calls play-4.

## Actual composition

Three phrases start at 0, 2000 and 4000 ms. Each phrase starts with:

1. 100 ms high-band note.
2. 100 ms silence.
3. 100 ms high-band note.
4. 90 ms silence, then the short-note cluster below.

| Phrase | Cluster notes | Cluster frequency | Sound per note | Silence between notes |
|---|---:|---:|---:|---:|
| 1 | 6 | 4250–4350 Hz | 45–55 ms | 30–40 ms |
| 2 | 7 | 1950–2050 Hz | 45–55 ms | 35–45 ms |
| 3 | 8 | 4250–4350 Hz | 40–50 ms | 25–35 ms |

All introductory notes use 4250–4350 Hz, including phrase 2's introduction.
Every note draws a base within its band; its two contiguous halves independently
draw base ±25 Hz, clamped to that band. There is no silence at the half boundary.
Each note duration and each inter-note gap is drawn independently within its
listed inclusive integer-millisecond bounds. The final note of a cluster has
no extra short gap: the following phrase pause replaces it.

Phrase 1 lasts 810–920 ms; its following silence is 1080–1190 ms.
Phrase 2 lasts 915–1045 ms; its following silence is 955–1085 ms.
Phrase 3 lasts 885–1035 ms. One complete bout therefore lasts 4885–5035 ms,
with 27 sounding notes (six introductions plus 21 cluster notes), and
1505–1715 ms of actual sound. The rest is intentional silence.

The next whole bout starts at 6000 ms: the final repeat pause is 965–1115 ms.
A single requested bout finishes after its final note; no final repeat pause is
added. N bouts take `(N - 1) * 6000 + generated_bout_ms` milliseconds.
One random composition is generated per command and repeated sample-for-sample,
including note timing, frequency, and starting DDS phase. Another invocation
advances play-4's private PRNG. It does not consume the normal bird-call RNG.

The first composition after reset is 4949 ms: phrase ends at 870, 2988 and
4949 ms; phrase pauses 1130 and 1012 ms; repeat pause 1051 ms; sound 1604 ms.
These are generated digital timings, not acoustic measurements.

Clean full-scale sine, unchanged immutable sine table, 100 kHz PWM carrier.
No gain increase, clipping, new harmonics, or attack/release envelope was added.
The implementation uses its own DMA state and renderer, matching the existing
play-3 transport. Its roughly five-second duration is not subject to the
original generator's 650 ms software cap.

## Research basis and limits

Great-tit measurements give natural D notes of 50 ±10 ms and D–D gaps of
30 ±6 ms (mean ±SD); F notes 110 ±40 ms, F–F gaps 100 ±30 ms and F–D gaps
90 ±40 ms. The paper's Table A1 reports D–D spacing as 0.03 s, while the main
methods say 0.3 s; this implementation follows the table, corroborated by the
independent great-tit study below. Two F notes followed by six D notes were a
constructed playback template, not a universal bird formula.
[Salis et al., Animal Behaviour, October 2024](https://doi.org/10.1016/j.anbehav.2024.07.020).

Great tits facing a sparrowhawk produced D calls averaging 531 ms, 7.095 notes,
and 41 ms gaps, versus 419 ms, 6.063 notes and 34 ms gaps for a tawny owl.
Higher threat increased whole-call rate (15.03 versus 7.97 calls per bird per
minute) but also lengthened inter-note silence. Faster notes are not universally
more threatening. [Kalb, Anger & Randler, Scientific Reports, 25 April 2019](https://doi.org/10.1038/s41598-019-43087-9).

Japanese-tit experiments used 20 or 30 calls/minute, described as within natural
ranges; those are experimental playback rates, not measured population means.
Our two-second phrase onset uses the faster of these values. It is faster than
the great-tit mean above. [Suzuki, Wheatcroft & Griesser, Nature Communications,
8 March 2016](https://doi.org/10.1038/ncomms10986).

The three-phrase progression, low middle band, random distributions, denser
ending, unchanged full amplitude and exact repeated bout are engineering choices.
They combine timing evidence with the user's narrow piezo resonance constraints;
they are not a transcription of one species. The two bands are based on the
user's bench findings. Loudness and intruder deterrence remain unverified here.

Timing alone cannot establish recognition: Salis et al. found much less mobbing
to pure-tone control sequences than to natural or complete artificial calls.
Mobbing may recruit other birds toward the sound. Audio-only playback affected
stress-related behavior in captive raptors, but does not validate this synthetic
piezo output as a deterrent. [Consla & Mumme, Ethology, 21 August 2012](https://doi.org/10.1111/eth.12007).

## Verification, 6 September 2026

- Host test: 1000 generated compositions; exact frequency bands, jitter bounds,
  all note/gap durations, complete phrases at 0/2/4 s, and repeat cycle 6 s.
- Actual PWM renderer: counts 1, 2 and 200 repeat identical samples, valid PWM
  levels, silence in gaps and after completion; ASan/UBSan, -Wall -Wextra -Werror.
- Existing play-2, play-3 and tone-comparison tests pass.
- Play-3's four source/header files byte-identical to the pre-play-4 workspace.
  Existing buzzer.c differs only in four play-4 integration additions; existing
  play/play-2/play-3 parser/dispatch blocks and BUZZER constants are unchanged.
- Production release binary is byte-identical to the pre-play-3 reference.
- Debug and release build cleanly with -Wall -Wextra. OpenOCD reported 4096 KiB,
  programmed, Verified OK, and reset; stable probe UART console reset completed.
- Hardware console: help, invalid arguments, completion, count 200/off, and
  replacement by/from play-2 and play-3 passed. Tick: zero overruns; IDLE,
  position 6, duty 0. No motion commands issued.

Implementation: `src/buzzer_threat_pattern.c`, `src/buzzer_threat.c` and
`BUZZER_THREAT_*` in `src/config.h`. Portable test: `test/test_buzzer_threat.c`.
