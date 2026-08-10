# luftfugl — Motor & Position Control Firmware

RP2040 firmware that drives an N20 gearmotor to any of five fixed positions,
sensed by a reed-switch resistor ladder on a single ADC pin, with a plain-text
UART console for control and diagnostics.

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
| MCU | YD-RP2040 "Ultimate Pico", 16 MB flash, USB-C |
| Driver | TB6612FNG breakout, channel A |
| Motor | N20 DC gearmotor with magnet on the output |
| Sensing | 5 reed switches → resistor ladder → GP26 (ADC0) |
| Supplies | VM +5 V, VCC +3.3 V |
| Console | UART0 on GP0/GP1, 115200 8N1 |

| RP2040 | Function |
|--------|----------|
| GP0 / GP1 | UART0 TX / RX |
| GP2 / GP3 | AIN1 / AIN2 (direction) |
| GP14 | PWMA (speed) |
| GP15 | STBY (driver enable) |
| GP26 (`A0`) | SENSE (position) |

Full wiring, BOM and grounding notes: **[`hardware.md`](hardware.md)**.

---

## Documentation

| File | Contents |
|------|----------|
| **[`agent.md`](agent.md)** | Behavioural specification — what the firmware must do. Pin assignments, ADC bands, state machine, timing, protocol, build config. The authoritative spec. |
| **[`hardware.md`](hardware.md)** | What gets physically built. BOM, wiring tables, ladder schematic, power tree, pre-power checklist, bench measurement log, troubleshooting. |
| **[`function-description.md`](function-description.md)** | How the code is organised. Every module, every public function, its contract and execution context. Invariants and test hooks. |

Read them in that order. `agent.md` is the source of truth; if the code and the
spec disagree, the spec is right until it's deliberately changed.

---

## Repository Layout

```
luftfugl-motor/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── boards/
│   └── luftfugl_rp2040.h    # 16 MB flash, UART0 on GP0/GP1
├── src/
│   ├── main.c               # init, main loop, 1 kHz timer
│   ├── config.h             # every tunable constant
│   ├── motor.c/.h           # TB6612FNG abstraction
│   ├── encoder.c/.h         # ADC, filter, band classify, debounce
│   ├── controller.c/.h      # state machine, limits, timeouts
│   └── console.c/.h         # UART parser, event queue
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

Build `Debug` for bring-up — breakpoints and symbols are worth far more than
the code size during commissioning.

## Flashing

Over SWD with the Debug Probe:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 5000" \
        -c "program build/luftfugl.elf verify reset exit"
```

Fallback: hold BOOTSEL, plug in USB-C, drag `luftfugl.uf2` onto the mass
storage device.

Confirm the flash size once after the first flash:

```sh
picotool info
```

It should report 16 MB. If it doesn't, fix `PICO_FLASH_SIZE_BYTES` in
`boards/luftfugl_rp2040.h` before going further.

## Console

```sh
minicom -b 115200 -o -D /dev/ttyACM0
```

The Debug Probe presents SWD and UART as separate USB devices; the UART is
usually the higher-numbered `ttyACM`.

## Debugging

`.vscode/launch.json` is configured for cortex-debug with OpenOCD. Note that
**halting at a breakpoint does not stop the motor** — the PWM hardware keeps
running with the CPU stopped. Prefer breaking from an idle state, or brake
first.

---

## Command Reference

Plain text, `\n` or `\r\n` terminated, case-insensitive.

| Command | Response |
|---------|----------|
| `pos` | `POS:1` … `POS:5`, or `POS:?` between reeds |
| `move N` | `OK: moving to N` |
| `stop` | `OK: stopped` |
| `home` | `OK: homing` |
| `status` | `POS:N DIR:FWD\|REV\|STP SPD:0-255 STATE:<name>` |

Unsolicited during a move:

| Message | Meaning |
|---------|---------|
| `PASS:N` | Crossed reed N in transit |
| `ARR:N` | Position N confirmed, motor stopped |

Errors: `ERR: invalid target`, `ERR: at end-stop`, `ERR: busy`,
`ERR: position unknown`, `ERR: fault`, `ERR: timeout`,
`ERR: unknown command`, `ERR: line too long`.

```
> pos
POS:2
> move 5
OK: moving to 5
PASS:3
PASS:4
ARR:5
> move 6
ERR: at end-stop
```

---

## How It Works

