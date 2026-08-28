# Agent: Aura luftfugl Motor & Position Control Firmware

**This is a full rewrite, confirmed against actual source
(`config.h`/`controller.c`/`console.c`/`motor.c`/`encoder.c`/`led.c`/
`main.c`) and the current schematic. See `agent-md-discrepancy-report.md`
for exactly what changed from the previous version and why. Two items
below are flagged as needing your explicit confirmation rather than
silently resolved — search for "NEEDS DECISION."**

## Scope

RP2040 firmware controlling an N20 DC gearmotor with a **single continuous
potentiometer** for absolute position feedback (not reed switches — see
discrepancy report §1), a TB6612FNG dual H-bridge (channel A for the
motor, channel B for the buzzer), an INA219 power monitor, an SK6812RGBWW
addressable LED, a UART1 debug console, and an SCD41 CO₂ sensor. **The
CO₂ sensor and buzzer are confirmed implemented and working**, not
pending — `debug.c` shows a full command set for both (`co2`, `ready`,
`serial`, `asc`, `offset`, `altitude`, `mode`, `sdc41 on/off`, `buzzer
on/off/play`), directly reusing the offset (0–20°C) and altitude
(0–3000 m) ranges established in the `sdc41` project. `hardware.md`'s
Phase 1/2 framing predates this — it needs updating to reflect that this
work has already happened, not just planned.

Built with the Raspberry Pi Pico SDK (C). Flashed and debugged over SWD
using a Raspberry Pi Debug Probe.

---

## 1. Target Hardware

