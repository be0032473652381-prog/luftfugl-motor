# luftfugl — Motor & Position Control Firmware

RP2040 firmware that drives an N20 gearmotor to any of five fixed positions,
sensed by a single continuous potentiometer on one ADC pin, with a plain-text
UART console for control and diagnostics — plus an SCD41 CO₂ sensor, a
buzzer, and an addressable RGBW LED.

Written in C against the Raspberry Pi Pico SDK. Flashed and debugged over SWD
with a Raspberry Pi Debug Probe.

---

> ## ⚠ There are no physical end-stops
>
> Positions 1 and 5 are travel limits enforced **in firmware only**. Nothing
> mechanically prevents the motor from driving past them, and the moving part
> carries a wire harness that will twist and tear if it does.
>
> - Never run the motor open-loop while coupled to the mechanism.
> - Never bypass the limit checks "just to test the motor".
> - Bring-up stages 1–3 are done with the motor **uncoupled**.
> - Keep the supply switch within reach during the first coupled tests.

---

## Hardware at a Glance

| | |
|---|---|
| MCU | YD-RP2040 dev board — flash size unresolved, see `agent.md` §1 |
| Driver | TB6612FNG, channel A (motor), channel B (buzzer) |
| Motor | N20 DC gearmotor, integrated 4.7 kΩ potentiometer on the output shaft |
| Sensing | Single continuous potentiometer → GP26 (ADC0), window-based classification — not reed switches |
| Supplies | VM and VCC both +3.3 V, same rail — not a separate 5 V motor supply |
| Console | UART**1** on GP20/GP21, 115200 8N1 |
| Also on board | SCD41 CO₂ sensor (I²C0), SK6812RGBWW LED, INA219 power monitor |

| RP2040 | Function |
|---|---|
| GP0 | LED power enable |
| GP2 / GP3 | AIN1 / AIN2 (motor direction) |
| GP4 / GP5 | I²C0 — SCD41 + INA219 |
| GP6 / GP7 | BIN1 / BIN2 (buzzer direction) |
| GP14 | PWMA (motor speed) |
| GP15 | STBY (motor driver enable) |
| GP16 | PWMB (buzzer) |
| GP18 | LED data |
| GP20 / GP21 | Console UART1 TX / RX |
| GP26 (`A0`) | Potentiometer wiper |

Full wiring, BOM and power tree: **[`hardware.md`](hardware.md)**.

---

## Documentation

| File | Contents |
|---|---|
| **[`agent.md`](agent.md)** | Behavioural specification — what the firmware must do. Pin assignments, position sensing, state machine, timing, protocol, `BENCH_TEST` profile. The authoritative spec. |
| **[`hardware.md`](hardware.md)** | What gets physically built. BOM, wiring, power tree, schematic-confirmed facts, still-open hardware questions. |
| **[`function-description.md`](function-description.md)** | How the code is organised. Every module, every public function, its contract and execution context. |
| **[`agent-md-discrepancy-report.md`](agent-md-discrepancy-report.md)** | What changed in `agent.md` and why — confirmed line-by-line against actual source, not assumed. |

Read `agent.md` first. If the code and the spec disagree, the spec is right
until it's deliberately changed — several open decisions are marked directly
in that document as **NEEDS DECISION**, not silently resolved.

---

## Repository Layout

```
luftfugl-motor/
├── CMakeLists.txt          — needs updating, see agent.md §10
├── boards/
├── src/
│   ├── main.c
│   ├── config.h / config.c
│   ├── motor.c/.h
│   ├── encoder.c/.h        — potentiometer, not reed-switch, sampling
│   ├── controller.c/.h
│   ├── console.c/.h
│   ├── led.c/.h
│   ├── power_monitor.c/.h
│   ├── ws2812.pio
│   ├── co2.c/.h            — SCD41 driver, confirmed implemented
│   ├── buzzer.c/.h         — confirmed implemented
│   └── debug.c/.h          (LUFTFUGL_MONITOR builds)
└── .vscode/
    └── launch.json
```

---

## Building

Requires the Pico SDK, `arm-none-eabi-gcc`, CMake ≥ 3.13, and `PICO_SDK_PATH`
set.

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j4
```

Produces `build/luftfugl.elf` and `build/luftfugl.uf2`.

## Flashing

Over SWD with the Debug Probe:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 5000" \
        -c "program build/luftfugl.elf verify reset exit"
```

Fallback: hold BOOTSEL, plug in USB-C, drag `luftfugl.uf2` onto the mass
storage device.

**Flash size is not yet confirmed** — `picotool info` should be checked
against whichever number `agent.md` §1 settles on, not assumed to be 16 MB.

## Console

```sh
minicom -b 115200 -o -D /dev/ttyACM0
```

The Debug Probe presents SWD and UART as separate USB devices.

