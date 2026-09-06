# Crips-5 — technical specification and ornithological review dossier

**Project:** luftfugl-motor  
**Document revision:** 1.0 — 6 September 2026  
**Canonical sound name:** `crips-5`  
**Equivalent conversational names:** “crips5” and “crisp-5”  
**Former debug name:** `play-5`  
**Audience:** reviewing ornithologist, acoustic measurement engineer and firmware maintainer  
**Implementation status:** integrated into station-arrival behavior in debug and release firmware  
**Ornithological approval status:** **not recorded; this document requests review, not an assumption of approval**

## 1. Purpose and scope of the review

Crips-5 is a synthesized, three-part bird-inspired sound used to announce selected station arrivals in the Luftfugl mechanism. Its development began with alarm/mobbing/threat-call ideas, but **the present production trigger is a station-arrival event, not detection of a predator or intruder**. These are separate questions for review:

1. Does the synthesized signal resemble a meaningful call to any relevant bird species, and if so, what response might it produce?
2. Is its use as a station indicator appropriate in the intended installation, at the actual sound level and repetition frequency?
3. If predator deterrence is still a proposed objective, what separate evidence would be required to support that use?

This note describes the **current implemented waveform and timing**, including the most recent reduction of the two between-part pauses and the between-sequence pause. It supersedes older timing descriptions of play-5. It does not alter the firmware, set a sound-pressure limit, or approve field use.

The terms “aggressive,” “contrast” and “climax” describe the developer's sound-design intention. They are not verified interpretations by a bird. The signal is not a recording or a validated imitation of a particular species. Its two frequency bands were selected for this piezo's reported output, rather than because an ornithological study prescribed those bands.

### Evidence categories used here

| Label | Meaning |
|---|---|
| Implemented | Directly read from the current source code |
| Derived | Calculated from that implementation and stated clock assumptions |
| Host verified | Exercised using the actual C generator/renderer or arrival-dispatch code on a host computer |
| Hardware verified | Confirmed by firmware build, SWD verification or UART readback; not automatically an acoustic measurement |
| Bench reported | Observation reported by the project owner; not independently calibrated or reproduced for this note |
| Literature | Result from the specifically cited species, experiment and context |
| Review required | Unknown or unresolved item for the ornithologist/acoustic assessment |

## 2. Executive specification

| Property | Current crips-5 implementation |
|---|---|
| Complete sequence | Three parts, in fixed order |
| Notes per part | 8, 9 and 10: each has two introductory notes followed by 6, 7 or 8 shorter bursts |
| Total notes | **27**: six introductory notes and 21 bursts |
| High band | **4250–4350 Hz**, inclusive nominal integer frequencies |
| Low band | **1950–2050 Hz**, inclusive nominal integer frequencies |
| Band ordering | All introductions high; burst clusters high / low / high |
| Introductory note duration | **100 ms** each |
| Burst duration | Parts 1/2: **45–55 ms**; part 3: **40–50 ms** |
| Structure within every note | Two contiguous pitch halves, with no silence between them |
| Per-half pitch perturbation | Independent pseudorandom draws within base ±25 Hz, then clamped to the part's band |
| Waveform | Full-scale Q15 sine-table DDS, transmitted as differential high-frequency PWM |
| Envelope | Abrupt on/off gating; no programmed attack, decay, fade or amplitude escalation |
| First generated reference sequence | **2746 ms** from first note onset to final note end |
| First reference repeat pause | **263 ms**; next sequence begins at **3009 ms** |
| Sound-on time in reference sequence | **1604 ms** |
| General duration | Approximately **2.75 s**; analytical timing bounds are given in section 6 |
| Repetition | One generated composition repeated exactly; new composition on each new invocation |
| Manual command | `buzzer crips-5 <count>`, count **1–200** complete sequences |
| Stop command | `buzzer off` |
| Production use | Arrival at stations 2/3/4/5 requests 1/2/3/4 complete sequences |
| Silent stations | 1 and 6 (`EVENT_POS`) |

“Frequency band” here means the programmed **fundamental during each half-note**. It is not a claim that every component of the electrical or acoustic spectrum lies within those bands; see section 8.

Source: [configuration](../src/config.h), [pattern generator and renderer](../src/buzzer_compact_pattern.c), [DMA/PWM player](../src/buzzer_compact.c), [station dispatch](../src/console.c).

## 3. Definitions and sequence hierarchy

- **Half-note:** one constant-frequency, constant-amplitude DDS segment. A note contains two half-notes.
- **Note:** one uninterrupted sounding interval. “Burst” means a shorter note in a cluster, not a pulse of broadband noise.
- **Introduction:** two 100 ms high-band notes at the start of each part.
- **Part / phrase:** one introduction followed by one burst cluster. These are engineering divisions, not established biological syllable categories.
- **Complete sequence / bout:** all three parts and the two internal long pauses. The final repeat pause is not included in its duration.
- **Count:** the number of complete sequences, not the number of parts, notes or half-notes.
- **Silence:** zero requested differential DDS output; the piezo may still ring acoustically after the drive stops.