**Sensing.** A 10 kΩ pull-up feeds a node tapped by five reed branches, each
switching a different resistor to ground. One reed closes at a time, so the ADC
reading identifies the position. All reeds open reads near full scale, which is
"unknown", not a position.

| Position | Resistor | Nominal ADC | Band |
|----------|----------|-------------|------|
| 1 | 1.0 kΩ | 372 | 0 – 555 |
| 2 | 2.2 kΩ | 738 | 556 – 1023 |
| 3 | 4.7 kΩ | 1309 | 1024 – 1678 |
| 4 | 10 kΩ | 2047 | 1679 – 2431 |
| 5 | 22 kΩ | 2815 | 2432 – 3455 |
| unknown | open | 4095 | 3456 – 4095 |

Bands are midpoints between nominal values. Verify against measured readings
during bring-up and update `config.h` if the ladder is out of tolerance.

**Filtering.** Sampled at 1 kHz into a 5-deep rolling average, classified into
a band, and confirmed once the classification holds for 12 ms. Transit
reporting uses the unconfirmed value, because at full speed a reed may not
dwell 12 ms.

**Moving.** Travel runs at full duty until one position from the target, then
drops to 30 % — or to creep speed if the target is 1 or 5. Arrival applies a
short brake immediately; the mechanism never coasts to a stop.

**Recovering.** If the position becomes unknown mid-operation, the motor creeps
until a valid reed is found. Direction is chosen by proximity to a limit, not
by travel history — creeping outward from reed 5 is what tears the harness.

**Failing.** A move that overruns its deadline brakes and homes. If homing also
overruns, the driver is disabled and the system faults until `home` or reset.

---

## Tuning

Everything adjustable lives in `src/config.h`. The values shipped are
estimates; `hardware.md` §10 is a measurement table where each row feeds one of
them.

| Constant | Default | Set from |
|----------|---------|----------|
| `DUTY_NORMAL` | 200 | Travel speed that still allows reed detection |
| `DUTY_APPROACH` | 60 | Overshoot observed at position 3 |
| `DUTY_CREEP` | 50 | Slowest smooth motion |
| `DUTY_MIN` | 45 | Lowest duty that breaks stiction |
| `TIMEOUT_STEP_MS` | 1500 | ~2× measured single-step time |
| `TIMEOUT_HOME_MS` | 6000 | ~2× measured full travel at creep |
| `BAND_*_MAX` | see above | Measured ADC at each position |

Keep the timeouts as tight as measurement allows. With no physical stops, a
timeout is the only thing that halts a runaway home sequence.

---

## Bring-Up Order

Do not jump to the full state machine. Stages 1–3 with the motor **uncoupled**.

1. UART only — banner prints, STBY low, no motor power
2. ADC only — rotate by hand, log readings at all five positions
3. Motor open-loop, uncoupled — direction, `DUTY_MIN`, brake vs coast, stall current
4. Single step, coupled, from position 3 — measure step time
5. Approach to a limit — watch for overshoot past reeds 1 and 5
6. Recovery and homing — park between reeds deliberately, test both limit cases

Detail in `agent.md` §13. Pre-power electrical checklist in `hardware.md` §9.

---

## Status

Specification complete; implementation pending. Open items before the firmware
can be considered commissioned:

- [ ] N20 stall current measured at 5 V, confirmed under 1.2 A
- [ ] Bulk capacitor fitted across VM
- [ ] ADC readings measured at all five positions, bands confirmed or adjusted
- [ ] Duty constants tuned on the bench
- [ ] `TIMEOUT_STEP_MS` and `TIMEOUT_HOME_MS` set from measured times
- [ ] Recovery direction verified from just outside reed 1 and just outside reed 5
- [ ] `picotool info` confirms 16 MB flash

---

## Notes

The TB6612FNG has no fault output. Thermal shutdown and overcurrent are
invisible to firmware and will present only as a move timeout. If the system
faults repeatedly for no apparent reason, measure VM current before suspecting
the state machine.

GP4–GP13 and GP16–GP28 are reserved for planned expansion — SD card, audio DAC,
VC-02 voice module, LEDs. See `agent.md` §9 before allocating any of them.

---

## Specification Audit

`validation-report.md` records a full review of the specification set and the
Codex prompt: 18 findings, all fixed. Nine were build-blocking or
safety-relevant, including a missing `pico_sdk_init()`, a stdio/raw-UART
conflict on the console, an absent watchdog, and a floating STBY line before
firmware runs.

`agent.md` §15 resolves every point that was previously under-specified. Check
there before concluding something is missing from the spec.