## Debugging

`.vscode/launch.json` is configured for cortex-debug with OpenOCD. Note that
**halting at a breakpoint does not stop the motor** — the PWM hardware keeps
running with the CPU stopped.

---

## Command Reference

Two separate interfaces exist — see `agent.md` §7 for the full breakdown:

- **Production console** (`console.c`) — the short command set below. This
  table reflects an earlier confirmed snapshot; `agent.md` flags it as
  possibly stale given how much else has changed, and not yet re-verified.
- **Debug monitor** (`debug.c`, `LUFTFUGL_MONITOR` builds) — a much larger,
  six-page fixed-screen interface with dozens of commands, including full
  CO₂ sensor control, battery diagnostics, calibration tools, and a
  manual motor-drive interlock. Documented in full in `agent.md` §7.2.

| Command | Response |
|---|---|
| `pos` | `POS:1` … `POS:5`, or `POS:?` |
| `move N` | `OK: moving to N` |
| `stop` | `OK: stopped` |
| `home` | `OK: homing` |
| `status` | `POS:N DIR:FWD\|REV\|STP SPD:0-255 STATE:<name>` |

Unsolicited during a move:

| Message | Meaning |
|---|---|
| `PASS:N` | Crossed station N in transit |
| `ARR:N` | Position N confirmed, motor stopped |

---

## How It Works

**Sensing.** A single continuous potentiometer, mechanically coupled to the
motor's output shaft, feeds GP26 (ADC0). A station is confirmed when the
filtered ADC value falls within `POS_WINDOW` counts of that station's
nominal value — not a discrete resistor-ladder band lookup. See `agent.md`
§2.7/§3 for the station table and its currently-unresolved discrepancy
against the schematic.

**Filtering.** Sampled at 1 kHz into a 5-deep rolling average, confirmed once
the classification holds continuously for `DEBOUNCE_MS`.

**Moving.** Duty scales down approaching the target; an anti-stiction
mechanism forces full duty if the mechanism stalls mid-move rather than
remaining at a reduced speed that can't overcome static friction — see
`agent.md` §4a.2.

**Recovering.** The direction-aware `RECOVER` state described in earlier
documentation **does not exist in the current code** — a timeout instead
falls back to homing toward position 1 unconditionally. This is flagged as
a genuine open decision in `agent.md` §4, not silently resolved either way.

**Failing.** A move that overruns its deadline brakes and homes. If homing
also overruns, the driver is disabled and the system faults until `home` or
reset.

---

## Tuning

Everything adjustable lives in `src/config.h`. **Current values differ
substantially from earlier documentation** — see `agent.md` §6.4/§6.5 for
the full comparison and the open question of whether current values are
deliberate bench corrections or drift. Tools exist specifically for
empirical measurement: `findmin` (lowest moving duty) and `cal motor`
(randomized station-move statistics), both in the debug monitor.

---

## Bring-Up Order

Do not jump to the full state machine. Stages 1–3 with the motor
**uncoupled**.

1. UART only — banner prints, no motor power.
2. Potentiometer only — rotate by hand, log readings at all five positions,
   compare against both station tables in `hardware.md` §0.
3. Motor open-loop, uncoupled — direction, `DUTY_MIN`, brake vs. coast,
   stall current.
4. Single step, coupled — measure actual step time against the current
   30-second `TIMEOUT_STEP_MS`.
5. Approach to a limit — watch for overshoot past positions 1 and 5.
6. Recovery and homing — test explicitly, given the open `RECOVER` question
   above.

Full detail in `agent.md`'s Bring-Up Order section.

---

## Status

CO₂ sensing, buzzer, and LED indication are confirmed implemented and
working — not pending, correcting earlier documentation in this project.
Open items, consolidated in `agent.md` §12:

- Flash size unresolved (4 MB per schematic vs. an earlier 16 MB claim).
- Two safety-relevant behaviors need an explicit decision: auto-home on
  invalid boot position, and whether direction-aware recovery is needed.
- `station1_lock`'s trigger source is unconfirmed.
- N20 stall current not yet measured against the 1.2 A driver limit.
- `console.c` needs re-fetching to confirm it's current.

---

## Notes

The TB6612FNG has no fault output. Thermal shutdown and overcurrent are
invisible to firmware and present only as a move timeout.

**GP4–GP7, GP16, GP18, and GP0 are now allocated** (I²C0, buzzer, LED) —
not reserved for future expansion as earlier documentation stated. See
`agent.md` §2.7 for the current complete pin picture.

---

## Specification Audit

`validation-report.md` records an earlier full review of the specification
set: 18 findings, all fixed. `agent-md-discrepancy-report.md` records a
second, later review specifically covering the reed-switch-to-potentiometer
transition and everything discovered alongside it — read both for the full
history of what's changed and why.