```text
Part 1: high introduction → 6 high-band bursts
             ↓ 270–298 ms pause
Part 2: high introduction → 7 low-band bursts
             ↓ 239–272 ms pause
Part 3: high introduction → 8 high-band bursts
             ↓ 242–279 ms pause, only if another sequence is requested
Repeat the same complete sequence
```

**Part 2 is not entirely low-frequency:** its two introductory notes remain in the high band. Only its seven-burst cluster uses the low band.

Within every part:

```text
100 ms high note
50 ms silence
100 ms high note
45 ms silence
burst 1 → short gap → burst 2 → … → final burst
```

The introduction occupies **295 ms** from its first onset to the first burst onset. There is no extra short gap after a cluster's final burst: the between-part pause replaces it, or the complete sequence ends.

## 4. Sounding-note parameters

### 4.1 Counts, frequencies and durations

| Part | Introductory notes | Burst count | Intro frequencies | Burst frequencies | Sound per burst | First half | Second half |
|---|---:|---:|---|---|---|---|---|
| 1 | 2 × 100 ms | 6 | 4250–4350 Hz | 4250–4350 Hz | 45–55 ms | 22–27 ms | 23–28 ms |
| 2 | 2 × 100 ms | 7 | 4250–4350 Hz | 1950–2050 Hz | 45–55 ms | 22–27 ms | 23–28 ms |
| 3 | 2 × 100 ms | 8 | 4250–4350 Hz | 4250–4350 Hz | 40–50 ms | 20–25 ms | 20–25 ms |

Every introduction is split into **50 ms + 50 ms**. The two half durations of a burst are linked, not independently chosen:

```text
first_half_ms  = floor(note_duration_ms / 2)
second_half_ms = note_duration_ms - first_half_ms
```

For example, a 47 ms burst has a 23 ms first half and a 24 ms second half. A 50 ms burst has two 25 ms halves. There is no discontinuity in the phase accumulator at their boundary, but there can be an instantaneous frequency change.

There are no whistles, rising/falling frequency sweeps, extra random rests, falling tail or separate rattle section in crips-5. Its shorter notes use the same two-half mechanism as its introductions.

### 4.2 Exact pitch selection

For every note:

```text
base = random_integer(low_band_edge, high_band_edge)
f1   = clamp(random_integer(base - 25, base + 25), low_band_edge, high_band_edge)
f2   = clamp(random_integer(base - 25, base + 25), low_band_edge, high_band_edge)
```

The actual generation code is:

```c
uint16_t base = compact_random(low, high);
compact_event(synth, index, compact_burst_pitch(base, low, high), ms / 2u, sys_hz);
compact_event(synth, index, compact_burst_pitch(base, low, high), ms - ms / 2u, sys_hz);
```

The base frequency is not itself a third sounding segment. Each half may equal or differ from the other; a pitch jump is not mandatory. The maximum difference between the halves is **50 Hz**. Clamping prevents a ±25 Hz perturbation from escaping the chosen band.

The two perturbation draws are separate, but their final frequencies are statistically related because they share one base. Edge clamping concentrates some probability at the band edges. Consequently, played half-note frequencies are **not uniformly distributed across the band**.

### 4.3 Randomness and reproducibility

The generator has a private 32-bit xorshift state initialized to `0x4B1DCA11`:

```c
compact_rng ^= compact_rng << 13;
compact_rng ^= compact_rng >> 17;
compact_rng ^= compact_rng << 5;
return low + compact_rng % (high - low + 1u);
```

All selections are inclusive integer draws. Modulo reduction gives an approximately uniform selection before clamping, with small modulo bias; it is not true randomness or a fitted distribution of natural bird vocalizations.

The state is not reseeded from environmental noise or time. Reset restores the starting state. A new accepted crips-5 invocation advances the private sequence; the requested repeat count does not cause regeneration between cycles. Station arrivals and manual crips-5 commands use this same generator state. An earlier station sound may therefore consume the first composition before a manual listening command is entered.

Crips-5 first constructs the original crips-4-style timing, consuming the same pattern of draws, and then shortens silence events. It does not modify sounding segments or consume extra random draws during compression. Its PRNG is separate from the other chirp players.

## 5. Every silence interval

| Location | Current silence | Occurrences per complete sequence |
|---|---:|---:|
| Between introductory notes | **50 ms** | 3 |
| After the second introductory note, before burst 1 | **45 ms** | 3 |
| Between bursts in part 1 | **15–20 ms** | 5 |
| Between bursts in part 2 | **18–23 ms** | 6 |
| Between bursts in part 3 | **13–18 ms** | 7 |
| Part 1 → part 2 | **270–298 ms** | 1 |
| Part 2 → part 3 | **239–272 ms** | 1 |
| Between complete repeated sequences | **242–279 ms** | count − 1 per invocation |
| Between the two halves of a note | **0 ms** | All 27 notes |
| After the last sequence in an invocation | No prescribed repeat pause | Playback finishes |

