# Task — Absolute Positioning Commands: `goto` and `angle`

Add two commands to the debug console for moving to an absolute position, one
in ADC counts and one in degrees.

Do not use `sed -i` by line number or `perl -0pi -e` on source files. Use
`apply_patch` with context.

---

## Why this is safe now, when `goto adc` was not

The old `goto adc` was removed because a stray keystroke committed a 1750-count
full-arc move on a mechanism with no physical end-stops. These commands restore
the capability without that failure mode:

- The target is **decomposed into a sequence of bounded jogs**, each no larger
  than `JOG_MAX_COUNTS`, each individually validated against the safe range.
- The sequence **runs one jog at a time from the main loop**, with the next
  only issued after the previous completes. Any fault, timeout or `stop`
  cancels the remainder.
- **`.` and `stop` abort the whole sequence**, not just the jog in flight.
- The operator sees each step reported as it happens.

So a mistyped target moves the mechanism in visible, interruptible increments
rather than one uninterruptible sweep. That is the difference that matters.

---

## `goto <adc>`

Move to an absolute ADC value.

```
> goto 1260
 goto 1260     moving, 631 counts back in 2 steps
 goto 1260     step 1 of 2, now at 1391
 goto 1260     step 2 of 2, now at 1262
 goto 1260     done, now at 1262, 2 counts from target
```

Behaviour:

- Compute the delta from the current filtered reading.
- Reject if the target is outside `ADC_SAFE_MIN..ADC_SAFE_MAX`:
  `rejected: 3200 is outside the safe range 272 to 2915`
- Reject if the delta is below `JOG_MIN_COUNTS`:
  `rejected: already within 10 counts of 1260`
- Split into ceil(|delta| / `JOG_MAX_COUNTS`) steps, the last one carrying the
  remainder.
- Report the plan before starting, then each step as it completes.
- After the final step, recompute the error and report it. Do not iterate to
  close it — one pass, then report. The operator can issue another `goto` if
  the residual matters.

## `angle <degrees>`

The same, expressed in degrees.

```
> angle 60
 angle 60      = ADC 682, moving 1209 counts back in 3 steps
 angle 60      step 1 of 3, now at 1391
 angle 60      step 2 of 3, now at 891
 angle 60      step 3 of 3, now at 684
 angle 60      done, now at 684, 60.1 degrees
```

The potentiometer is a **360° single-turn type**, wired 0 V to 3.3 V across its
full rotation. So:

```c
#define ADC_PER_360_DEG   4095u
/* adc = degrees * 4095 / 360;  degrees = adc * 360 / 4095 */
```

Use integer arithmetic. Report degrees to one decimal place by computing
tenths: `tenths = adc * 3600 / 4095`.

Accept 0–360, one decimal place accepted on input (`angle 60.5`). Reject
outside that range with
`rejected: 400 is outside 0 to 360 degrees`.

If the resulting ADC falls outside the safe range, reject with both figures:
`rejected: 300 degrees is ADC 3413, outside the safe range 272 to 2915`.

## `angle` with no argument

Report the current position in degrees:

```
> angle
 angle         now at 166.2 degrees, ADC 1891
```

## `goto` with no argument

Report the current ADC — same as `adc`, kept for symmetry.

---

## Add degrees to the status region

The status block gains one field, since the operator is evidently thinking in
degrees:

```
  STATE   IDLE          POSITION  between 3 and 4      ADC  1891
  TARGET  --            ERROR     --                   STEP 100 counts
  FAULTS  0             DUTY      0                    DIR  stopped
  ANGLE   166.2 deg
```

And the stations line shows both:

```
  STATIONS   1: 372 (32.7°)   2: 738 (64.9°)   3: 1309 (115.1°) ...
```

---

## help entries

```
> help goto
  goto — move to an exact ADC reading

  Examples
    goto 1260    move until the reading is 1260
    goto 2047    move to the middle of the travel

  Limits
    target must be between 272 and 2915
    large moves are split into steps of 500 counts or less
    each step is checked before it starts

  Notes
    One count is about 0.09 degrees.
    Type "." at any time to stop.
```

```
> help angle
  angle — move to an exact angle

  Examples
    angle 60     move to 60 degrees
    angle 166.2  move to 166.2 degrees
    angle        show the current angle without moving

  Limits
    0 to 360 degrees
    the angle must fall inside the safe range

  Notes
    The sensor turns a full 360 degrees across its range.
    One degree is about 11 counts.
    Type "." at any time to stop.
```

---

## Sequencing

Hold the sequence state in the console module, not the controller:

```c
static bool     seq_active;
static uint16_t seq_target_adc;
static uint8_t  seq_step, seq_total;
static char     seq_label[16];    /* "goto 1260" or "angle 60" */
```

The main loop advances it: when `seq_active` and the controller is `ST_IDLE`
and no jog is pending, issue the next jog. On fault, timeout or stop, clear
`seq_active` and report `cancelled at step n of m`.

**Do not add a new controller request kind.** Each step is an ordinary
`controller_request_jog()` call with all its existing validation. That is what
keeps this safe.

---

## Verification

Report actual console output for:

1. `goto 1260` from a reading more than 500 counts away — plan, each step, and
   the final error
2. `goto 3200` — `rejected: 3200 is outside the safe range 272 to 2915`
3. `angle 60` — the ADC conversion, the steps, and the final angle
4. `angle 400` — `rejected: 400 is outside 0 to 360 degrees`
5. `angle` alone — current angle and ADC, no motion
6. `.` during a multi-step `goto` — sequence cancelled, remaining steps not
   issued
7. A fault mid-sequence — sequence cancelled, reported

Point 6 is the safety property. Demonstrate it.

Do not run the motor yourself; report what the code will do and let me test it.
