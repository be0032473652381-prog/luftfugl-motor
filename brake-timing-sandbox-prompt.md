# Task — Sandbox: Where Does the Brake Actually Fire

Continue on **`sandbox/debounce-drift`** — do not create a new branch. The
trace tool already built there is exactly what this step needs.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## Correction to the previous conclusion

`DUTY_APPROACH` and `DUTY_MIN` are both 25 — the same value. There is no
room to lower approach speed further without risking stall below the
measured stiction threshold. The earlier recommendation to reduce
`DUTY_APPROACH` was wrong and should not be implemented.

What the trace did establish, solidly: braking itself is fast. Once `STP`
appears, the mechanism travels only about 1 count in the following tick —
this is not a slow-brake problem.

What the trace did **not** establish: where, relative to the target and the
active `POS_WINDOW`, the brake command was actually first issued. That trace
had no target ADC or window value attached to it, so the exact relationship
between "window entered" and "brake fired" is still unknown.

---

## Step 1 — annotate, don't guess

Extend the `trace` command's output to include, for the move it is reporting:

- target ADC for that move
- `POS_WINDOW` value in effect at the time
- the tick and ADC at which `duty` first dropped to the approach value
  (should correspond to crossing `APPROACH_COUNTS` from target)
- the tick and ADC at which `STP`/`duty=0` was first commanded
- the distance from target at that STP-commanded tick, i.e. how far inside
  or outside the window the brake actually fired

This requires no new measurement, only reporting values the firmware already
has at the moment it emits each trace line. No behaviour changes.

---

## Step 2 — run and report, annotated

Repeat the same two moves as before (`pos 3` approaching from above,
`pos 5` approaching from below), with the annotated trace, at the current
`POS_WINDOW`.

Report explicitly: **does STP fire at the moment the window is first
entered, or later, after the mechanism has already travelled partway through
it?**

- If STP fires essentially at window entry (within a tick or two), then the
  undershoot is explained by real distance covered during normal braking
  response — which the trace already shows is on the order of 30+ counts
  even though the mechanism stops within ~1 tick of STP being commanded, and
  the fix is a **measured** brake-lead: trigger STP earlier by
  approach-speed × response-ticks, using the actual measured approach speed
  from this trace (~0.6–0.7 counts/ms), not a guessed constant.
- If STP fires noticeably later — well after window entry, with the
  mechanism already deep inside the window before the brake condition is
  even checked — then something in the state machine is delaying the check
  itself, and that delay is the thing to find, not a speed or duty problem.

Do not implement a fix until this distinction is confirmed by the annotated
trace. It changes what the fix should be.

---

## Constraints, unchanged

- Build to `build-sandbox`. Do not touch `build/luftfugl.elf`.
- Do not flash automatically. Report when ready; I flash and test by hand.
- Report the annotated trace verbatim, not a summary.
- Do not adjust `DUTY_APPROACH`, `DUTY_MIN`, `POS_WINDOW`, or any other
  motion constant in this task. Measurement only.