The implementation contains **80 events**: 54 sounding half-notes and 26 internal silence events. Between-sequence silence is held separately in the repeat state, not in the 80-event array.

### 5.1 Compression and integer rounding

Define:

```text
half_up(x) = ceil(x / 2) = (x + 1) / 2, using integer division
```

The general silence control is `BUZZER_COMPACT_SILENCE_PERCENT = 50`. The additional long-pause control is `BUZZER_COMPACT_LONG_PAUSE_PERCENT = 50`.

- Introductory and inter-burst gaps are shortened **once**: `half_up(original_gap)`.
- The two between-part pauses and the repeat pause are shortened **twice**: `half_up(half_up(original_pause)) = ceil(original_pause / 4)`.
- Both operations round upward to integer milliseconds.
- No sounding duration is scaled.

Part 1 burst gaps originate from integer draws 30–40 ms; part 2 from 35–45 ms; part 3 from 25–35 ms. Because multiple original values can round to the same shortened gap, the final gap values are not equally probable even if original draws were perfectly uniform.

### 5.2 Why the long pauses vary

Long pauses are **calculated from the generated notes and gaps**, not selected independently from the displayed ranges.

For part `i`, let:

```text
S_i = sum of its burst sound durations (introductory notes excluded)
G_i = sum of its original, uncompressed inter-burst gaps
H_i = sum of ceil(each original inter-burst gap / 2)

L_i = 390 + S_i + G_i       original part duration
D_i = 295 + S_i + H_i       current crips-5 part duration
P_i = ceil((2000 - L_i) / 4)
```

The original introduction occupied 390 ms; the current one occupies 295 ms. `P_1` and `P_2` are between-part pauses. `P_3` is the pause before another complete sequence.

This retains the original construction dependency on a 2000 ms phrase slot, but **crips-5 does not play at fixed two-second phrase onsets**. Longer generated original parts tend to yield shorter long pauses. Adding independent minimum or maximum rows from the tables can therefore give incorrect total-duration bounds.

In the event array, the two internal long pauses are indices **23** and **50**. Their positions are computed from the configured note counts, rather than detected by an arbitrary duration threshold.

## 6. Complete duration, repetition and exposure

### 6.1 Exact duration equations

```text
T = D_1 + P_1 + D_2 + P_2 + D_3       one complete sequence
C = T + P_3                           repeating cycle, onset to next onset
elapsed(N) = N*T + (N - 1)*P_3        N complete sequences
```

For a new invocation, the pattern is generated once. All `N` sequences have the same `T`, `P_3`, frequencies and note timings. The phase accumulator resets to zero at the start of each repeated complete sequence, so repeated digital waveforms match, not merely the frequency list.

### 6.2 Analytical timing bounds

The following are derived bounds over the permitted integer duration/gap combinations. They are **not population measurements, probability intervals, or a claim that a finite PRNG sample visits every extreme**.

| Quantity | Derived range |
|---|---:|
| Part 1, excluding following long pause (`D_1`) | 640–725 ms |
| Part 2, excluding following long pause (`D_2`) | 718–818 ms |
| Part 3, excluding repeat pause (`D_3`) | 706–821 ms |
| Part 1 plus its following pause (`D_1 + P_1`) | 938–997 ms |
| Part 2 plus its following pause (`D_2 + P_2`) | 988–1057 ms |
| Part 3 plus its repeat pause (`D_3 + P_3`) | 983–1063 ms |
| Complete sequence (`T`) | **2632–2875 ms** |
| Repeating cycle (`C`) | **2909–3117 ms** |
| Total sounding time | **1505–1715 ms** |
| High-band sounding time | 1190–1330 ms |
| Low-band sounding time | 315–385 ms |

The bounds account for the coupling between each original part and its following pause. They can be reproduced by enumerating sums of original gaps and their individually rounded halves; notes can realize every integer sum between their count-scaled duration limits. The 1000-composition host regression run observed **2717–2786 ms** sequence durations; that finite observed range is narrower than the allowed analytical bounds.

### 6.3 Reference sequence and repeated exposure

For the first generated composition at the nominal 125 MHz system clock:

| Item | Value |
|---|---:|
| Part 1 onset / end | 0 / 689 ms |
| First long pause | 283 ms |
| Part 2 onset / end | 972 / 1744 ms |
| Second long pause | 253 ms |
| Part 3 onset / end | 1997 / 2746 ms |
| Repeat pause | 263 ms |
| Next complete sequence onset | 3009 ms |
| Sounding time by part | 504 / 555 / 545 ms |
| Total high / low sounding time | 1249 / 355 ms |
| Sound-on fraction within one sequence | Approximately 58.41% |
| Sound-on fraction during uninterrupted repeating cycles | Approximately 53.31% |

These sound-on fractions describe scheduled digital drive time, not radiated acoustic energy or measured RMS voltage.