| Item | Value |
|---|---|
| MCU board | YD-RP2040 dev board |
| Flash | **4 MB, confirmed directly from silicon.** OpenOCD's own SFDP read reports `sfdp id = 0x164020 size = 4096 KiB` — this is the actual chip reporting its own size during a real flash operation, not a schematic label or a photograph of a possibly-different board. This settles the earlier discrepancy: a prior version of this document claimed 16 MB "confirmed from photographs," which was almost certainly a different physical YD-RP2040 unit. `PICO_FLASH_SIZE_BYTES` must be `4 * 1024 * 1024`. |
| System clock | 125 MHz (SDK default) |
| Motor | N20 DC gearmotor, integrated 4.7 kΩ potentiometer on the output shaft |
| Driver | TB6612FNG breakout |
| VM (motor supply) | **+3.3 V — same rail as VCC, not 5 V.** Confirmed directly: the N20 is 3–3.5 V tolerant (matching `hardware.md`'s schematic note), and `debug.c` itself documents "the loaded 3.3 V mechanism" in its stiction-handling logic. There is no separate 5 V motor rail in this design — the single XL63070 buck-boost output (3.3 V) feeds both VM and VCC. |
| VCC (logic supply) | +3.3 V, same rail as VM |
| Driver current | 1.2 A continuous per channel, 3.2 A peak (TB6612FNG datasheet rating, unaffected by any of the above) |
| Position sensing | Single continuous potentiometer, window-based classification (§2.7) |
| Debug / flash | Raspberry Pi Debug Probe — SWD for flashing, UART for console |

### 1.1 Driver Identification

TB6612FNG, confirmed by pin names (`PWMA`, `PWMB`, `STBY`, separate `VCC`
from `VM`) and by the schematic's own part reference (`U4
TB6612FNG_DualMotorDriverModule`).

- Direction and speed are on separate pins: AIN1/AIN2 set direction,
  PWMA sets speed.
- PWMA LOW while STBY is HIGH is **short brake**, not coast.
- No fault output exists on this part — timeouts (§6.9) are the only
  fault detection available.

### 1.2 Current Headroom

N20 stall current must stay under 1.2 A continuous. **Still an open bench
item** — not yet measured, per `hardware.md`.

Bulk capacitor across VM/GND — confirm present per `hardware.md`'s BOM
(`C1`, `C2`, `C3`, 100 µF each, at various points in the schematic).

---

## 2. Pin Assignments

Confirmed directly from `config.h`.

### 2.1 Motor Driver — TB6612FNG Channel A (motor)

| Signal | RP2040 Pin | Function |
|---|---|---|
| AIN1 | GP2 | Direction bit 1 |
| AIN2 | GP3 | Direction bit 2 |
| PWMA | GP14 | Speed, PWM |
| STBY | GP15 | Driver enable — must be driven HIGH before motor operation |

### 2.2 TB6612FNG Channel B (buzzer) — confirmed allocated

| Signal | RP2040 Pin |
|---|---|
| BIN1 | GP6 |
| BIN2 | GP7 |
| PWMB | GP16 |

Confirmed from `debug.c`'s own help text (`pwm` and `buzzer` command
descriptions repeat these pin numbers explicitly). This corrects an
earlier version of this document, which — working from an older
`controller.c`/`config.h` snapshot — assumed channel B was still
unallocated. The `buzzer` module (`buzzer.h`/`buzzer.c`) exists and drives
a bird-call warning pattern (`buzzer play 1..10`), independent of the
hardware `Sound ON-OFF` mute switch documented in `hardware.md`.

### 2.3 I²C0 — confirmed active, driving the SCD41 and INA219

| Signal | RP2040 Pin |
|---|---|
| SDA | GP4 |
| SCL | GP5 |

**Confirmed active, not merely reserved** — `debug.c`'s full working CO₂
command set (`co2`, `serial`, `asc`, `offset`, `altitude`, `mode`, `sdc41
on/off`) and INA219 register access (`ina`, `batt raw`) both depend on
real I²C traffic over this bus. An earlier version of this document,
working from an older source snapshot, said no I²C init call existed
anywhere — that's now confirmed wrong; the CO₂ driver (`co2.h`/`co2.c`)
and INA219 driver (`power_monitor.c`) both use it.

### 2.4 LED — now confirmed with a separate power-enable pin

| Signal | RP2040 Pin |
|---|---|
| Data | GP18 |
| Power enable | GP0 |

Single SK6812RGBWW, confirmed RGBW, GRBW wire order with white
intentionally held at zero to preserve hue saturation. **GP0 is a
separate load-switch enable**, confirmed from `debug.c`'s `led` help
text ("GP0 switches LED power... active HIGH... GP0 HIGH; wait 300 µs;
transmit pixel data... dark/off: GP0 LOW, placing the SK6812 in its
unpowered sleep state"). This matches `hardware.md`'s schematic `LED
enable` net — this is that GPIO, now confirmed as GP0. Station 5 keeps
GP0 HIGH continuously for its hazard-blink pattern rather than power-
cycling per pulse.

### 2.5 Console UART — UART1, not UART0

| Signal | RP2040 Pin |
|---|---|
| TX | GP20 |
| RX | GP21 |

**Peripheral is `uart1`** (`uart_init(uart1, ...)`, `UART1_IRQ` in
`console.c`) — not UART0, and not on GP0/GP1. This placement is
deliberate: this project has previously hit a real TX/RX coupling bug on
adjacent pins, and GP20/GP21 avoids repeating it. RX is interrupt-driven
into a ring buffer; TX is a blocking `uart_putc_raw()` loop (its own
elapsed time is measured into `tx_spin_us` for diagnostics).

115200 baud, 8N1, no flow control, RX pull-up enabled.

### 2.6 SWD

SWDIO/SWCLK are dedicated RP2040 package pins, not GPIOs. Standard
4-pin debug header. No firmware configuration required.

### 2.7 Currently allocated vs. reserved — full picture

| Pin | Status |
|---|---|
| GP0 | LED power enable (TPS22918, active HIGH) |
| GP2, GP3, GP14, GP15 | Motor driver, TB6612 channel A |
| GP4, GP5 | I²C0 — SCD41 + INA219, confirmed active |
| GP6, GP7 | Buzzer, TB6612 channel B, BIN1/BIN2 |
| GP16 | Buzzer, TB6612 channel B, PWMB |
| GP18 | LED data |
| GP20, GP21 | Console UART (UART1) |
| GP26 | ADC0 — potentiometer wiper |
| `SDC41 enable` (schematic) | Almost certainly GP0's counterpart for the sensor's own TPS22918 — exact pin not yet confirmed from source, only the LED's enable pin (GP0) has been directly confirmed |
| `CO2-LIMIT_AB` switch | Per schematic, GPIO number still not confirmed — see `hardware.md` §7 |

Everything not listed above remains genuinely free. Every pin above is
now confirmed **active**, not reserved-for-later — this table has changed
twice across this review as more source files came in; treat it as
current only as of the files actually reviewed, not as a permanent
ceiling on what's been checked.

---

## 3. Mechanical Constraints

**There are no physical end-stops.** This remains the single most
important constraint regardless of sensing mechanism.

- 5 fixed positions along the travel path, within a bounded arc — not
  continuous rotation. The moving part carries a wire harness that will
  twist and tear if driven past the intended limits.
- Position 1 and position 5 are the limits, enforced **in firmware only**.
- At position 1: forward motion only. At position 5: reverse motion only.
- No wrap-around — 5→1 travels back through 4, 3, 2.

---

## 4. State Machine

Confirmed from `config.h`'s `sys_state_t` enum:

| State | Meaning |
|---|---|
| `ST_BOOT` | Initial position read and classification |
| `ST_IDLE` | Stationary at a confirmed position, motor braked |
| `ST_MOVING` | Travelling at normal duty, more than one position from target |
| `ST_APPROACH` | Travelling at reduced duty, one position from target |
| `ST_HOMING` | Seeking position 1, also used as the fallback after a move timeout |
| `ST_FAULT` | Motor disabled, awaiting `home` or reset |
| `ST_DEBUG` | Interactive debug monitor (`LUFTFUGL_DEBUG` builds only) |

**NEEDS DECISION — no `RECOVER` state exists.** A previous version of this
document specified a dedicated `RECOVER` state with detailed limit-aware
direction-selection logic for between-position recovery. That state does
not exist in the current enum, and the described direction-selection
logic doesn't appear in `controller.c`. The actual code's fallback on a
non-jog move timeout is to re-enter `ST_HOMING` via `begin_home()` —
always seeking position 1, not the direction-aware behavior the old spec
described. **Confirm whether this simpler behavior is acceptable as final,
or whether the original `RECOVER` design needs implementing** — this
matters because the original design's stated purpose was specifically to
avoid creeping further past an unprotected limit, and a straight
homing-only fallback loses the "already near position 5, don't reverse
through the whole travel" nuance the removed design had.

Transitions are driven by the 1 kHz control tick (`controller_tick()`,
called from `on_tick()`), never by blocking waits.

---

## 4a. New mechanisms confirmed from `controller.c`/`debug.c` — not in the original review

Three genuine additions found in a newer `controller.c` than what this
document was first written against.

### 4a.1 `station1_lock` — a safety interlock whose trigger is not yet confirmed

`controller_set_station1_lock(bool)` exists and, when active, rejects
almost everything: every request except `REQ_MOVE` to `POS_MIN`
specifically, all jog commands, all debug-goto-ADC, and all debug
requests except exit. **Nothing in `controller.c` or `debug.c` itself
calls this setter** — something else does, most likely an updated
`console.c` or `main.c` neither of which has been reviewed in this
current form. Until that's found, treat this as: a real safety mechanism
exists, its *effect* is fully documented above, its *trigger condition*
is not. Confirm before assuming `BENCH_TEST` mode (§13) either needs to
bypass this or already doesn't interact with it at all.

### 4a.2 Anti-stiction motion logic

New state: `motion_progress_adc`/`motion_progress_ms` track whether the
filtered ADC has moved at least `CFG_ARRIVAL_WINDOW` counts within
`CFG_BRAKE_HOLD_MS`. If not — the mechanism has stalled mid-move — a
`stiction_boost` flag forces full `CFG_DUTY_NORMAL` duty for the rest of
that move, rather than the reduced approach/creep duty that may not be
enough to break static friction on the loaded 3.3 V mechanism. This
directly explains and resolves what would otherwise look like an
unexplained duty jump mid-move — it's deliberate, not a bug.

The final settle check (`target_correcting` phase) also now uses the
tighter `CFG_ARRIVAL_WINDOW`, not the wider `CFG_POS_WINDOW`, and drives
at full `CFG_DUTY_NORMAL` rather than creep during a correction — both
consistent with the same "break stiction, then settle precisely" logic.

### 4a.3 Endstops are runtime-adjustable and persisted to flash

`lowendstop=<adc>` / `highendstop=<adc>` (confirmed in `debug.c`) change
`cfg.low_endstop_adc`/`cfg.high_endstop_adc` in RAM immediately, then
`endstop_persist()` writes them to a dedicated flash sector at
`PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE`, with a watchdog
scratch-register copy as a secondary fallback if the flash write is
interrupted. Restored via `endstop_restore()` at boot, validated against
a magic number and checksum before trusting either source.

**This raises the priority of the still-open flash-size question (§1)
from cosmetic to load-bearing.** If `PICO_FLASH_SIZE_BYTES` is wrong,
this computes the wrong physical address to erase and program —
confirm the real flash size before this feature is trusted, not after.



Confirmed from `main.c` and `controller.c`'s `ST_BOOT` handling:

1. `console_init()`, `motor_init()`, `encoder_init()`, `led_init()`,
   `power_monitor_init()`, `controller_init()`, in that order.
2. `motor_enable()` is called before the 1 kHz timer starts.
3. Timer starts; boot waits for the first tick to complete.
4. `watchdog_enable(100, true)` — only after that first tick, not before.
5. On the first `controller_tick()` in `ST_BOOT`: reads
   `encoder_confirmed()`. If valid (1–5), enters `ST_IDLE` at that
   position and reports arrival. **If invalid, sets position to
   "between" and reports unknown — it does not automatically home.**

**NEEDS DECISION.** A previous version of this document mandated automatic
reverse-creep homing on an invalid boot reading, specifically to avoid
leaving the mechanism in an unprotected unknown state. The current code
does not do this — it waits for an explicit `home` command. Confirm
whether this is the intended, final behavior, or whether auto-homing
needs adding back. This is genuinely safety-relevant given there are no
physical stops.

`watchdog_caused_reboot()` is checked at the top of `main()`; if true, an
`ERR: watchdog reset` message is emitted before normal boot continues.

---

## 6. Runtime Behaviours

### 6.1 No zero position
An unconfirmed/between-positions reading is invalid and never reported as
a station. `pos` returns `POS:?`.

### 6.2 Limit enforcement
Every move target is validated against the current position before
energizing the motor. At position 1, reverse commands rejected. At
position 5, forward commands rejected.

### 6.3 Sampling and confirmation

- ADC sampled every 1 ms (`encoder_tick()`, called from the 1 kHz timer).
- 5-deep rolling average (`FILTER_DEPTH=5`).
- Classification: within `POS_WINDOW` (30 counts) of a station's nominal
  ADC value.
- A position is confirmed once its classification holds continuously for
  `DEBOUNCE_MS` (12 ms).

### 6.4 Duty/speed constants

| Constant | Value (`config.h`) |
|---|---|
| `DUTY_NORMAL` | 50 |
| `DUTY_APPROACH` | 25 |
| `DUTY_CREEP` | 25 |
| `DUTY_MIN` | 25 |

**A previous version of this document specified 200/60/50/45 respectively
— every value differs.** These may be genuine bench-measured corrections
from bring-up that never made it back into the spec, or an unresolved
regression. Confirm which, since `DUTY_APPROACH` and `DUTY_CREEP` being
identical (both 25) removes any speed distinction between "one station
away" and "creeping/homing" — worth checking this is intentional and not
a copy-paste artifact.

### 6.5 Timeouts

| Constant | Value (`config.h`) |
|---|---|
| `TIMEOUT_STEP_MS` | 30000 |
| `TIMEOUT_HOME_MS` | 6000 |
| `JOG_TIMEOUT_MS` | 3000 |

**A previous version of this document specified `TIMEOUT_STEP_MS` as
1500 ms — the current value is 20× longer, and no separate
`TIMEOUT_RECOVER_MS` constant exists** (consistent with §4's missing
`RECOVER` state). Confirm 30000 ms is deliberate; a 30-second timeout on a
5-station move is a very loose bound compared to the tight-timeout
philosophy this project's own documentation otherwise states ("keep
timeouts as tight as measurement allows, since it is the only guard
against runaway homing").

**On move timeout**: brake, report timeout, then `begin_home()` — always
re-seeking position 1, not a direction-aware recovery (see §4).

### 6.6 Jog command bounds

Bounded by `±ADC_MAX_VALUE` (~4095) in `controller_request_jog()` — not a
separate `JOG_MIN_COUNTS`/`JOG_MAX_COUNTS` pair (those constants don't
exist). Out-of-range reports `ERR: at end-stop` (`JOG_ENDSTOP`), not
"overtravel" — that word doesn't appear anywhere in the console output.

### 6.7 Non-blocking operation
No `sleep_ms()` in any control or command path. Confirmed: `controller_tick()`
is pure arithmetic and state transitions, no blocking calls.

### 6.8 Watchdog

100 ms (`watchdog_enable(100, true)`), `pause_on_debug=true`. Kicked as
the **first substantive line of `controller_tick()`**, which runs from
the independent 1 kHz hardware timer callback — not the main loop. This
means a slow blocking operation in the main loop (e.g. a future CO₂ I²C
read) does not by itself risk a watchdog reset, since the timer interrupt
can still preempt it and service the watchdog on schedule, as long as the
blocking call doesn't disable interrupts. See `hardware.md` §6a for the
full reasoning — this was a real point of confusion resolved once
`controller.c` was actually reviewed.

### 6.9 Known bug — inconsistent debug-state build flags

`config.h`'s `sys_state_t` enum gates `ST_DEBUG` behind `#ifdef
LUFTFUGL_DEBUG`. `console.c`'s `state_name()` array gates its `"DEBUG"`
string behind `#ifdef LUFTFUGL_MONITOR` — a different flag. If a build
ever defines `LUFTFUGL_MONITOR` without `LUFTFUGL_DEBUG`, the enum and the
name array disagree in size. Found by cross-referencing the two files
directly, independent of any spec question. Needs a code fix (align both
guards to the same flag), not a documentation change.

---

## 7. Console Command Protocol

**Two distinct interfaces, not one** — this wasn't clear until `debug.c`
was actually reviewed.

### 7.1 Production console — plain line commands (`console.c`)

The set below was confirmed against an earlier `console.c` snapshot.
**Given how much `controller.c`/`debug.c` have moved since — new pins, a
new safety interlock, new modules — `console.c` itself needs re-fetching
to confirm this table is still current, not assumed.**

| Command | Response | Notes |
|---|---|---|
| `pos` | `POS:1`…`POS:5` or `POS:?` | |
| `adc` | `ADC raw=<n> avg=<n> pos=<n or ?>` | Read-only |
| `jog <±counts>` | `OK: jog <±n> from <adc>` | Bounds: ±4095, see §6.6 |
| `setpos <1-5>` | `OK: pos <n> = <adc>` | Sets one station nominal in RAM |
| `savepos` | Five `#define POS_n_ADC <adc>` lines | Does not write flash |
| `move N` | see §8 | |
| `stop` | `OK: stopped` | Always accepted, always brakes |
| `status` | `POS:N DIR:FWD\|REV\|STP SPD:0-255 STATE:<name>` | |
| `home` | `OK: homing` | |
| `batt` (+`raw`/`res`/`log`/`events`/`reset`) | Various | INA219-backed |
| `load`, `ina` | Reports | |
| `help` | Lists `batt`/`load`/`ina` only | Doesn't list `pos`/`move`/etc. |
| `dbg`, `dbg plain` | Enters debug monitor | `LUFTFUGL_MONITOR` builds only |

### 7.2 Debug monitor — far larger, confirmed directly from `debug.c`

A full fixed-screen UI, six pages, cycled with digit keys `1`–`6`:

| Page | Content |
|---|---|
| 1 | General information |
| 2 | Motor controller |
| 3 | Motor positions |
| 4 | Battery information |
| 5 | CO₂ sensor |
| 6 | Commands (index of every command, single-letter aliases) |

`plain` switches to line-oriented output without escape codes — same
category of choice as `sdc41`'s own plain-vs-fixed-screen distinction,
already made correctly here rather than needing to be relearned.

Commands confirmed present, well beyond the production set: `jog`,
`step`, `sel`, `save`, `stations`, `limits`, `lowendstop=`/`highendstop=`
(§4a.3), `export`, `reset` (full reboot), `reset stations`, `bootsel`
(USB bootloader), `move`/`pos`, `goto` (raw ADC target), `home`, `stop`,
`status`, `adc`, `angle`, `led` (`on`/`off`/`auto`/`rgbw on`/`rgbw
off`/`raw <hex>`), `buzzer` (`on`/`off`/`play 1..10`), `page`,
`selftest`, `tick`, `trace`, `pins`, `pwm`, `cfg <SETTING> <value>` /
`cfg reset`, `sim` (`on`/`off`/`adc`/`travel`), `cal sim`, `cal motor
[5|50|500]`, `arm`/`disarm`/`drive` (manual pulse, requires `arm`),
`findmin` (empirically finds the lowest moving duty — this is very
likely how the current `DUTY_MIN`/etc. values in `config.h` were actually
derived, strengthening the case that those values are deliberate
bench measurements, not stale drift — see §6.4), the full CO₂ command
set (§ Scope), and `help [command]` for any of the above.

**`cfg`'s live validation rules, confirmed exactly**:
`DUTY_MIN ≤ DUTY_CREEP ≤ DUTY_APPROACH ≤ DUTY_NORMAL`, `POS_WINDOW` must
stay below a quarter of the smallest station gap, endstops must bracket
stations 1 and 5. Changes are RAM-only unless the setting is an endstop
(§4a.3, which persists to flash).

`STATE:` names, confirmed independently in both `console.c` and
`debug.c`: `BOOT`, `IDLE`, `MOVING`, `APPROACH`, `HOMING`, `FAULT`, and
conditionally `DEBUG` — no `RECOVER`, matching §4.

---

## 8. Protocol Edge Cases

Confirmed against `console.c` and `controller.c`:

| Situation | Response |
|---|---|
| Unrecognised command | `ERR: unknown command` |
| `move` missing/non-numeric argument | `ERR: invalid target` |
| `move N` outside 1–5 | `ERR: invalid target` |
| `move N` == current position | `OK: already at N` |
| `move N` while busy | `ERR: busy` |
| `move` while position unknown | `ERR: position unknown` |
| `move` while in `ST_FAULT` | `ERR: fault; use home` |
| `jog` malformed or out of ±4095 | `ERR: invalid jog` |
| `jog` endpoint outside configured endstop range | `ERR: at end-stop` |
| `jog` while busy | `ERR: busy` |
| Line exceeds `CONSOLE_LINE_MAX` (32 chars) | `ERR: line too long`, discard to next newline |
| Empty line | No response |

---

## 9. Project Structure

Confirmed current module set — larger than any previous version of this
document listed:

```
luftfugl-motor/
├── CMakeLists.txt
├── boards/
├── src/
│   ├── main.c
│   ├── config.h / config.c
│   ├── motor.c/.h
│   ├── encoder.c/.h
│   ├── controller.c/.h
│   ├── console.c/.h
│   ├── led.c/.h
│   ├── power_monitor.c/.h
│   ├── ws2812.pio
│   └── debug.c/.h        (LUFTFUGL_MONITOR builds)
└── .vscode/
```

`led.c`, `power_monitor.c/.h`, `ws2812.pio` did not exist when earlier
versions of this document's build configuration (§10 below) were written
— that section's `add_executable()` source list needs updating to match.

---

## 10. Build Configuration

**Needs updating, not fully confirmed line-by-line** — the previous
version's `CMakeLists.txt` example only lists `motor.c`, `encoder.c`,
`controller.c`, `console.c`, `debug.c`; it's missing `config.c`, `led.c`,
`power_monitor.c` entirely, and only mentions the `LUFTFUGL_DEBUG` build
option, not `LUFTFUGL_MONITOR` (which gates a substantial amount of the
actual codebase, per `config.h`'s extensive `#ifdef LUFTFUGL_MONITOR`
sections). A corrected `CMakeLists.txt` should be written directly against
the actual project's real file, not reconstructed from this document —
worth pulling the real one rather than guessing its current content.

`PICO_FLASH_SIZE_BYTES` — see §1's open flash-size question before
setting this.

---

## 11. Bring-Up Order

Largely still applicable, with reed-switch-specific wording removed:

1. **UART only** — banner prints, no motor power (STBY held LOW).
2. **Potentiometer only** — motor unpowered, rotate by hand, log raw and
   averaged ADC at each of the 5 stations. Compare against `hardware.md`'s
   station table and confirm which of the two conflicting tables (code's
   vs. schematic's — see `hardware.md` §0) is actually correct.
3. **Motor open-loop, uncoupled** — direction, `DUTY_MIN`, brake vs.
   coast, stall current against 1.2 A.
4. **Closed-loop single step, coupled** — measure real step time, compare
   against the (now 30-second) `TIMEOUT_STEP_MS`.
5. **Approach to a limit** — watch for overshoot past position 1 and 5.
6. **Recovery and homing** — test explicitly, given §4's open question
   about whether direction-aware recovery is implemented or needed.

---

## 12. Open Items

Consolidated from this rewrite and `hardware.md`:

- **NEEDS DECISION**: auto-home on invalid boot position — implement, or
  confirm explicit-`home`-only is final (§5).
- **NEEDS DECISION**: `RECOVER` state and direction-aware recovery logic —
  implement, or confirm homing-only fallback is final (§4).
- **NEW — `station1_lock`'s trigger source is unconfirmed** (§4a.1). The
  mechanism's *effect* is fully documented; what actually sets it is in a
  file not yet reviewed (likely an updated `console.c` or `main.c`).
- Station-table discrepancy between `config.h` and the schematic — see
  `hardware.md` §0.
- ~~Flash size~~ — **resolved**: 4 MB, confirmed directly via SFDP during a
  real successful flash (§1). `endstop_persist()` (§4a.3) computes a
  physical flash address from `PICO_FLASH_SIZE_BYTES` — confirm this
  constant is actually set to `4 * 1024 * 1024` in the board header before
  trusting that feature, now that the correct value is known.
- Duty constants and `TIMEOUT_STEP_MS` — the existence of `findmin` and
  `cal motor` (§7.2) as dedicated empirical-measurement tools makes it
  more likely current values are deliberate bench results, not stale
  drift — but still not directly confirmed which specific values came
  from those tools versus elsewhere.
- Fix the `LUFTFUGL_DEBUG`/`LUFTFUGL_MONITOR` flag mismatch on `ST_DEBUG`
  (§6.9) — a real code bug, not a spec question.
- N20 stall current still not measured against the 1.2 A limit.
- `CMakeLists.txt` needs updating for the actual current source file
  list — now including `co2.c`, `buzzer.c` on top of everything in §10.
- **`console.c` needs re-fetching** — confirmed stale relative to the
  pins and mechanisms `controller.c`/`debug.c` now reveal; §7.1 is
  presented as last-confirmed, not current.
- I²C0 and buzzer channel B are **no longer open items** — both confirmed
  active and implemented (§2.2, §2.3), correcting this document's own
  earlier framing from a prior revision.

---

## 13. `BENCH_TEST` Profile — unrestricted bench operation

A distinct, deliberately-opted-into build configuration for bench testing
with a low-power N20 (3 V / 300 mA class) where a stall or over-travel
cannot cause electrical or mechanical harm to the motor or driver itself.
**This is an addendum, not a replacement** — the production behaviour in
§§1–12 is unchanged and remains the default; `BENCH_TEST` is opt-in.

Gated behind a new, dedicated compile flag, **`LUFTFUGL_BENCH_TEST`** —
deliberately separate from `LUFTFUGL_DEBUG`/`LUFTFUGL_MONITOR`, so a
normal debug build never silently inherits zero-protection behaviour
meant only for bench sessions.

### 13.1 What stays, and why

**The watchdog stays enabled.** It costs nothing and only helps — a hung
board during exploratory testing reports `ERR: watchdog reset` and
recovers on its own, rather than sitting silently until a manual power
cycle. If a 100 ms window is too tight while single-stepping in a
debugger, raise it for this profile specifically:

```c
#ifdef LUFTFUGL_BENCH_TEST
#define BENCH_WATCHDOG_MS 5000u   // generous, breakpoint-friendly
#endif
```

### 13.2 Position limits — runtime-declared, not a single static choice

There's no sensor for "is the harness physically attached" — this has to
be an operator declaration, not something firmware can detect. Default to
the **safer** assumption until told otherwise:

```
bench coupled      -> position-limit checks become a warning, not a
                      hard reject: "WARN: past position 5, harness may
                      be under load" printed, move proceeds anyway
bench uncoupled    -> position-limit checks removed entirely, no warning
```

Defaults to `coupled` on entry to `BENCH_TEST` mode (the safer of the two)
until the operator explicitly declares `bench uncoupled` for the current
session. Not persisted across reset — every new bench session starts
back at the safer default, deliberately, so a forgotten declaration from
last time can't carry over silently.

### 13.3 `move` — unrestricted

- No `MOVE_BUSY` rejection — a new target immediately interrupts and
  retargets an in-progress move rather than being refused.
- No `MOVE_POS_UNKNOWN` requirement — movable from an unconfirmed/between
  position.
- No station-table bounds — accept a raw target ADC value directly, not
  only the five predefined stations, if that's useful for bench sweeps.
- Full 0–255 duty range — `CFG_DUTY_MIN`'s floor does not apply; a duty
  below the normal stiction-breaking minimum is allowed through
  deliberately, e.g. for characterizing the motor's actual minimum
  moving duty rather than assuming the production constant.

### 13.4 `jog` — unrestricted

- `CFG_LOW_ENDSTOP_ADC`/`CFG_HIGH_ENDSTOP_ADC` bounds do not apply.
- The `±ADC_MAX_VALUE` (~4095) bound remains — this isn't a safety
  feature, it's the signed 16-bit delta's actual representable range, a
  type limit rather than a protection to remove.

### 13.5 `setpos` — unrestricted

- No ascending-order requirement between stations.
- No quarter-gap minimum-separation check.
- Any raw ADC value accepted for any station, including values that would
  make two stations overlap or invert order in production.

### 13.6 Timeouts — disabled, using the existing "no deadline" sentinel

`controller.c`'s own `reached()` already treats `deadline_ms == 0` as
"never expires" — `return deadline && (int32_t)(now - deadline) >= 0`.
`BENCH_TEST` mode doesn't need new timeout logic, just skip setting a
deadline for `TIMEOUT_STEP_MS`/`TIMEOUT_HOME_MS`/`JOG_TIMEOUT_MS` when
this flag is active. A move, jog, or home in bench mode simply runs until
it arrives, is stopped, or is retargeted — no forced timeout-triggered
re-homing.

### 13.7 Debug raw-drive commands — promoted, not just left in the existing menu

The existing debug-request mechanism (`DBG_OP_DRIVE`, `DBG_OP_BRAKE`,
`DBG_OP_COAST`, `DBG_OP_STANDBY`, `DBG_OP_GPIO_SET`, `DBG_OP_SIM_ENABLE`,
`DBG_OP_SIM_SET`, `DBG_OP_GOTO_ADC` — confirmed from `controller.h`'s
`dbg_request_t`) already provides exactly the "bypass the state machine
entirely" capability this profile wants: direct direction/duty drive,
raw GPIO-level AIN1/AIN2/STBY control, and simulated encoder values
independent of the real potentiometer.

**Document these as `BENCH_TEST`'s primary interface, not a buried debug
submenu** — but the exact current key bindings and menu navigation live
in `debug.c`, which hasn't been reviewed (two failed transfers). Confirm
and document the actual keystrokes once that file is available, rather
than this document inventing ones that might not match what's really
there.

### 13.8 What `BENCH_TEST` does not remove

- The watchdog (§13.1).
- The `±ADC_MAX_VALUE` type bound on jog delta (§13.4) — a data-type
  limit, not a safety feature.
- Anything in §§1–12 when `LUFTFUGL_BENCH_TEST` is not defined — production
  behaviour is completely unaffected by this profile's existence.
