# AGENTS.md — luftfugl-motor

RP2040 firmware built with the Pico SDK and C11. It drives an N20 gearmotor
through a TB6612FNG and measures six calibrated positions with a switched
4.7 kohm potentiometer. Positions 1–5 represent CO2 levels; position 6 is the
special `EVENT_POS`. This file contains standing repository rules. The task
itself arrives in the prompt.

## Sources of truth — read before writing code

| File | Role |
|------|------|
| `agent.md` | Protocol, state machine, safety requirements, and original build specification |
| `function-description.md` | Module contracts and invariants |
| `hardware.md` | Original wiring, BOM, power tree, and grounding |
| `debug-functions.md` | Debug monitor behavior and safety interlocks |
| `src/config.h` | Implemented pins, timing, limits, and tunable values |

The hardware and behavior amendments recorded in this file describe the
current built firmware and override stale hardware facts in the older
specification documents. Otherwise, precedence is `agent.md`, then
`function-description.md`, then the remaining specification documents. If a
conflict is not resolved here or by the source, stop and ask.

## Current hardware and firmware facts

| Function | Current implementation |
|----------|------------------------|
| Board/flash | `vcc-gnd_yd-rp2040_4m`, 4 MiB external flash |
| Motor driver | AIN1 GP2, AIN2 GP3, PWMA GP14, STBY GP15 |
| Position input | Potentiometer wiper to ADC0/GP26 |
| Position power | GP22 directly supplies the 4.7 kohm potentiometer |
| Debug/production UART | raw UART1, TX GP8, RX GP9, 115200 8N1 |
| I2C0 | SDA GP4, SCL GP5, 100 kHz; shared by SCD41, INA219, and DS3231 |
| SCD41 profile input | GP10 |
| RGBW LED | GP0 directly supplies power; SK6812 data is GP18 |
| Buzzer | differential BIN1/BIN2 GP6/GP7, PWM GP16 |
| DS3231 | INT/SQW GP17; event indication uses onboard LED GP25 |

There is no TPS22918 LED load switch in the current hardware. GP0 is the
physical SK6812 supply: set GP0 high before sending a non-zero frame and low
after sending/latching an off frame. The PIO state machine is enabled only to
transmit a changed frame and is disabled after the latch interval.

GP22 must be configured as a high output as the first hardware initialization
action after every reset or power-on. No position sample, motor initialization,
or movement decision is valid before the potentiometer has power and its ADC
input has settled. GP22 remains high during the present awake-mode firmware
because the 1 kHz safety controller continuously needs valid position data.
`encoder_power_hold(false)` is reserved for a future sleep implementation;
after restoring power, code must wait for a valid settled sample before motion.

## Positions and safety

The mechanism has no physical end-stops. Position 1 is the low firmware limit
and position 6 is the high firmware limit. The moving part carries a wire
harness that can be torn by driving beyond either limit.

- Positions 1–5 are normal CO2 display/motor stations.
- Position 6 is named `EVENT_POS`; do not reintroduce `POS_ERROR`.
- Never drive reverse at position 1 or forward at position 6, including from
  debug code unless a deliberately guarded calibration operation requires it.
- Stopping is always a short brake (`AIN1 = AIN2 = 1`). Never coast and never
  use STBY as a normal stop.
- Limit approaches retain the creep-speed behavior and recovery direction must
  not depend on unsafe extrapolation or travel history.
- The watchdog is mandatory: 100 ms, `pause_on_debug = true`, and kicked only
  from `controller_tick()` in the 1 kHz timer IRQ.
- Preserve all applicable invariants in `function-description.md` section 9.

Current calibrated position values are maintained only in `src/config.h`.
The implemented nominal ADC values are 200, 611, 1022, 1433, 1844, and 3000
for positions 1 through 6 respectively.

## Current startup and runtime behavior

GP22 position-sensor power is initialized first. The firmware then initializes
the LED power control, raw UART console, motor, buzzer, debug monitor (debug
build only), ADC position sensing, RGBW LED, INA219, DS3231 event timer, SCD41,
and controller. It enables the motor driver only after those subsystems are
initialized.

When an SCD41 is available, the startup coordinator requires the mechanism at
position 6 (`EVENT_POS`). It requests a guarded move there if needed, holds the
normal motion interface locked, performs the 60-second SCD41 stabilization,
then collects seven accepted samples before normal CO2 control becomes valid.
The resulting CO2 level selects one of positions 1–5 and its LED indication.

The main loop services UART, queued events, SCD41 coordination, LED, battery,
DS3231, buzzer, CO2, and the debug monitor, then executes `__wfi()`. It normally
wakes on the 1 ms timer, UART, or GPIO interrupt instead of busy-spinning.

## DS3231 event timer

The DS3231 timer is one-shot and remains stopped after power-on, reset, or
expiry. Initialization restores the saved interval, disables Alarm1, and
enables the falling-edge interrupt on GP17. It never starts automatically.

