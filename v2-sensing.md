# Version 2 — Potentiometer Position Sensing

Authoritative specification for replacing the reed-switch resistor ladder with
a single potentiometer. **This document supersedes the named sections below.**
Everything not named here is unchanged.

| Superseded | In | Replaced by |
|---|---|---|
| §2.6 Position Encoder, §2.7 Sense-line RC | `agent.md` | §2, §3 here |
| §5 Boot behaviour | `agent.md` | §6 here |
| §6.1, §6.3, §6.5, §6.6 | `agent.md` | §7 here |
| §5 Encoder module | `function-description.md` | §8 here |
| §6.3, §6.5 recovery | `function-description.md` | §7.4 here |
| §5 ladder, BOM items 4–6 | `hardware.md` | §4, §5 here |
| Menu 2, menu 9 band items | `debug-functions.md` | §10 here |

---

## 1. What Changes, and Why It Matters

The reed ladder was a **discrete** sensor: five valid readings and one invalid
"between reeds" region covering everything else. A potentiometer is an
**absolute continuous** sensor. Every reading corresponds to a real, known
angle.

Four consequences follow, and they are the whole substance of this revision:

1. **There is no unknown state.** The open band (3456–4095) no longer means
   "no reed closed" — it means "the wiper is near the top of its travel". Any
   code treating a high reading as invalid is now wrong.

2. **Boot homing is no longer required.** Position is absolute and available
   within one filter window of power-up. `agent.md` §5 step 6 — creep reverse
   until position 1 — is deleted. It was the single most dangerous operation in
   version 1, since it drove blindly toward a limit that had no physical stop.

3. **Over-travel becomes detectable.** In version 1, driving past reed 5 read
   as "unknown" and was indistinguishable from any other lost-position case.
   Now, exceeding 2815 reads as a larger number. The firmware can brake on
   over-travel rather than merely trying to avoid it.

4. **`recover_direction()` stops being a safety-critical special case.** Its
   version 1 job was guessing which way to creep when position was unknown.
   With a continuous sensor the direction is the sign of `target - current`.
   The limit-aware branches are deleted because the situation they guarded
   against — not knowing which side of a limit you are on — cannot occur.

Net effect: the mechanism is meaningfully safer, and three of the trickiest
pieces of version 1 logic disappear.

---

## 2. Sensing

| Signal | RP2040 pin | Board label |
|--------|-----------|-------------|
| WIPER | GP26 (ADC0) | `A0` |

Potentiometer wired as a voltage divider: one end to **+3.3 V**, other end to
**AG**, wiper to GP26. Same three conductors as version 1 — the harness does
not change.

### 2.1 Position Values — unchanged

The five positions keep their version 1 ADC values:

| Position | ADC | Angle from position 1 |
|----------|-----|----------------------|
| 1 | 372 | 0° |
| 2 | 738 | 27° |
| 3 | 1309 | 69° |
| 4 | 2047 | 123° |
| 5 | 2815 | 180° |

Span 2443 counts over 180°, so **13.6 counts per degree**.

Note these are not evenly spaced — they are inherited from the resistor ladder,
where spacing came from resistor values rather than geometry. The gaps run 366,
571, 738 and 768 counts. This is fine, but it means the mechanism's five
stations are *not* at equal angular intervals: 27°, 42°, 54° and 57° apart. If
the physical stations *are* evenly spaced, these values are wrong and must be
re-measured — see §11.

### 2.2 Position Windows — new

A position is "reached" when the filtered reading is within `POS_WINDOW` counts
of its nominal value.

```c
#define POS_WINDOW  40      /* counts, ~2.9 degrees */
```

40 counts is chosen against the smallest gap (366 counts, positions 1→2): the
window is under a quarter of the half-gap, so adjacent windows cannot overlap
even with substantial pot non-linearity. It is also far above the noise floor
of a filtered RP2040 ADC reading, which is a few counts.

`classify()` is replaced by a window match:

```c
static position_t position_at(uint16_t v)
{
    for (position_t p = POS_MIN; p <= POS_MAX; p++) {
        uint16_t nominal = position_adc[p - 1];
        uint16_t delta = v > nominal ? v - nominal : nominal - v;
        if (delta <= CFG_POS_WINDOW) return p;
    }
    return POS_BETWEEN;     /* a known angle, not an error */
}
```

