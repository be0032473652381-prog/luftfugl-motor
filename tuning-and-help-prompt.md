# Task — Bake In the Tuned Values, and Complete the Help

Two changes. The first is compiled constants, the second is documentation the
firmware generates about itself.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## Part 1 — Tuned motion constants

The mechanism overshoots every station: arrivals land 40 to 58 counts out, and
a `pos 3` from 1445 travelled past the target to 446 — nearly 1000 counts of
overshoot — then oscillated.

`DUTY_NORMAL` 60 is too fast for the loop to arrest inside a 60-count window,
and `APPROACH_COUNTS` 100 starts decelerating far too late.

Set these in `config.h` as the compiled defaults:

```c
#define DUTY_NORMAL      30
#define DUTY_APPROACH    20
#define DUTY_CREEP       15
#define DUTY_MIN         15
#define APPROACH_COUNTS  300
#define POS_WINDOW       60
```

`APPROACH_COUNTS` at 300 is nearly a full station gap of 325, so almost the
whole of any single-station move runs at approach speed. That is intended: the
mechanism is fast and the window is narrow.

**`DUTY_MIN` at 15 may be below the motor's stiction threshold** — an earlier
`findmin` measured 25. If the mechanism fails to move at all, report it and I
will raise these rather than you guessing. Do not silently adjust them.

Update the `cfg` defaults so `reset` restores these, and confirm the ordering
validation `DUTY_MIN ≤ DUTY_CREEP ≤ DUTY_APPROACH ≤ DUTY_NORMAL` still passes.

### Also fix the `cfg` rejection message

```
rejected: unknown key or value violates configurati
```

It is truncated and says nothing useful. Replace with a message naming the
actual problem:

```
> cfg DUTY_APPROACH 20
 rejected: DUTY_APPROACH 20 is below DUTY_CREEP 25
           set DUTY_CREEP first, or use a value of 25 or more

> cfg FOO 10
 rejected: no setting called "FOO"
           type "cfg" to list them
```

Each rejection names the constraint that failed and what to do about it.

---

## Part 2 — Complete the help

`help` does not list every command, and `cfg` does not list every setting. The
operator cannot discover what exists.

### Every command must appear

Audit the command table and ensure **every** command that exists appears in
`help`, with a working example. If a command exists that is not in help, add
it. If help lists one that does not exist, remove it and say which.

Report the total count.

### Every setting must appear

`cfg` with no argument lists **all** runtime-settable constants, with current
value, compiled default, units, and the constraint on each:

```
> cfg
 SETTINGS                    now   default   limits
   DUTY_NORMAL                30        30   15..255, >= DUTY_APPROACH
   DUTY_APPROACH              20        20   15..255, between CREEP and NORMAL
   DUTY_CREEP                 15        15   15..255, between MIN and APPROACH
   DUTY_MIN                   15        15   15..255, <= DUTY_CREEP
   APPROACH_COUNTS           300       300   10..1000 counts
   POS_WINDOW                 60        60   10..81 counts, < quarter gap
   DEBOUNCE_MS                12        12   1..100 ms
   BRAKE_HOLD_MS             100       100   0..1000 ms
   POS_1_ADC                 200       200   0..4095, must stay ascending
   POS_2_ADC                 525       525   0..4095, must stay ascending
   POS_3_ADC                 850       850   0..4095, must stay ascending
   POS_4_ADC                1175      1175   0..4095, must stay ascending
   POS_5_ADC                1500      1500   0..4095, must stay ascending
 "cfg DUTY_NORMAL 40" to change one, "cfg reset" to restore defaults
```

Values that differ from their default are marked, so the operator can see at a
glance what has been changed in RAM.

`help cfg` explains that changes are RAM-only and lost on reset, and that
`export` prints the lines for `config.h`.

### `help <setting>` too

```
> help POS_WINDOW
  POS_WINDOW — how close counts as "arrived"

  now 60 counts, about 5.3 degrees either side of a station

  Examples
    cfg POS_WINDOW 40      tighter, needs more accurate stopping
    cfg POS_WINDOW 80      looser, arrives sooner but less precisely

  Limits
    10 to 81 counts. Must stay below a quarter of the smallest gap
    between stations, which is currently 325 counts.

  Notes
    If the mechanism overshoots and oscillates, widening this is a
    workaround. Reducing DUTY_APPROACH is usually the better fix.
```

Limits and current values **read from the live constants**, so the help cannot
go stale.

---

## Verification

Report actual console output for:

1. `cfg` — the full table, every setting, current and default
2. `cfg DUTY_APPROACH 10` — a rejection naming the constraint
3. `cfg FOO 1` — a rejection naming the unknown key
4. `help` — the count of commands listed, and confirmation it matches the
   number that exist
5. `help POS_WINDOW` — detail with live values
6. `pos 3` from station 5 — whether it now settles rather than overshooting

Item 6 is the point of Part 1. Report the arrival error in counts.

If the mechanism does not move at all with `DUTY_MIN` 15, say so plainly rather
than adjusting the value yourself.