- `ds3231 start` arms one event using the currently configured interval.
- `ds3231 stop` disables the active Alarm1 event.
- `ds3231 timer` reports the countdown or stopped state.
- `ds3231 temp` reports the DS3231 temperature.
- `ds3231 timeset <15..18000>` changes the interval in RAM. If the timer is
  active, it is immediately re-armed from the new interval.
- `ds3231 timeset <15..18000> /s` also saves the interval to flash.

At expiry, the GP17 interrupt is serviced, Alarm1 is disabled, and GP25 is lit
for five seconds. The event timer does not re-arm itself. The former event
buzzer and external RGBW blink are not part of DS3231 expiry behavior.

## LED and battery indications

Normal station colors use 3% brightness. Station 5 uses static rose/red.
Battery warning overlays a double orange flash
(`255,48,0`) at 30% brightness every 15 seconds. Battery critical uses the
orange double-hazard indication at 30%, requests position 6, and runs the
configured critical-battery buzzer sequence. Current default thresholds are
3.600 V warning and 3.300 V critical; saved settings may override defaults.

On a genuine arrival event, the bird-call indication plays once at Station 2,
twice at Station 3, three times at Station 4, and four times at Station 5.
Station 1 and position 6 do not produce an arrival bird call.

## Debug monitor

Debug builds contain seven pages. Page 6 lists commands. Page 7 is a 64-entry
data log displaying the newest 20 rows and records meaningful changes in motor
and controller state, INA219 voltage (initial reading, battery-state changes,
or at least the configured 50 mV hysteresis), battery state, LED power/color,
SCD41 state/frames, DS3231 state, buzzer state, and command results.

On Page 7, `q`/`Q` pauses logging, `s`/`S` resumes it, and `c`/`C` clears
the log and resumes logging. These single-key controls are page-local.

## Architecture rules

There are two execution contexts:

- **1 kHz timer IRQ:** position sampling, controller state transitions, motor
  writes, INA219 scheduling, and the watchdog kick.
- **Main loop:** UART I/O, text formatting, I2C state-machine service, RGBW LED,
  buzzer, DS3231, CO2 coordination, and debug-monitor work.

Control requests cross into the IRQ through the command mailbox and events
cross back through the event ring buffer. Read-only snapshot getters may be
used by main-context diagnostics. Do not print, block, perform floating-point
division, or call main-context services in the timer IRQ. Do not call
`motor_*()` from the main loop.

## Never do these

1. Do not modify `agent.md`, `hardware.md`, `function-description.md`, or
   `debug-functions.md`. They are specification inputs. Report stale or
   contradictory statements instead of silently rewriting them.
2. Do not invent constants. Tunable values belong in `src/config.h`; if a value
   is genuinely absent, stop and ask.
3. Do not change the production protocol in `agent.md` section 7. Debug console
   commands may be added when requested, and additions must be recorded in the
   commit message.
4. Do not call `stdio_init_all()`. The console is a raw UART1 driver; SDK UART
   and USB stdio are disabled for the firmware target.
5. Do not dynamically allocate memory.

Motion is permitted on the simulation rig during development and diagnostics,
but every issued motion command and its result must be reported.

## Code style

- C11 with `-Wall -Wextra`; fix project warnings rather than suppressing them.
  Report SDK-internal warnings without patching SDK sources.
- Static-qualify functions and objects not exported by a header.
- Use header guards, not `#pragma once`.
- Use `volatile` for every object crossing an IRQ boundary.
- Keep every tunable value in `src/config.h`.
- Comment why, especially recovery direction and arrival detection.

## Build and verification

Both configurations must build cleanly:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DLUFTFUGL_DEBUG=ON
cmake --build build-debug -j4

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DLUFTFUGL_DEBUG=OFF
cmake --build build-release -j4
```

`pico_sdk_import.cmake` must remain the verbatim SDK import file. Do not
hand-write it or reorder load-bearing CMake initialization.

## Flash and reset

After every successful firmware build, flash without requesting confirmation:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 1000" \
  -c "program build-debug/luftfugl.elf verify reset exit"
```

OpenOCD must identify a 4096 KiB flash device. `picotool info` is useful only
when the target is in BOOTSEL mode and is not the normal post-SWD verification
method.

An SWD reset does not reproduce the complete console presentation obtained by
a power cycle. After flashing, always perform the working console reset
sequence so the user receives a fresh debug menu:

```sh
sleep 1
stty -F /dev/ttyACM0 115200 cs8 -cstopb -parenb -ixon -ixoff
printf 'reset\r' > /dev/ttyACM0
```

## Reporting

State what was built, tested, flashed, and reset. Report all motion performed.
Support acceptance claims with command output or file-and-line references.
Identify any remaining divergence between implementation and specification.
