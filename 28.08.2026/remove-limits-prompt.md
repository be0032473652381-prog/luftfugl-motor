# Task — Remove All Motion Limits and Guards

The bench has changed. This is a micro motor on a simulation rig with a 360°
continuous potentiometer. There is no wire harness, no physical end-stop to
protect, and no travel that can damage anything.

Every guard written for the previous mechanism is now obstruction rather than
protection, and the direction check in particular blocks all motion.

Remove them.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## Remove entirely

**The direction check.** Delete `EV_FAULT_DIRECTION`, `REVERSE_DELTA`,
`best_error_magnitude`, `direction_check_ms` and the comparison block. This is
the defect that has blocked every move.

**The stall check.** Delete `EV_FAULT_STALL`, `STALL_DELTA`,
`STALL_WINDOW_MS`, `stall_reference` and `stall_check_ms`. A stalled micro
motor on a simulation rig is not a hazard.

**The over-travel fault.** Delete `EV_FAULT_OVERTRAVEL`, `ADC_SAFE_MIN`,
`ADC_SAFE_MAX`, `encoder_in_safe_range()` and every call to it. The full ADC
range 0–4095 is now valid travel.

**Move timeouts.** Delete `TIMEOUT_STEP_MS`, `TIMEOUT_HOME_MS` and the
deadline machinery that faults on expiry. A move that does not complete simply
does not complete.

**End-stop rejection.** `move`, `pos`, `jog` and `goto` no longer refuse
anything on range grounds. `MOVE_ENDSTOP`, `JOG_ENDSTOP` and their messages go.

**Jog bounds.** `JOG_MIN_COUNTS` and `JOG_MAX_COUNTS` go. `jog` accepts any
signed value from −4095 to +4095 in a single command, and no longer decomposes
into steps.

**`ST_FAULT` in its entirety**, along with `clearfault`, if nothing can now
raise a fault. If some path still can, say which and why before keeping it.

---

## Keep

- Everything that reports: `adc`, `angle`, `status`, `stations`, `limits`
- The five station values, `sel`, `save`, `export` — calibration is the point
  of the exercise
- Closed-loop `move` and `pos`, still using the position window to decide
  arrival
- `stop`, which must always work
- The 1 kHz control tick and the watchdog

`limits` now reports the full 0–4095 range and states that no motion limits are
enforced. It should not silently look the same as before.

---

## Wrap-around

The potentiometer turns continuously through 360°, so the ADC wraps 4095 → 0.
Do not attempt wrap-aware shortest-path movement — it adds a class of bug for
no benefit on a simulation rig. Movement is linear in ADC value: a target below
the current reading moves down, above moves up, and the mechanism is free to
cross the wrap point under manual jog if the operator drives it there.

Say clearly in `limits` that movement is linear, not wrap-aware.

---

## Documentation

`v2-sensing.md` §2.4, §3 and §7.5 describe the safe range, the no-end-stop
constraint and the stall and direction checks. **Do not edit that document** —
report which sections are now obsolete and I will amend them.

Record in the commit message that this is a deliberate removal for a simulation
rig with a micro motor and a continuous 360° sensor, and that the guards must
be restored if the firmware is ever used on a mechanism with real travel
limits.

---

## Verification

Report actual console output for:

1. `pos 5` from well below — completes and reports the arrival reading
2. `pos 1` from well above — completes
3. `jog +2000` — accepted in one command
4. `goto 4000` and `goto 100` — both accepted
5. `status` — no fault state present
6. `limits` — shows 0–4095 and states no limits are enforced
7. Five consecutive `pos` commands with no fault and no clearing

Item 7 is the point of the exercise. Motion has not worked end to end once.

Report both build sizes. The removals should make it noticeably smaller.