### 2.3 `POS_BETWEEN` replaces `POS_UNKNOWN`

`POS_UNKNOWN` (0) is retained for exactly one case: before the filter has
primed at boot, and when the reading is outside the safe range (§2.4).

`POS_BETWEEN` is new and means "at a known angle that is not one of the five
stations". It is a normal, non-error condition.

```c
#define POS_UNKNOWN   0     /* filter not primed, or outside safe range */
#define POS_BETWEEN   6     /* valid angle, between stations */
```

Both report `POS:?` on the wire — `agent.md` §7 is unchanged. Internally they
are different: `POS_BETWEEN` permits movement, `POS_UNKNOWN` does not.

### 2.4 Safe Range and Over-Travel — new

```c
#define ADC_SAFE_MIN   272      /* position 1 nominal minus 100 counts */
#define ADC_SAFE_MAX   2915     /* position 5 nominal plus 100 counts */
```

100 counts is about 7.4°, chosen to allow the coast overshoot measured in
bring-up without tripping, while still braking well before the harness reaches
its limit.

**A filtered reading outside this range is an over-travel event.** The
controller must, in the same tick:

1. `motor_brake()`
2. `motor_disable()`
3. emit `ERR: overtravel`
4. enter `ST_FAULT`

No creep, no recovery attempt, no automatic homing. Over-travel means the
mechanism is already outside its designed arc and the harness is under strain;
the correct action is to stop driving and let a human look at it.

This is a new error string and is added to `agent.md` §8.

---

## 3. Filtering

The 5-deep rolling average is retained. The 12 ms debounce is **retained but
repurposed**: there is no contact bounce to reject, but the confirmation window
still guards against a transient noise spike being read as an arrival.

The 100 nF capacitor to AG is retained. With a 10 kΩ pot the maximum source
impedance is 2.5 kΩ at mid-travel, so the filter time constant is at most
0.25 ms — four times faster than the version 1 worst case, which had 10 kΩ.

**Consequence:** the traverse speed limit that version 1 imposed to make reeds
detectable is gone. A potentiometer cannot be "missed" the way a reed could.
`DUTY_NORMAL` may be raised during bring-up if desired; it is no longer bounded
by sensing.

---

## 4. Hardware — supersedes `hardware.md` §5 and BOM items 4–6

| # | Item | Spec | Qty |
|---|------|------|-----|
| 4 | Potentiometer | **10 kΩ, linear taper (Type B), single-turn** | 1 |
| 5 | — | *(pull-up resistor no longer required)* | — |
| 6 | — | *(ladder resistors no longer required)* | — |
| 7 | Filter capacitor | 100 nF ceramic X7R, wiper to AG | 1 |

**Linear taper is mandatory.** A logarithmic or audio-taper pot will produce
readings that do not map linearly to angle, and every position value in §2.1
will be wrong.

10 kΩ is chosen as a compromise: low enough that source impedance stays
comfortable for the RP2040 ADC, high enough that the divider draws only 330 µA
from the 3.3 V rail.

```
        +3.3 V
          │
          │
        ┌─┴─┐
        │   │
        │   │◄──────── wiper ──────────► GP26 / A0
        │   │                    │
        └─┬─┘                  ──┴──  100 nF
          │                    ──┬──
          │                      │
          └──────────────────────┴────► AG
```

### 4.1 New Mechanical Hazard

**The potentiometer has its own internal end-stops.** A single-turn pot rotates
about 270° and stops hard at each end. This introduces a failure mode that did
not exist with reed switches:

- If the mechanism over-travels far enough, it drives the pot against its
  internal stop.
- The pot then either breaks, or the coupling slips — and a slipped coupling
  means every position reading is silently wrong from that moment on.

Mitigations, all required:

1. **Mount so the 180° working arc sits in the middle of the pot's travel**,
   with at least 30° of pot rotation spare at each end.
2. **Use a coupling that slips before the pot breaks**, not one that transmits
   full motor torque. A slipped coupling is recoverable; a snapped pot shaft
   inside a mechanism is not.
3. **Treat any position reading that disagrees with expected travel as a
   suspected slip** — see the stall and direction checks in §7.5.

A slipped coupling is now the most likely silent failure in the system. It is
worth a physical index mark on the pot shaft and its mount, so a slip is
visible at a glance.

---

