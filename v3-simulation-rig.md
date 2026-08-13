# Version 3 — Simulation Rig, No Motion Limits

Authoritative amendment. The mechanism has changed and most of the safety
architecture written for version 2 no longer applies.

**This document supersedes the sections named below.** Everything not named
here is unchanged.

| Superseded | In | Replaced by |
|---|---|---|
| §2.4 Safe range and over-travel | `v2-sensing.md` | §3 here |
| §3 Mechanical constraints, §3.1 | `v2-sensing.md` | §2 here |
| §6 Boot | `v2-sensing.md` | §6 here |
| §7.2 Speed selection | `v2-sensing.md` | §7 here |
| §7.5 Stall and direction checks | `v2-sensing.md` | §4 here |
| §2.1 Position values, §11 Bring-up | `v2-sensing.md` | §5, §9 here |
| §3 Mechanical constraints | `agent.md` | §2 here |
| §6.2, §6.9 Limits and timeouts | `agent.md` | §4 here |
| §9 Invariants 3, 4, 6 | `function-description.md` | §4 here |
| §3.1 Consequences of no end-stops | `debug-functions.md` | §2 here |
| §13.1 Fault clear, §14.4 Simulation inhibit | `debug-functions.md` | §8 here |

---

## 1. What changed

The hardware is now a **micro motor on a simulation rig** driving a **360°
continuous potentiometer**. There is no wire harness, no physical end-stop, and
no travel that can damage anything.

Version 2 was built around a mechanism where over-travel tore a wire harness.
Every guard — the safe range, the stall check, the direction check, the move
timeouts, the end-stop rejection — existed to protect that harness. None of
them protects anything now, and collectively they blocked all motion during
bring-up.

This revision removes them. The firmware is a development and debugging tool
first.

> **If this firmware is ever used on a mechanism with real travel limits, the
> version 2 guards must be restored before it is powered.** That is the single
> most important sentence in this document.

## 2. Mechanical constraints — removed

There are none.

- No physical end-stops, and none needed.
- The potentiometer turns continuously through 360°. The ADC wraps 4095 → 0.
- Any position in 0–4095 is valid travel.
- The motor may be driven in either direction from any position, indefinitely.

Version 2's rule that positions 1 and 5 are firmware-enforced limits is
deleted. So are the consequences that followed from it: creep-only approaches
to the end stations, braking on first detection at a limit, and the
limit-aware recovery direction.

## 3. Safe range — removed

`ADC_SAFE_MIN`, `ADC_SAFE_MAX`, `encoder_in_safe_range()` and
`ERR: overtravel` are deleted.

The full 0–4095 range is valid. No command is refused on range grounds.

`limits` still exists as a reporting command, and must state plainly that no
motion limits are enforced. It should not read as though it is describing
restrictions.

## 4. Fault detection — removed

Deleted entirely:

| Removed | Was |
|---|---|
| Direction check | `REVERSE_DELTA`, `best_error_magnitude`, `direction_check_ms`, `ERR: direction` |
| Stall check | `STALL_DELTA`, `STALL_WINDOW_MS`, `stall_reference`, `stall_check_ms`, `ERR: stall` |
| Over-travel | `ADC_SAFE_MIN/MAX`, `ERR: overtravel` |
| Move timeouts | `TIMEOUT_STEP_MS`, `TIMEOUT_HOME_MS`, `ERR: timeout` |
| End-stop rejection | `MOVE_ENDSTOP`, `JOG_ENDSTOP`, `ERR: at end-stop` |
| Jog bounds | `JOG_MIN_COUNTS`, `JOG_MAX_COUNTS` |

**`ST_FAULT` is removed** if nothing remains that can raise a fault. If any
path still can, it must be named and justified.

A move that does not reach its target simply does not reach it. The operator
sees the reading and issues another command. That is the correct behaviour on a
bench.

### Invariants

`function-description.md` §9 invariants **3, 4 and 6** are deleted:

- ~~never drive reverse at position 1~~
- ~~never drive forward at position 5~~
- ~~`deadline_ms != 0` whenever the motor is energised~~