| Requested count | Reference elapsed time | Reference sound-on time |
|---:|---:|---:|
| 1 | 2.746 s | 1.604 s |
| 2 | 5.755 s | 3.208 s |
| 3 | 8.764 s | 4.812 s |
| 4 | 11.773 s | 6.416 s |
| 200 | 601.537 s — approximately 10 min | 320.800 s |

The manual upper limit of 200 is an engineering/debug count limit, **not an ornithologically approved exposure duration**. Station arrivals request at most four sequences per event. There is no dedicated crips-5 hourly/daily exposure cap or minimum interval between distinct arrival-triggered invocations in the current sound-dispatch path. Actual accumulated exposure depends on station activity and interruptions.

For the reference continuous cycle, three phrases per 3.009 seconds correspond to approximately **59.8 phrases/minute**. Calling one three-part sequence “one call” must not obscure this internal phrase rate when comparing it with biological studies.

## 7. DDS and electrical drive

### 7.1 Hardware and signal path

```text
RP2040 fixed-point DDS
  → 32-bit DMA writes to one PWM slice's A/B compare registers
  → BIN1 GP6 and BIN2 GP7
  → TB6612FNG channel B, enabled by PWMB GP16
  → one 3.9 Ω series resistor in each buzzer lead
  → piezo BZ1, with 1.5 µF across it after the resistors
```

The resistor/capacitor values are the project's recorded bench configuration. The capacitor is non-polar because the differential voltage reverses. The note assumes this fitted network; tolerances, capacitor technology, piezo part number/impedance model, enclosure and actual channel-B supply voltage have not been documented as calibrated measurement inputs for this review.

The buzzer uses channel B. Its playback code does not change the motor's channel-A outputs or the shared STBY pin. The controller remains responsible for motor state and driver enable. GPIO mapping is taken from current `config.h`/`AGENTS.md`; older hardware prose contains superseded pin/allocation information.

The nominal RC time constant, ignoring the piezo and driver impedances, is `(3.9 + 3.9) Ω × 1.5 µF = 11.7 µs`, equivalent to an unloaded first-order corner of approximately **13.6 kHz**. This simple calculation does not model piezo resonance, mechanical damping, electrical loading or actual carrier attenuation.

### 7.2 Carrier and sample timing

Let `F_sys` be the system clock and `F_target = 100000 Hz`:

```text
P = ceil(F_sys / F_target)
F_sample = F_sys / P
PWM divider = 1
PWM TOP = P - 1
```

At nominal `F_sys = 125000000 Hz`:

- `P = 1250` clock ticks; TOP = 1249.
- PWM carrier and DDS update rate are **100000 samples/s**.
- One DDS sample occupies **10 µs**.
- One integer millisecond contains exactly **100 samples**.