## 5. Wiring — supersedes `hardware.md` §4.3

| Wire | Function |
|------|----------|
| 1 | +3.3 V → pot end A |
| 2 | WIPER → GP26 (`A0`) |
| 3 | AG → pot end B |

If increasing position number reads as *decreasing* ADC, swap wires 1 and 3
rather than inverting anything in firmware.

---

## 6. Boot — supersedes `agent.md` §5

```
1. Configure GPIO, PWM, ADC, UART0. STBY low.
2. Print banner.
3. Set AIN1 = AIN2 = 0, raise STBY.
4. Fill the 5-deep sample buffer (5 ms), then read.
5. If outside the safe range  -> ERR: overtravel, STBY low, ST_FAULT.
6. If within POS_WINDOW of a station -> IDLE at that position, brake, ARR:N.
7. Otherwise -> IDLE at POS_BETWEEN, brake, report POS:?.
```

**Step 7 does not move the motor.** This is the key difference from version 1,
where an unknown position triggered a blind reverse creep. The position is
known; there is simply no reason to move until commanded.

`TIMEOUT_HOME_MS` is retained but now only bounds an explicit `home` command.

---

## 7. Control — supersedes `agent.md` §6.1, §6.3, §6.5, §6.6

### 7.1 Movement is error-driven

```c
static int16_t position_error(uint16_t target_adc, uint16_t current_adc)
{
    return (int16_t)target_adc - (int16_t)current_adc;
}
```

Direction is the sign of the error. Arrival is `|error| <= POS_WINDOW`.

### 7.2 Speed from distance

Distance to target is now known continuously, so speed follows it directly
rather than counting stations:

```c
static uint8_t speed_for_error(int16_t err, position_t tgt)
{
    uint16_t mag = err < 0 ? (uint16_t)-err : (uint16_t)err;
    if (tgt == POS_MIN || tgt == POS_MAX) {
        if (mag <= CFG_APPROACH_COUNTS) return CFG_DUTY_CREEP;
    } else if (mag <= CFG_APPROACH_COUNTS) {
        return CFG_DUTY_APPROACH;
    }
    return CFG_DUTY_NORMAL;
}

#define APPROACH_COUNTS  200   /* ~15 degrees before target */
```

Limits still get creep speed for their final approach. This is a smoother and
more predictable behaviour than the version 1 step-counting scheme.

### 7.3 Arrival

The instant-versus-confirmed split from version 1 §6.4 is **retained** and
still matters. Brake on the first in-window sample when the target is 1 or 5;
confirm before emitting `ARR:N` in all cases.

Braking early costs nothing. With a continuous sensor you now also get to see
how far past the window the mechanism coasted, which is exactly what
`dbg_cal_overshoot` measures.

### 7.4 Recovery — simplified

`ST_RECOVER` and `recover_direction()` are **deleted**.

Their purpose was to guess a direction when position was unknown. With a
continuous absolute sensor there is nothing to guess. A `move` issued while at
`POS_BETWEEN` simply drives toward the target, direction given by the sign of
the error.

`TIMEOUT_RECOVER_MS` is deleted.

> This is the largest simplification in version 2. The most error-prone
> function in version 1 is gone, not fixed — the condition it existed to handle
> can no longer arise.

### 7.5 Stall and direction checks — new

Continuous feedback makes two new fault detections possible. Both are required,
and they partly compensate for the TB6612FNG having no fault output.

**Stall.** While driving above `DUTY_MIN`, if the filtered reading changes by
less than `STALL_DELTA` counts over `STALL_WINDOW_MS`, the mechanism is not
moving.

```c
#define STALL_DELTA       8      /* counts */
#define STALL_WINDOW_MS   300
```

Action: brake, `ERR: stall`, `ST_FAULT`.

**Wrong direction.** While driving, if the reading moves *away* from the target
by more than `REVERSE_DELTA` counts, the motor leads are swapped, the pot is
wired backwards, or the coupling has slipped.

```c
#define REVERSE_DELTA     30     /* counts */
```

Action: brake, `ERR: direction`, `ST_FAULT`.

Both are new error strings, added to `agent.md` §8. The direction check in
particular is worth having on the very first powered test — it catches reversed
motor leads in under 50 ms, before the mechanism can travel far.

---

## 8. Encoder Module — supersedes `function-description.md` §5