Invariants 1, 2, 5, 7 and 8 stand unchanged. In particular **7 and 8** — motor
writes only from the tick, UART only from the main loop — remain binding. They
are structural, not protective, and removing them would break the firmware
rather than merely unguard it.

## 5. Station values

Five stations, evenly spaced 30° apart, offset 10° so station 1 has margin
below it:

| Station | Angle | ADC |
|---------|-------|-----|
| 1 | 10° | 114 |
| 2 | 40° | 455 |
| 3 | 70° | 796 |
| 4 | 100° | 1138 |
| 5 | 130° | 1479 |

Derived as `degrees * 4095 / 360`. Spacing is 341 counts throughout.

The 10° offset exists because station 1 at ADC 0 would wrap to 4095 on any
undershoot, and the firmware would read that as the opposite end of travel.

**Consequences to check:**

- `POS_WINDOW` at 80 is valid but close to its ceiling — a quarter of the
  341-count gap is 85.
- `APPROACH_COUNTS` at 200 is **more than half a station gap**, so the
  controller would be in approach mode for most of any single-station move.
  It should be reduced to around 100.

## 6. Boot

```
1. Configure GPIO, PWM, ADC, UART. STBY low.
2. Print the banner.
3. AIN1 = AIN2 = 0, raise STBY.
4. Fill the sample buffer, classify.
5. Enter IDLE at whatever the reading indicates, braked.
```

No safe-range check, no fault on boot, no homing. The position is whatever it
is.

## 7. Speed

Speed follows distance to target as before, but without the limit-station
special case:

```c
static uint8_t speed_for_error(int16_t err)
{
    uint16_t mag = err < 0 ? (uint16_t)-err : (uint16_t)err;
    return mag <= CFG_APPROACH_COUNTS ? CFG_DUTY_APPROACH : CFG_DUTY_NORMAL;
}
```

Stations 1 and 5 are ordinary stations. `DUTY_CREEP` remains available for jog
and for manual drive.

## 8. Debug console

- The `UNCOUPLED` arming interlock is **retained**, but as a deliberate-action
  gate rather than a safety measure. Manual drive should still take a
  conscious step.
- The `COUPLED` interlock is removed. There is nothing to be coupled to.
- Simulation no longer inhibits the motor. `motor_set_inhibit()` may be kept
  for convenience but is not required.
- `clearfault` is removed along with `ST_FAULT`, unless a fault path survives.

## 9. Bring-up

The staged, uncoupled procedure of `v2-sensing.md` §11 is replaced by:

1. Confirm the console responds — `adc` returns a plausible reading.
2. Rotate by hand or jog, and confirm the reading changes smoothly and
   monotonically across the range.
3. `pos 1` through `pos 5` in sequence, confirming each completes.
4. Adjust the station values if the mechanism's real positions differ, using
   `sel` / `jog` / `save` / `export`.

There is no uncoupled stage because there is nothing to uncouple from.

## 10. Wrap-around

The ADC wraps 4095 → 0. **Movement remains linear in ADC value** — a target
below the current reading moves down, above moves up. No shortest-path logic
across the wrap point.

This is deliberate: wrap-aware movement adds a class of bug for no benefit on a
rig where the operator can simply jog through the wrap manually if they want
to.

`limits` must state that movement is linear, not wrap-aware.

## 11. Protocol

`agent.md` §7 commands are unchanged. Removed from §8:

`ERR: at end-stop`, `ERR: overtravel`, `ERR: stall`, `ERR: direction`,
`ERR: timeout`, `ERR: fault home timeout`, `ERR: fault`

Retained: `ERR: invalid target`, `ERR: busy`, `ERR: position unknown`,
`ERR: unknown command`, `ERR: line too long`, and the debug console's own
plain-language rejections.

## 12. What is still enforced

Not everything is gone, and the remainder is worth stating clearly:

- The 1 kHz control tick and the watchdog
- `stop` and `.`, which must always work
- Motor writes only from the tick; UART only from the main loop
- Braking rather than coasting on arrival
- The five station values and the position window, which define what "arrived"
  means

The firmware is now a development tool with no motion protection. That is the
intent, and it is safe only because the rig cannot be damaged.