The driver datasheet specifies a maximum PWM switching frequency of 100 kHz. The implementation operates at that stated boundary; this does not by itself characterize distortion, switching losses or performance with this capacitive piezo load. [Toshiba, TB6612FNG datasheet, 13 May 2026, operating range and control-function table](https://toshiba.semicon-storage.com/info/docget.jsp?did=10660&prodName=TB6612FNG).

### 7.3 Frequency quantization and phase

For each half-note with nominal frequency `f`:

```text
phase_step = floor(f * 2^32 * P / F_sys)
phase      = (phase + phase_step) modulo 2^32
sample     = sine_table[phase >> 24]
actual_DDS_fundamental = phase_step * F_sample / 2^32
```

The actual code reads the current phase's sample before incrementing phase. The lookup table has 256 signed Q15 entries, approximately ±32767. There is no interpolation between table entries.

At 100 ksample/s, the frequency-step resolution is approximately **0.000023283 Hz**. Because conversion truncates downward, the DDS fundamental is below or equal to the nominal frequency by less than one such step, before physical clock error. Thus an edge nominally specified as 1950 Hz can be infinitesimally below 1950 Hz digitally. Real clock tolerance and transducer behavior are much more significant than this numerical error.

The fundamental is checked against the actual sample-rate Nyquist limit. At the nominal sample rate, both used bands are far below 50 kHz. This check does not imply the whole driver/piezo system is characterized throughout that range.

Phase is retained across half-note changes and between sounding notes. During silent events the phase accumulator is **frozen**, not advanced through elapsed silence. Each whole-sequence repetition restarts at phase zero. There is no separate phase reset or zero-crossing alignment at every note onset.

### 7.4 Amplitude and bridge encoding

For sine sample `s`:

```text
m = min(abs(s), 32767)
pulse = floor(m * P / 32768)
A = P
B = P
if s >= 0: B = P - pulse
else:      A = P - pulse
compare_word = A | (B << 16)
```

Only one bridge input pulses low for a given nonzero sample polarity. A zero sample sets both compare values to `P`, holding both inputs high during the PWM cycle. With STBY enabled, the driver's truth table gives short braking for both inputs high, so this does not alternate a full-amplitude bipolar carrier during requested silence. [Toshiba control-function table](https://toshiba.semicon-storage.com/info/docget.jsp?did=10660&prodName=TB6612FNG).

At the largest table magnitude and `P = 1250`, the pulse calculation yields 1249 ticks. No software gain boost or intentional clipping is applied. All introductions and bursts use the same full-scale digital amplitude; the final part is denser, not louder by a programmed gain change.

**Configuration detail:** `BUZZER_DDS_GAIN_PERCENT` is currently 100, but the crips-5 renderer does not call the gain-adjusting `buzzer_dds_compare()` helper. It reads the shared sine table and performs the calculation above directly. Changing that macro alone does **not** adjust crips-5 amplitude.

### 7.5 DMA, completion and interruption behavior

- Two DMA channels are claimed on first use and retained for subsequent playback.
- Two statically allocated buffers each contain 256 32-bit compare words: **1024 bytes per buffer, 2048 bytes total**.
- Each buffer spans **2.56 ms** at 100 ksample/s. Chaining lets one buffer play while the interrupt handler refills the other.
- DMA uses a 10-bit read-address ring, preventing reads beyond a buffer if refill is missed.
- Both PWM compare registers are written atomically in one 32-bit operation.
- DMA IRQ 0 runs at the lowest configured IRQ priority, below the 1 kHz motor safety controller.
- If the expected next DMA channel is not busy during refill handling, the code mutes PWMB and records an underrun. This is a fault response, not successful sequence completion; brief repeated buffer content before detection is possible.
- A wholly silent terminal buffer drains after the last musical event to accommodate buffered PWM compares. Software playback status can remain active for a few additional silent milliseconds; the musical duration tables exclude that drain and main-loop cleanup latency.
- `buzzer off`, a replacement player or a higher-priority fixed tone stops the current sequence. Aborted content is not resumed.
- Starting a new crips-5 invocation replaces a currently playing chirp, including any remaining repeats, rather than mixing or queueing sounds.
- No floating-point sine computation, heap allocation, or console formatting occurs in the sample-refill IRQ.

These describe the digital implementation. Exact microphone onset, ring-down, residual switching spectrum and latency require measurement.

## 8. Acoustic interpretation and limits of “clean sine”

The firmware aims for a sinusoidal differential **average drive** during each half-note. The physical bridge still produces PWM pulses. The external circuit, piezo and enclosure determine the actual electrical and acoustic reconstruction.

Several properties prevent a claim of spectrally pure or biologically faithful output:

1. The finite Q15 table, 8-bit table index and PWM pulse quantization introduce numerical error and unwanted spectral components.
2. Abrupt note starts/stops and discrete pitch changes create transient/broadband spectral energy, even when the sustained segment is a sine.
3. Freezing phase during gaps can restart a note away from a zero crossing. There is no smoothing envelope.
4. The piezo can continue sounding after the commanded silence begins. Short commanded gaps are not guaranteed acoustically silent intervals.
5. The mechanical and electrical transfer functions can change level and ringing at each frequency. Equal digital amplitudes do not establish equal SPL between the two bands.
6. Residual carrier, driver distortion, mounting resonances and room reflections are not described by the nominal frequency limits.

**Bench-reported basis:** the owner reported pronounced output peaks near 2.0 and 4.3 kHz, with substantial reduction a few hundred hertz away, and judged the resulting composition loud and clear. Earlier 2.7–3 kHz resonance guesses were superseded by that later bench observation. The current ±50 Hz bands follow the later observation.

Earlier diagnostic comparisons reported approximately 1.13 Vrms for matched 6 kHz tones through the older sine players, and earlier sine trials reported approximately 2.8 V peak-to-peak. Those observations used different test conditions from this complete sequence. They are **not calibrated specifications for current crips-5**, are not converted here into SPL, and do not establish an acoustic exposure limit.

### Measurements still required

| Measurement | Needed record |
|---|---|
| Piezo and installation identity | Part number, mounting, enclosure, aperture, component tolerances and supply voltage |
| Sound level | Calibrated level, weighting, integration/peak method, distance, orientation and microphone/calibrator identification |
| Spectrum | Sustained bands, harmonics, transients, residual carrier where instrument bandwidth permits, and background spectrum |
| Envelope | Actual onset, decay/ring-down, audible silence and overlap between notes |
| Directivity and range | Level/spectrum around the device and at plausible bird locations |
| Operating variation | Battery/supply range, representative environmental conditions and enclosure variations |
| Exposure | Sequences per arrival and arrivals per hour/day, including unusual repeated station changes |
| Recording provenance | Unprocessed recording, gain settings, sample rate, calibration and exact firmware/configuration identity |

A recording with automatic gain control cannot, by itself, substantiate calibrated relative or absolute output level.

## 9. Biological evidence and the remaining inference gap

### 9.1 What informed the original timing

Great-tit measurements in Salis et al. give F-note duration 110 ±40 ms and D-note duration 50 ±10 ms; inter-note gaps are F–F 100 ±30 ms, F–D 90 ±40 ms and D–D 30 ±6 ms (mean ±SD, Table A1). Their two-F/six-D artificial sequence was a playback construction, not a universal formula. The methods state 0.3 s D–D spacing while Table A1 gives 0.03 s; the earlier design followed the table. The same study found mobbing in 3% of birds hearing a pure-tone control, versus 33% for complete artificial calls and 36% for natural calls. Timing alone did not reproduce normal recognition. [Salis et al., 2024](https://doi.org/10.1016/j.anbehav.2024.07.020).

**Application to crips-5:** its sounding-note durations retain that approximate temporal scale, but its shortened silences, two narrow frequency bands and equal amplitudes are engineering substitutions. Labels such as “F-like” and “D-like” indicate inspiration only; the implemented bursts lack the sustained broadband/harmonic structure of natural D notes.

### 9.2 More urgent does not universally mean shorter gaps

Kalb, Anger and Randler recorded great-tit responses to predator mounts. Sparrowhawk-directed D calls averaged 531 ±33 ms, 7.095 ±0.391 elements and 41 ±2 ms gaps; tawny-owl-directed calls averaged 419 ±24 ms, 6.063 ±0.371 elements and 34 ±1 ms gaps (mean ±SE). Higher threat increased whole-call rate to 15.03 versus 7.97 calls per individual per minute, but also lengthened gaps inside calls. [Kalb et al., 2019, results on page 2](https://d-nb.info/118629857X/34).

Crips-5's denser final part is a listening-driven progression. It is not evidence that the signal represents greater threat, nor that its current 13–23 ms inter-burst gaps match those natural measurements.

### 9.3 Order, receiver and context matter

Japanese tits in Suzuki et al. responded differently to alert and recruitment components, and reversing their order reduced the normal combined response. Natural inter-note intervals used in preparation were 50–150 ms; experimental rates of 20 or 30 calls/min were described as within natural ranges. These are species/context-specific results and playback settings, not a prescribed rate for Luftfugl. [Suzuki, Wheatcroft & Griesser, 2016](https://doi.org/10.1038/ncomms10986).

The earlier crips-4 used two-second phrase onsets. Crips-5 now has approximately one-second phrase spacing across repeated cycles. Its temporal compression therefore moves it further from that particular playback schedule, even though individual note durations remain similar. Neither study establishes the meaning of crips-5's high/low/high mapping.

### 9.4 Deterrence is not established

Recorded mobbing calls caused stress-related behavioral changes in 15 captive raptors of seven species in Consla and Mumme's study; this did not establish field departure or the efficacy of two-band piezo synthesis. [Consla & Mumme, 2012](https://doi.org/10.1111/eth.12007).

The ornithologist should distinguish attracting/recruiting other birds, vigilance, territorial approach or aggression, escape and predator displacement. “It sounds urgent to a person” cannot select among those outcomes. No species-response or intruder-deterrence experiment has been performed for this implementation in the evidence available to this note.

## 10. Current station integration and failure behavior

### 10.1 Arrival mapping

| Station | Requested sound | First-reference duration if uninterrupted |
|---|---|---:|
| 1 | None | 0 |
| 2 | 1 complete crips-5 sequence | 2.746 s |
| 3 | 2 complete crips-5 sequences | 5.755 s |
| 4 | 3 complete crips-5 sequences | 8.764 s |
| 5 | 4 complete crips-5 sequences | 11.773 s |
| 6 / EVENT_POS | None | 0 |

Durations refer to one reference composition. Each station invocation generates its own composition, so real digital durations can vary within the implementation's rules.

The controller's arrival routine brakes the motor, updates the position and transitions to idle before queuing `EV_ARRIVE`. In the main loop, `console_drain_events()` maps positions 2–5 to counts 1–4 and calls `buzzer_crips_5(count)`, which forwards to `buzzer_play_5(count)`.

```c
unsigned int bird_plays = event.kind == EV_ARRIVE
                              ? station_arrival_bird_plays(event.arg)
                              : 0u;
if (bird_plays)
  (void)buzzer_crips_5(bird_plays);
```

`EV_PASS` does not trigger a call. This code does not continuously sound merely because the mechanism remains at a station. The count mapping and dispatch are active in **both debug and release builds**. Debug command access is additional; it is no longer the only use of crips-5.

### 10.2 Priority and missed/interrupted calls

A crips-5 start can return false for invalid count, an active fixed/battery tone sequence, unavailable DMA resources or an invalid generator/clock configuration. The station dispatch currently discards that return value: there is **no retry, deferred playback or station-specific failure acknowledgement** in this path.

A fixed tone takes exclusive control and stops crips-5. The active-tone check includes tone pauses and remaining repetitions, preserving battery-tone priority. A new arrival while another chirp is active starts a replacement sequence; earlier remaining repeats are not accumulated. Queue overflow can also drop events under the existing event-ring behavior. Thus a requested station count is not a guarantee of uninterrupted acoustic delivery under all operating conditions.

There is no species detector, intruder classifier, biological urgency input, automatic SPL adjustment or ornithological approval gate in the implementation. Integration has already been made at the user's request. Any reviewer decision to restrict deployment would need an explicit operational or firmware change; this document does not impose one silently.

## 11. Verification evidence and its limits

### 11.1 Verified digital behavior

- The actual crips-5 generator was compiled as portable C and used to produce the reference table in appendix A.
- `test/test_buzzer_compact.c` compares 1000 matched crips-4/crips-5 compositions, checks sounding events and the two-stage silence reduction, and compares actual sounding PWM samples for ten complete compositions.
- The same test checks exact repetitions for counts 1, 2 and 200, invalid inputs and terminal silence. It uses `-Wall -Wextra -Werror` with address and undefined-behavior sanitizers.
- `test/test_station_crips.py` exercises the actual extracted event queue/arrival dispatch in both debug/monitor and release configurations, confirming counts 1/2/3/4 at stations 2/3/4/5, silence at 1/6, and no triggering on pass-through or unrelated events.
- The crips-5 composition/renderer source remained byte-identical when station integration was added. Only its build availability, lifecycle hooks and arrival routing changed.
- Debug and release firmware built successfully with `-Wall -Wextra` and no compiler warnings in the recorded integration build.
- The release ELF contains the crips-5 player and renderer; it is no longer identical to the earlier pre-integration release binary.

### 11.2 Verified on the target

OpenOCD identified a **4096 KiB** flash device, programmed the debug image and reported **Verified OK**. A manual `buzzer crips-5 4` hardware-console test reported active playback followed by normal off state. The subsequent controller snapshot was IDLE at position 6; the 1 kHz tick reported zero overruns and a 100 ms watchdog. The console was reset after testing.

This was a non-motion playback check. No motion commands were issued. Physical movement through all stations was **not** performed for that verification; station selection/count correctness was tested through the actual dispatch code on the host.

### 11.3 Not verified

No calibrated SPL, acoustic waveform, distortion, directivity, ringing, hearing-range exposure, bird recognition, avoidance, recruitment, habituation or deterrence result is established by those software and console tests. No ornithologist's approval is contained in the available project evidence. Those are review/measurement tasks, not claims to infer from a clean build or successful flash.

## 12. Requested ornithological assessment

### 12.1 Specify the intended use before judging the signal

Please complete or amend:

| Review input | Current information / field to complete |
|---|---|
| Operational purpose | Station indication is implemented; determine whether a deterrence objective is intended at all |
| Target and non-target species | Not specified |
| Installation location and habitat | Not specified |
| Indoor/outdoor placement and acoustic escape paths | Not specified |
| Season, breeding/nesting proximity and sensitive contexts | Not specified |
| Typical and worst-case bird distance | Not measured |
| Calibrated output level and spectrum | Not available |
| Expected station arrivals per hour/day | Not established |
| Intended maximum continuous/repeated exposure | Requires explicit decision |
| Intended response and unacceptable responses | Requires species/context-specific definition |

### 12.2 Questions to answer

1. Could the signal be interpreted as alarm, recruitment, territorial threat, distress, song/contact sound or an unfamiliar mechanical sound by relevant receivers?
2. Are the two narrow bands and equal amplitudes compatible with the intended biological interpretation, or do they remove critical information?
3. Does the high-band introduction followed by low bursts in part 2 have any plausible species-specific meaning? Is the three-part progression appropriate?
4. Are the current note durations, 13–23 ms inter-burst gaps and approximately 0.25 s long pauses acceptable, given that the later timing compression was chosen by listening rather than biological measurement?
5. Could station-triggered playback in the absence of a real threat recruit, distract or repeatedly alarm nearby birds? Could an intended intruder approach rather than withdraw?
6. Does exact repetition within an invocation create a recognition or habituation concern? What does the relevant species/context evidence predict, and what remains unknown?
7. What sound level, exposure schedule, placement, seasonal restrictions and stopping rules, if any, should govern an approved evaluation?
8. Is approval appropriate for an indoor indicator, a controlled outdoor evaluation, general deployment, or none of these? These scopes should be stated separately.

### 12.3 Suggested evaluation sequence for reviewer amendment

This is a proposed assessment structure, not an approved field protocol:

1. **Bench characterization:** record calibrated acoustic output and the electrical drive with the actual piezo/network/enclosure. Confirm the reference sequence and characterize silence/ring-down.
2. **Define endpoints:** identify target/non-target species, intended response and adverse-response indicators before exposure. Distinguish approach, scanning, calling, retreat and displacement.
3. **Design a comparison:** if biological testing is justified, the ornithologist should select appropriate controls, replication, exposure duration, observation windows and any permissions required for the chosen setting. Do not presume that a natural alarm recording is a neutral control.
4. **Assess repeated exposure:** evaluate the approved operating schedule rather than only one isolated playback. Record background conditions, station activity and interruptions.
5. **Review the results:** approve, conditionally approve, request changes or reject a defined use and version. Reassess after changes to the signal, output level, mounting or exposure schedule.

No fixed distance, SPL, sample size or safe exposure duration is invented in this note. The reviewer should set those values based on species, context and measured output.

### 12.4 Review and approval record

- **Reviewer / qualifications:** ______________________________
- **Review date:** ______________________________
- **Document revision and firmware/source identity reviewed:** ______________________________
- **Species, setting and use reviewed:** ______________________________
- **Calibrated recording/measurement references:** ______________________________
- **Decision:** ☐ Approved for stated scope ☐ Approved with conditions ☐ Further evidence required ☐ Not approved
- **Approved scope:** ☐ Bench listening ☐ Controlled biological evaluation ☐ Defined deployment
- **Output/exposure/placement conditions:** ______________________________
- **Required changes or evidence:** ______________________________
- **Stopping criteria / reassessment triggers:** ______________________________
- **Reasoned assessment and signature:** ______________________________

An approval should identify the actual tested configuration and permitted use; it should not be inferred to cover all future sounds named crips-5.

## Appendix A. Exact first generated sequence

This table was emitted from the current `compact_pattern_init()` C implementation in a fresh host process at 125 MHz. It contains **all 27 sounding notes and all 26 internal silence events**, by combining each pair of half-note events into one row. Event indices are zero-based. Time is relative to the first DDS musical sample; intervals are end-exclusive. Frequencies are nominal Hz; all times are ms.

The listed gap follows the second half. The final dash means the musical sequence ends. If another sequence is requested, **263 ms** follows that final row before this same pattern restarts. Introductory half-note frequencies are also randomized.

This is a reproducible reference, not a guarantee that a manual command entered after earlier automatic station calls will receive the first PRNG composition.

| Part | Note | Sound event indices | Onset | Half 1 ms | Half 1 Hz | Half 2 ms | Half 2 Hz | End | Following silence ms |
|---:|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | Intro 1 | 0–1 | 0 | 50 | 4344 | 50 | 4345 | 100 | 50 |
| 1 | Intro 2 | 3–4 | 150 | 50 | 4272 | 50 | 4284 | 250 | 45 |
| 1 | Burst 1 | 6–7 | 295 | 24 | 4264 | 24 | 4267 | 343 | 19 |
| 1 | Burst 2 | 9–10 | 362 | 23 | 4277 | 23 | 4277 | 408 | 17 |
| 1 | Burst 3 | 12–13 | 425 | 27 | 4279 | 27 | 4250 | 479 | 18 |
| 1 | Burst 4 | 15–16 | 497 | 26 | 4350 | 26 | 4329 | 549 | 17 |
| 1 | Burst 5 | 18–19 | 566 | 25 | 4251 | 25 | 4298 | 616 | 19 |
| 1 | Burst 6 | 21–22 | 635 | 27 | 4304 | 27 | 4305 | 689 | 283 |
| 2 | Intro 1 | 24–25 | 972 | 50 | 4317 | 50 | 4327 | 1072 | 50 |
| 2 | Intro 2 | 27–28 | 1122 | 50 | 4250 | 50 | 4250 | 1222 | 45 |
| 2 | Burst 1 | 30–31 | 1267 | 23 | 2035 | 24 | 2038 | 1314 | 19 |
| 2 | Burst 2 | 33–34 | 1333 | 25 | 2031 | 26 | 2050 | 1384 | 21 |
| 2 | Burst 3 | 36–37 | 1405 | 27 | 2050 | 27 | 2050 | 1459 | 20 |
| 2 | Burst 4 | 39–40 | 1479 | 26 | 2007 | 26 | 1977 | 1531 | 21 |
| 2 | Burst 5 | 42–43 | 1552 | 26 | 1984 | 26 | 1974 | 1604 | 22 |
| 2 | Burst 6 | 45–46 | 1626 | 27 | 2016 | 27 | 1981 | 1680 | 19 |
| 2 | Burst 7 | 48–49 | 1699 | 22 | 1957 | 23 | 1968 | 1744 | 253 |
| 3 | Intro 1 | 51–52 | 1997 | 50 | 4341 | 50 | 4339 | 2097 | 50 |
| 3 | Intro 2 | 54–55 | 2147 | 50 | 4298 | 50 | 4329 | 2247 | 45 |
| 3 | Burst 1 | 57–58 | 2292 | 20 | 4250 | 20 | 4267 | 2332 | 16 |
| 3 | Burst 2 | 60–61 | 2348 | 21 | 4312 | 22 | 4333 | 2391 | 17 |
| 3 | Burst 3 | 63–64 | 2408 | 20 | 4339 | 21 | 4296 | 2449 | 15 |
| 3 | Burst 4 | 66–67 | 2464 | 20 | 4313 | 20 | 4331 | 2504 | 16 |
| 3 | Burst 5 | 69–70 | 2520 | 22 | 4350 | 23 | 4340 | 2565 | 14 |
| 3 | Burst 6 | 72–73 | 2579 | 24 | 4340 | 25 | 4350 | 2628 | 15 |
| 3 | Burst 7 | 75–76 | 2643 | 22 | 4290 | 23 | 4295 | 2688 | 16 |
| 3 | Burst 8 | 78–79 | 2704 | 21 | 4269 | 21 | 4263 | 2746 | — |