```c
void       encoder_init(void);
void       encoder_tick(void);
uint16_t   encoder_raw(void);
uint16_t   encoder_average(void);
position_t encoder_instant(void);      /* window match, no debounce */
position_t encoder_confirmed(void);    /* held POS_WINDOW for DEBOUNCE_MS */
bool       encoder_take_change(position_t *out);
bool       encoder_in_safe_range(void);          /* new */
int16_t    encoder_error_to(position_t target);  /* new, counts */
uint16_t   encoder_nominal(position_t p);        /* new, table lookup */
```

`classify()` is replaced by `position_at()` per §2.2. The rolling average,
debounce timing and change flag are unchanged.

`position_adc[]` lives in `config.h` and is overridable at runtime through the
existing `cfg_t` mechanism, replacing the five `BAND_*_MAX` entries.

---

## 9. Configuration — replaces the band constants

Remove: `BAND_P1_MAX` … `BAND_P5_MAX`, `TIMEOUT_RECOVER_MS`.

Add:

```c
#define POS_1_ADC          372
#define POS_2_ADC          738
#define POS_3_ADC          1309
#define POS_4_ADC          2047
#define POS_5_ADC          2815
#define POS_WINDOW         40
#define APPROACH_COUNTS    200
#define ADC_SAFE_MIN       272
#define ADC_SAFE_MAX       2915
#define STALL_DELTA        8
#define STALL_WINDOW_MS    300
#define REVERSE_DELTA      30
```

All are overridable through `cfg_t` under `LUFTFUGL_DEBUG`, with validation:
the five position values must be strictly ascending, and `POS_WINDOW` must be
less than a quarter of the smallest gap between adjacent positions.

---

## 10. Debug Monitor — supersedes the band items

**Menu 2** — `b` "band table" becomes "position table", listing nominal, window
and measured error for each station. `e` "band margin" becomes "position error",
reporting counts and degrees from the nearest station.

**Menu 9** — `s` "sweep boundaries" becomes "sweep windows", injecting the full
0–4095 range and reporting where each position window starts and ends, plus the
safe-range boundaries.

`l` "drift past limit" is **retained and now tests the over-travel fault**
rather than recovery direction. Inject a value above `ADC_SAFE_MAX` and confirm
the controller brakes, disables and faults within one tick.

**Menu 4** — `dbg_cal_positions` changes meaning and becomes considerably more
useful. Instead of recording band edges, it records the actual ADC value at each
physical station, so the five constants in §9 can be set from the mechanism
rather than inherited from the old resistor values. **Run this first during
bring-up.**

New in menu 8: `s` reports live stall and direction check status — current
delta, window remaining, and whether either check is armed.

---

## 11. Bring-Up — supersedes `agent.md` §13

The order changes because the pot is absolute and must be calibrated to the
mechanism before anything else is meaningful.

1. **Mount and index-mark the pot.** Confirm the 180° arc sits mid-travel with
   30° spare each end.
2. **Uncoupled, no motor power.** Rotate by hand through the full arc, watching
   menu 2 `m`. The reading must change smoothly and monotonically with no jumps
   or dropouts. A jumpy trace means a worn or dirty pot — replace it before
   going further.
3. **Coupled, no motor power.** Run menu 4 `p` at each of the five physical
   stations and record actual values. **If they differ materially from §2.1,
   the §2.1 values are wrong and the measured ones are right.** Update
   `config.h`.
4. **Verify window sizing** against the measured gaps: `POS_WINDOW` must stay
   below a quarter of the smallest half-gap.
5. **Motor powered, uncoupled.** Confirm the direction check fires when leads
   are deliberately reversed. This is a cheap test that pays for itself.
6. **Coupled, single steps from position 3**, then limits, then full travel.

Step 3 is the one that matters most. The §2.1 values are inherited from a
resistor ladder and there is no reason the mechanism's stations should land on
them. Expect to replace all five.

---

## 12. Protocol

`agent.md` §7 is unchanged. Three error strings are added to §8:

| String | Cause |
|--------|-------|
| `ERR: overtravel` | Reading outside the safe range |
| `ERR: stall` | No movement while driving |
| `ERR: direction` | Movement away from target |

`ERR: position unknown` is retained for the pre-primed and out-of-range cases.
`POS:?` now covers both `POS_UNKNOWN` and `POS_BETWEEN`.
