# Agent: Aura luftfugl Motor & Position Control Firmware

## Scope

RP2040 firmware controlling an N20 DC gearmotor with a 5-position reed-switch
encoder, a **TB6612FNG** dual H-bridge (channel A only), and a UART0 debug
monitor. Built with the **Raspberry Pi Pico SDK (C/C++)**. Flashed and debugged
over SWD using a Raspberry Pi Debug Probe; the same probe carries the UART0
console.

---

## 1. Target Hardware

| Item | Value |
|------|-------|
| MCU board | YD-RP2040 "Ultimate Pico", purple, USB-C, BOOTSEL button |
| Flash | **16 MB** (128 Mbit, marking `25FQ128`) |
| System clock | 125 MHz (SDK default) |
| Motor | N20 DC gearmotor, permanent magnet on output for reed sensing |
| Driver | TB6612FNG breakout (red, SparkFun-style silkscreen) |
| VM (motor supply) | **+5 V** (device range 2.5–13.5 V) |
| VCC (logic supply) | **+3.3 V** (device range 2.7–5.5 V) |
| Driver current | 1.2 A continuous per channel, 3.2 A peak |
| Encoder | 5 reed switches, resistor ladder to a single ADC input |
| Debug / flash | Raspberry Pi Debug Probe — SWD for flashing, UART for console |

### 1.1 Driver Identification

Earlier revisions of this document specified a DRV8833. **The part in use is a
TB6612FNG**, confirmed by chip marking and by the silkscreen: `PWMA`, `PWMB`,
`STBY` and a `VCC` separate from `VM` exist only on the TB6612FNG. The DRV8833
has none of them.

This is what makes the VM = 5 V / VCC = 3.3 V wiring correct. On a DRV8833 that
same pin position is `VINT`, a regulator *output*, and feeding it 3.3 V would
risk damaging the chip.

Practical consequences of the TB6612FNG versus the DRV8833:

- Direction and speed are on **separate pins**. AIN1/AIN2 set direction as
  plain GPIO; PWMA sets speed. No inverted-duty trick is needed.
- Braking is inherent: when PWMA is LOW, the output is **short brake**, not
  coast. Low duty therefore gives naturally damped, controllable slow motion,
  which suits the approach-to-limit behaviour in §3.1.
- **There is no fault output.** The TB6612FNG has internal thermal shutdown but
  no way to report it. Timeouts (§6.9) are the only fault detection available.

### 1.2 Current Headroom

The N20's stall current must stay under 1.2 A continuous. Most N20 variants at
5 V stall between 0.6 A and 1.1 A depending on gear ratio. Since this mechanism
has no physical stops, a stall should never occur in normal operation — but
measure it once before trusting that.

Add a bulk capacitor (100 µF electrolytic or larger) across VM and GND close to
the module. The small onboard capacitors are not sufficient for motor inrush,
and supply dips during acceleration will show up as ADC noise on the sense
line.

### 1.4 Verified Silicon and Flash

Confirmed from photographs of the actual hardware:

| Marking | Part |
|---------|------|
| `RP2-B2 22/07 PHN284.00` | RP2040, **B2 stepping** |
| `25FQ128GSSIG` (date code 2537) | SPI NOR flash, **128 Mbit = 16 MB** |
| `TB717A3 6612FNG` | Toshiba TB6612FNG |

The `128` in the flash marking is standard JEDEC SPI NOR naming for 128 Mbit.
This is a **16 MB** part, not 8 MB. `PICO_FLASH_SIZE_BYTES` is set accordingly
in §11 — verify with `picotool info` after the first flash before doing
anything else.

Because the silicon is B2, `PICO_RP2040_B0_SUPPORTED` is set to 0. Leaving it
at 1 is harmless but pulls in B0 errata workarounds this board does not need.

### 1.3 Board Pin Labels

The YD-RP2040 silkscreen numbers GPIOs directly (`0`–`25`) and labels the ADC
pins `A0`–`A3` (= GP26–GP29). The pins used by this subsystem are labelled:

`0`, `1`, `2`, `3`, `14`, `15`, and `A0`.

The board also carries a dedicated 4-pin debug header marked
**`3V3` / `GND` / `SWCLK` / `SWIO`** on its left edge. SWD is *not* on
GP24/GP25 — those are ordinary GPIOs broken out on the bottom row.

---

## 2. Pin Assignments

### 2.1 Motor Driver — TB6612FNG, Channel A

| Signal | RP2040 Pin | Function | Notes |
|--------|------------|----------|-------|
| AIN1 | GP2 | GPIO out | Direction bit 1 |
| AIN2 | GP3 | GPIO out | Direction bit 2 |
| PWMA | GP14 | PWM slice 7, chan A | Speed |
| STBY | GP15 | GPIO out | **Must be driven HIGH before any motor operation.** LOW = standby, all outputs Hi-Z |

AIN1, AIN2 and STBY are plain digital outputs — they do not need PWM-capable
pins. Only PWMA does.

### 2.2 Full Module Wiring

| Module pin | Connect to |
|------------|------------|
| PWMA | GP14 |
| AIN1 | GP2 |
| AIN2 | GP3 |
| STBY | GP15 |
| VM | +5 V |
| VCC | +3.3 V |
| GND (all) | Common ground with the RP2040 board |
| AO1 | Motor terminal 1 |
| AO2 | Motor terminal 2 |
| BIN1, BIN2, PWMB | **Tie to GND** — unused channel, do not leave floating |
| BO1, BO2 | Leave unconnected |

STBY has no pull-up on the breakout. It must be actively driven HIGH by
firmware; on reset the RP2040 pin defaults to input, so the driver stays safely
in standby until initialisation completes.

If the motor runs backwards relative to the position numbering, swap AO1 and
AO2 rather than inverting the logic in firmware.

### 2.3 TB6612FNG Truth Table (channel A)

| STBY | AIN1 | AIN2 | PWMA | Output |
|------|------|------|------|--------|
| L | x | x | x | **Standby** (Hi-Z) |
| H | H | H | x | **Short brake** |
| H | H | L | H | Forward drive |
| H | H | L | L | Short brake |
| H | L | H | H | Reverse drive |
| H | L | H | L | Short brake |
| H | L | L | H | **Stop** (Hi-Z, coast) |

Two things follow from this table and both matter:

1. **`AIN1 = AIN2 = 0` is coast, not brake.** The original revision of this
   document called it "stop / brake". It is a coast. Because gearbox inertia
   causes overshoot and this mechanism has no physical stops (§3), every
   arrival at a target must use short brake (`AIN1 = AIN2 = 1`), never coast.
2. **The PWM off-phase is a short brake**, so PWM here is inherently slow
   decay. This is why the TB6612FNG holds low speeds well and why
   `DUTY_CREEP` is viable.

### 2.4 Drive Scheme

Let `d` be the requested duty, 0–255. The duty is written **directly** — no
inversion.

| Command | STBY | AIN1 | AIN2 | PWMA |
|---------|------|------|------|------|
| Forward at duty `d` | 1 | 1 | 0 | `d` |
| Reverse at duty `d` | 1 | 0 | 1 | `d` |
| Brake | 1 | 1 | 1 | 255 |
| Coast | 1 | 0 | 0 | 255 |
| Disabled | 0 | 0 | 0 | 0 |

Implement this as a single `motor_drive(dir, duty)` function plus
`motor_brake()`, `motor_coast()` and `motor_disable()`. No other code touches
GP2, GP3, GP14 or GP15.

Coast is used only when deliberately freeing the mechanism; normal operation
never coasts.

### 2.5 PWM Configuration

| Parameter | Value |
|-----------|-------|
| Target frequency | 5 kHz |
| Wrap | 255 (gives the 0–255 duty range used by `status`) |
| Clock divider | 97.6875 |
| Resulting frequency | 4998.4 Hz |

`125 000 000 / (97.6875 x 256) = 4998.4 Hz`. The exact value 97.65625 is not
representable in the RP2040's 8.4 fixed-point divider; 97.6875 is the nearest
and lands within 0.04 % of 5 kHz. The TB6612FNG accepts PWM up to 100 kHz, so
5 kHz is comfortably inside spec.

### 2.6 Speed Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `DUTY_NORMAL` | 200 | Normal travel speed |
| `DUTY_APPROACH` | 60 | 30 % of normal — used one position from target |
| `DUTY_CREEP` | 50 | Homing, between-reeds recovery, and all approaches to positions 1 and 5 |
| `DUTY_MIN` | 45 | Below this the N20 will not break stiction |

All four are tunable constants in `config.h`. They are starting points and must
be verified on the bench at 5 V VM.

### 2.7 Position Encoder

| Signal | RP2040 Pin | Board label | Notes |
|--------|------------|-------------|-------|
| SENSE | GP26 (ADC0) | `A0` | 12-bit ADC input |

**Wiring to the motor assembly (3 wires):**

- 3.3 V -> 10 kOhm pull-up -> SENSE line
- SENSE line taps 5 parallel branches, each: reed switch -> resistor -> GND
- 100 nF ceramic capacitor: SENSE -> GND, placed at the RP2040 PCB end

The pull-up must come from the RP2040's 3.3 V rail, the same rail feeding VCC,
so that the ADC reference and the divider top rail track each other.

**Resistor ladder and ADC bands:**

Nominal reading = `4095 x R / (R + 10 kOhm)`. Band edges are the midpoints
between adjacent nominal values, which maximises tolerance to resistor spread
and ADC noise.

| Position | R to GND | Nominal ADC | Accepted band |
|----------|----------|-------------|---------------|
| 1 | 1.0 kOhm | 372 | 0 – 555 |
| 2 | 2.2 kOhm | 738 | 556 – 1023 |
| 3 | 4.7 kOhm | 1309 | 1024 – 1678 |
| 4 | 10 kOhm | 2047 | 1679 – 2431 |
| 5 | 22 kOhm | 2815 | 2432 – 3455 |
| Between reeds | open | 4095 | 3456 – 4095 |

The bands in the original revision of this document were arithmetically wrong:
position 4's nominal reading of 2047 fell inside the band assigned to position
5, and position 5's nominal 2815 fell in an undefined gap. Position 4 would
never have been detected. Use the table above.

A reading in the open band means the magnet is not over any reed switch. This
is an **invalid / unknown state, not a position**.

### 2.8 Sense-Line RC Behaviour

The 100 nF cap and the 10 kOhm pull-up form a low-pass filter whose time
constant depends on which reed is closed:

- Reed closed, position 1: 1.0k || 10k = 0.91 kOhm -> tau ~ 0.09 ms
- Reed closed, position 5: 22k || 10k = 6.9 kOhm -> tau ~ 0.69 ms
- All reeds open: 10 kOhm -> tau ~ 1.0 ms

Entering a reed settles quickly; leaving one takes up to ~5 ms to reach the
open band. This sets a floor on how fast the mechanism may traverse a reed and
still be seen. See §6.5.

### 2.9 Debug Monitor (UART0)

| Signal | RP2040 Pin | Notes |
|--------|------------|-------|
| TX | GP0 | To Debug Probe RX |
| RX | GP1 | From Debug Probe TX |

115200 baud, 8N1, no flow control. Ground must be common between the module
and the Debug Probe.

### 2.10 SWD

SWDIO and SWCLK are dedicated pins on the RP2040 package, **not GPIOs**. On the
YD-RP2040 they are brought out to the 4-pin header marked
`3V3 / GND / SWCLK / SWIO`. Connect `SWCLK`, `SWIO` and `GND` to the Debug
Probe's SWD connector. Do not connect `3V3` if the board is separately powered.
No firmware configuration is required for SWD; it is always available.

### 2.11 Pin Budget

Consumed by this subsystem: **GP0, GP1, GP2, GP3, GP14, GP15, GP26.**

All other pins remain reserved (§9).

---

## 3. Mechanical Constraints

**There are no physical end-stops.** This is the single most important
constraint in this document and it shapes several behaviours.

- 5 fixed positions along the travel path.
- The motor assembly has **wires attached to the moving part**. Continuous
  rotation is forbidden — it would twist and tear the harness.
- Valid travel is forward and reverse within a 180 degree arc. Positions 1
  through 5 lie within that arc.
- **Reed 1 and reed 5 are the limits, and they are detected in firmware only.**
  Nothing physically prevents the motor from driving past them.
- At Position 1 the motor may only move forward (toward 5).
- At Position 5 the motor may only move reverse (toward 1).
- Positions 2, 3 and 4 allow movement in either direction.
- There is **no wrap-around**. Moving from 5 to 1 means travelling back through
  4, 3, 2.
- Only one reed switch closes at a time.

### 3.1 Consequences of Having No Stops

These rules are not optional. Each one exists because overshooting reed 1 or
reed 5 damages the harness.

1. **Never drive outward from a limit.** Firmware must reject any command or
   internal transition that would move forward from position 5 or reverse from
   position 1, in any state including `RECOVER` and `HOMING`.
2. **Approach positions 1 and 5 at `DUTY_CREEP`, not `DUTY_APPROACH`.** The
   final step into a limit gets the slowest speed available.
3. **Brake on first detection at a limit.** When targeting 1 or 5, apply short
   brake on the *first* in-band sample rather than waiting for the 12 ms
   confirmation. Confirm afterwards, then emit `ARR:N`. Everywhere else,
   confirm first (§6.5).
4. **Recovery direction must be limit-aware, not history-aware.** See §6.3.
   This is the rule most likely to be implemented wrongly, and getting it wrong
   drives the mechanism further past the limit.
5. **Homing runs at `DUTY_CREEP` for its entire duration**, never at
   `DUTY_NORMAL`.
6. **Bench-test homing with the motor uncoupled from the mechanism first.**

---

## 4. State Machine

| State | Meaning |
|-------|---------|
| `BOOT` | Initial ADC read and classification |
| `IDLE` | Stationary at a confirmed position, motor braked |
| `MOVING` | Travelling at `DUTY_NORMAL`, more than one position from target |
| `APPROACH` | Travelling at `DUTY_APPROACH` (or `DUTY_CREEP` if the target is 1 or 5), one position from target |
| `HOMING` | Creeping in reverse toward position 1 at `DUTY_CREEP` |
| `RECOVER` | Between reeds during normal operation, creeping to regain a valid position |
| `FAULT` | Motor disabled (STBY LOW), awaiting `home` or reset |

Transitions are driven by the 1 kHz control tick, never by blocking waits.

---

## 5. Boot / Reset Behaviour

1. Configure GPIO, PWM, ADC and UART0. Leave STBY LOW and PWMA at 0 throughout
   this step so the driver never glitches the motor during init.
2. Print a banner: `luftfugl motor fw <version>`.
3. Set AIN1 = AIN2 = 0, then raise STBY HIGH.
4. Fill the 5-deep sample buffer (5 ms), then classify.
5. If the position is valid (1–5), enter `IDLE` at that position, apply brake,
   and report `ARR:N`. Do not move.
6. If the reading is in the open band, enter `HOMING`: creep **reverse** at
   `DUTY_CREEP` until position 1 is confirmed.
7. If homing exceeds `TIMEOUT_HOME_MS`, brake, drop STBY LOW, emit
   `ERR: fault home timeout`, and enter `FAULT`.

Step 6 carries real risk: if the mechanism booted already parked past reed 1,
reverse homing drives it further out. `TIMEOUT_HOME_MS` is the only protection.
Set it from measured full-travel time and no larger.

`last_known_position` and `last_direction` live in RAM only and do not survive
reset. On boot, `last_direction` initialises to `REV`.

---

## 6. Runtime Behaviours

### 6.1 No zero position
A reading in the open band (>= 3456) is invalid. Never report it as a position.
`pos` returns `POS:?`.

### 6.2 Limit enforcement
Validate every move target against the current position *before* energising the
motor. Reject anything outside 1–5. At position 1 reject reverse commands; at
position 5 reject forward commands. Enforcement is entirely in firmware — there
is no stop to catch a mistake.

### 6.3 Between-positions recovery

If the reading enters the open band during normal operation, enter `RECOVER`
and creep at `DUTY_CREEP`. The direction is chosen as follows, **in this
order**:

| Last confirmed position | Creep direction | Reason |
|-------------------------|-----------------|--------|
| 1 | **FORWARD** | Reverse would drive further past the lower limit |
| 5 | **REVERSE** | Forward would drive further past the upper limit |
| 2, 3, 4 | `last_direction` | Safe to continue as before |
| none (boot) | REVERSE, via `HOMING` | §5 |

The original specification said to creep in the last known direction
unconditionally. With no physical stops, that rule tears the harness: leaving
reed 5 in the forward direction would be met with further forward creep. The
limit check overrides direction history.

Bound recovery by `TIMEOUT_RECOVER_MS`; on expiry, brake and enter `FAULT`.

### 6.4 Direction selection
`target > current` -> forward. `target < current` -> reverse.
`target == current` -> no motion (§8).

### 6.5 Sampling, filtering and debounce

- Sample ADC0 every **1 ms** from a repeating timer.
- Maintain a **5-deep rolling average** of raw 12-bit samples.
- Classify the averaged value into a band (§2.7) on every tick.
- A position becomes **confirmed** when the same band classification persists
  continuously for **12 ms**. Compare *band identity*, not raw ADC values —
  raw values will never repeat exactly.
- Confirmation is required for: `ARR:N`, `pos`, arrival detection, and homing
  completion.
- **Exception at the limits:** when the target is 1 or 5, brake on the first
  in-band sample, then confirm (§3.1 rule 3).
- **Transit detection (`PASS:N`) uses a lighter rule**: a single classification
  that is a valid position and differs from the previous reported band. It must
  not require the 12 ms hold, because at `DUTY_NORMAL` a reed may not dwell
  that long.

**Implied speed limit:** for arrival to be detected, the target reed must stay
closed for at least 12 ms plus the ~5 ms filter settling time. `DUTY_APPROACH`
and `DUTY_CREEP` exist to guarantee this. If arrivals are ever missed on the
bench, lower those before touching the debounce window.

### 6.6 Coast compensation
When the motor is one position from the target, drop to `DUTY_APPROACH` — or to
`DUTY_CREEP` if the target is position 1 or 5. On detection of the target reed,
apply **short brake (AIN1 = AIN2 = 1)** immediately. Hold brake for
`BRAKE_HOLD_MS` (default 100 ms), then remain braked while `IDLE`. Do not
coast, and do not drop STBY as a way of stopping — that releases the motor
into a free spin.

### 6.7 ADC configuration
12-bit, ADC0 on GP26, `adc_select_input(0)`.

### 6.8 Non-blocking operation
No `sleep_ms()` in any control or command path. UART parsing is polled or
interrupt-driven in the main loop; sampling and control run from a 1 kHz
repeating timer. Neither may block the other.

### 6.9 Timeouts

| Constant | Value | Scope |
|----------|-------|-------|
| `TIMEOUT_STEP_MS` | **1500 ms** | Per position step of a move |
| `TIMEOUT_HOME_MS` | 6000 ms | Full home sequence (4 steps x 1500 ms) |
| `TIMEOUT_RECOVER_MS` | 2000 ms | Between-reeds recovery |

A move to a target `k` positions away is allowed `k x TIMEOUT_STEP_MS`.

**On move timeout:** brake, emit `ERR: timeout`, then automatically initiate a
home sequence to position 1.
**If that home sequence also times out:** brake, drop STBY LOW, emit
`ERR: fault home timeout`, enter `FAULT`. Only a `home` command or a reset
leaves `FAULT`. This two-stage rule prevents an endless retry loop.

Because homing runs at `DUTY_CREEP`, verify that 6000 ms is actually enough for
full travel at creep speed before trusting it. If creep is slower than a
quarter of normal speed, raise `TIMEOUT_HOME_MS` to match measured reality —
but keep it as tight as the measurement allows, since it is the only guard
against runaway homing.

### 6.10 Absence of driver fault reporting
The TB6612FNG has internal thermal shutdown and overcurrent protection, but
**no fault output pin**. Firmware cannot detect a driver fault directly. A
thermal shutdown will present as a move timeout, which §6.9 already handles by
homing and then faulting. If the mechanism ever faults repeatedly for no
apparent reason, suspect thermal shutdown and check VM current draw.

---

## 7. Debug Monitor Command Protocol

Plain text, terminated by `\n` or `\r\n`. Commands are case-insensitive.
Responses are plain text, each terminated by `\r\n`.

| Command | Response | Description |
|---------|----------|-------------|
| `pos` | `POS:1` … `POS:5` or `POS:?` | Current confirmed position |
| `adc` | `ADC raw=<n> avg=<n> pos=<n or ?>` | Report raw and filtered ADC and the classified position. Diagnostic and read-only; commands no motion. |
| `jog <±counts>` | `OK: jog <±n> from <adc>` | Move by a bounded signed ADC delta at creep speed |
| `setpos <1-5>` | `OK: pos <n> = <adc>` | Set one station nominal to the current filtered ADC in RAM |
| `savepos` | Five paste-ready `#define POS_n_ADC <adc>` lines | Report the current station table; does not write flash |
| `move N` | see §8 | Move to position N (1–5) |
| `stop` | `OK: stopped` | Immediate brake, abandon target |
| `status` | `POS:N DIR:FWD\|REV\|STP SPD:0-255 STATE:<state>` | Full state dump |
| `home` | `OK: homing` | Home sequence to position 1 |

Unsolicited progress messages during an active move:

- `PASS:N` — the motor crossed reed N in transit
- `ARR:N` — position N confirmed and the motor has stopped

`status` reports `POS:?` when between reeds and `SPD:0` when braked.

---

## 8. Protocol Edge Cases

| Situation | Response |
|-----------|----------|
| Unrecognised command | `ERR: unknown command` |
| `move` with no argument or a non-numeric argument | `ERR: invalid target` |
| `move N`, N outside 1–5 | `ERR: invalid target` |
| `move N` where N equals the current position | `OK: already at N` — no motion |
| `move N` while a move is already active | `ERR: busy` — issue `stop` first |
| `move` while position is `?` | `ERR: position unknown` — issue `home` first |
| `move` while in `FAULT` | `ERR: fault` |
| At position 1, `move 0` or below | `ERR: at end-stop` |
| At position 5, `move 6` or above | `ERR: at end-stop` |
| `stop` when already stopped | `OK: stopped` |
| Input line exceeds 32 characters | `ERR: line too long`, discard to next newline |
| Empty line | No response |
| `adc` in any state | Report the current raw ADC, filtered ADC and instant classification; do not change state or motor outputs |
| `jog` delta is zero, malformed, or exceeds `JOG_MAX_COUNTS` | `ERR: invalid jog` |
| `jog` endpoint is outside the safe ADC range | `ERR: at end-stop` |
| `jog` starts outside the safe ADC range | `ERR: overtravel` |
| `jog` while another motion is active | `ERR: busy` |
| `jog` while in `FAULT` | `ERR: fault` |
| `jog` exceeds `JOG_TIMEOUT_MS` | Brake and emit `ERR: timeout` |
| `jog` leaves the safe ADC range | Brake, disable the driver, emit `ERR: overtravel`, and enter `FAULT` |
| `setpos` would make the station table non-ascending or leave `POS_WINDOW` at least one quarter of the smallest gap | `ERR: invalid target`; table unchanged |
| `setpos` while motion is active or in `FAULT` | `ERR: busy` or `ERR: fault`; table unchanged |

The motor must always come to rest on a **valid position 1–5**, and only ever
between positions 1 and 5 inclusive. It must never be left parked between
reeds; any operation that ends between reeds triggers `RECOVER` (§6.3).

---

## 9. Reserved Pins (DO NOT USE)

Reserved for future expansion (SD card, audio DAC, VC-02 voice module, LEDs):

| Bus | Reserved Pins |
|-----|---------------|
| SPI0 | GP4–GP7, GP16–GP21 |
| SPI1 | GP8–GP13 |
| I2C0 | GP4/GP5, GP8/GP9, GP16/GP17 |
| I2C1 | GP6/GP7, GP10/GP11, GP18/GP19 |
| UART1 | GP4/GP5, GP8/GP9, GP20/GP21 |
| I2S | GP16–GP22 |
| ADC1 / ADC2 | GP27, GP28 (board labels `A1`, `A2`) |

(The original revision listed GP18–GP19 under SPI1; on the RP2040 those are
SPI0 pins. Corrected above.)

The YD-RP2040 also uses GP23 for an onboard WS2812 RGB LED and GP25 for the
user LED on most variants. Confirm before assigning either.

---

## 10. Project Structure

```
luftfugl-motor/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── boards/
│   └── luftfugl_rp2040.h
├── src/
│   ├── main.c          # init, main loop, 1 kHz timer
│   ├── config.h        # all pins, bands, durations, duty constants
│   ├── motor.c/.h      # TB6612FNG drive, brake, coast, standby
│   ├── encoder.c/.h    # ADC sampling, rolling average, band classify, debounce
│   ├── controller.c/.h # state machine, limit enforcement, timeouts
│   └── console.c/.h    # UART0 line buffer, parser, responses
└── .vscode/
    └── launch.json
```

Every magic number in this document belongs in `config.h` and nowhere else.

---

## 11. Build Configuration

Before writing a custom board header, check whether the SDK already ships one:

```sh
ls $PICO_SDK_PATH/src/boards/include/boards/ | grep -i yd
```

If `yd_rp2040.h` exists, use `set(PICO_BOARD yd_rp2040)` and verify its flash
size matches 16 MB. Otherwise create `boards/luftfugl_rp2040.h`:

```c
#ifndef _BOARDS_LUFTFUGL_RP2040_H
#define _BOARDS_LUFTFUGL_RP2040_H

#define PICO_DEFAULT_UART             0
#define PICO_DEFAULT_UART_TX_PIN      0
#define PICO_DEFAULT_UART_RX_PIN      1
#define PICO_FLASH_SPI_CLKDIV         2
#define PICO_FLASH_SIZE_BYTES         (16 * 1024 * 1024)
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#define PICO_RP2040_B0_SUPPORTED      0   // silicon is B2, see §1.4

#endif
```

`pico_sdk_import.cmake` is **not written by hand**. Copy it verbatim:

```sh
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
```

Complete `CMakeLists.txt`. The ordering is load-bearing: `PICO_BOARD` and
`PICO_BOARD_HEADER_DIRS` must be set *before* `pico_sdk_import.cmake` is
included, and `pico_sdk_init()` must be called *after* `project()`. Getting
either wrong produces confusing failures about a missing board header or
undefined SDK targets.

```cmake
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD luftfugl_rp2040)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

project(luftfugl C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

option(LUFTFUGL_DEBUG "Build interactive debug monitor" ON)

add_executable(luftfugl
    src/main.c
    src/motor.c
    src/encoder.c
    src/controller.c
    src/console.c
)

if(LUFTFUGL_DEBUG)
    target_sources(luftfugl PRIVATE src/debug.c)
    target_compile_definitions(luftfugl PRIVATE LUFTFUGL_DEBUG=1)
endif()

target_include_directories(luftfugl PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(luftfugl PRIVATE -Wall -Wextra)

target_link_libraries(luftfugl
    pico_stdlib
    hardware_pwm
    hardware_adc
    hardware_timer
    hardware_watchdog
)

# Console is a raw UART0 driver, not SDK stdio. See §15.3.
pico_enable_stdio_uart(luftfugl 0)
pico_enable_stdio_usb(luftfugl 0)

pico_add_extra_outputs(luftfugl)
```

Build:

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j4
```

`PICO_BOOT_STAGE2_CHOOSE_W25Q080` is the generic stage-2 loader and works with
W25Q-compatible 128 Mbit parts. Confirm with `picotool info` after
the first flash that the reported flash size is 16 MB.

---

## 12. Flashing and Debugging

Flash over SWD with the Debug Probe:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 5000" \
        -c "program build/luftfugl.elf verify reset exit"
```

Console (same probe, separate USB serial device):

```sh
minicom -b 115200 -o -D /dev/ttyACM0
```

Interactive debugging — `.vscode/launch.json` using cortex-debug:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug luftfugl (Pico Probe)",
      "type": "cortex-debug",
      "request": "launch",
      "cwd": "${workspaceRoot}",
      "executable": "${workspaceRoot}/build/luftfugl.elf",
      "servertype": "openocd",
      "gdbPath": "gdb-multiarch",
      "device": "RP2040",
      "configFiles": ["interface/cmsis-dap.cfg", "target/rp2040.cfg"],
      "openOCDLaunchCommands": ["adapter speed 5000"],
      "svdFile": "${env:PICO_SDK_PATH}/src/rp2040/hardware_regs/rp2040.svd",
      "runToEntryPoint": "main"
    }
  ]
}
```

Halting at a breakpoint leaves the motor in whatever state it was in — the PWM
hardware keeps running with the CPU stopped. When setting breakpoints in the
control path, prefer breaking only from `IDLE`, or add a debug command that
brakes before the breakpoint is reachable.

The BOOTSEL button and USB-C port remain available as a fallback flashing path
(drag-and-drop UF2) if SWD is not working.

---

## 13. Bring-Up Order

Do not attempt the full state machine first. Because there are no physical
stops, stages 1–3 must be done with the motor **uncoupled from the mechanism**.

1. **UART only** — banner prints, `pos` echoes something. STBY held LOW, no
   motor power.
2. **ADC only** — motor unpowered, rotate the mechanism by hand, log raw and
   averaged ADC values at each of the 5 positions. Compare against §2.7 and
   adjust the bands to the measured values if the resistors are out of
   tolerance. This stage also confirms reed 1 and reed 5 are reliably
   detectable, which the whole limit scheme depends on.
3. **Motor open-loop, uncoupled** — verify direction mapping (swap AO1/AO2 if
   reversed), that `DUTY_MIN` actually turns the motor at 5 V, that
   `DUTY_CREEP` turns it smoothly, and that short brake stops it noticeably
   faster than coast. Measure stall current against the 1.2 A limit.
4. **Closed-loop single step, coupled, starting from position 3** — move one
   position each way and measure the time. Confirm 1500 ms is comfortably
   adequate; adjust if not.
5. **Approach to a limit** — `move 1` from position 2, then `move 5` from
   position 4. Watch for overshoot past the reed. Lower `DUTY_CREEP` if the
   mechanism coasts past.
6. **Recovery and homing** — deliberately park between reeds and verify
   `RECOVER` picks the correct direction, especially from just outside reed 1
   and just outside reed 5. Test homing last.

---

## 14. Open Items

- N20 stall current measured at 5 V against the 1.2 A channel rating. (§1.2)
- Bulk capacitor fitted across VM. (§1.2)
- Duty constants and `TIMEOUT_HOME_MS` pending bench measurement. (§2.6, §6.9)
- Confirm `picotool info` reports 16 MB after first flash. (§11)

---

## 15. Resolved Ambiguities

Each item below was under-specified in earlier revisions and would have forced
an implementer to guess. The resolutions are binding.

### 15.1 Firmware version string

```c
#define FW_VERSION "1.0.0"
```

Boot banner, emitted once from `console_init()`:

```
luftfugl motor fw 1.0.0
```

### 15.2 State names in the `status` response

`STATE:` carries the bare name without the `ST_` prefix, uppercase:

`BOOT`, `IDLE`, `MOVING`, `APPROACH`, `HOMING`, `RECOVER`, `FAULT`, `DEBUG`

Example: `POS:3 DIR:STP SPD:0 STATE:IDLE`

### 15.3 Raw UART, not SDK stdio

The console is a **raw UART0 driver** using `uart_init()`, `uart_is_readable()`
and `uart_putc_raw()`. SDK stdio is disabled in CMake for both UART and USB.

Both mechanisms configure UART0; enabling stdio *and* driving the peripheral
directly is a conflict. Raw UART is chosen because `console_poll()` must be
strictly non-blocking, and because the event queue already handles output
buffering. `stdio_init_all()` must not appear anywhere in the project.

This supersedes the `stdio_init_all()` line shown in
`function-description.md` §8 (now corrected).

### 15.4 `stop` while between reeds

`stop` is always accepted and always brakes immediately — an emergency stop
that refused to act would be worse than useless.

If the position is `POS_UNKNOWN` when the motor comes to rest, the controller
enters `ST_IDLE` and emits:

```
OK: stopped
POS:?
```

It does **not** automatically re-enter `ST_RECOVER`. An operator who pressed
stop wants the motor stopped, not creeping again a moment later. The rule in §8
that the motor must come to rest on a valid position applies to *completed
operations*, not to a commanded emergency stop. Recovery from this state
requires an explicit `home`.

### 15.5 `PASS:N` semantics

- Emitted only in `ST_MOVING` and `ST_APPROACH`, never during homing or
  recovery.
- Fires when `encoder_instant()` returns a valid position differing from the
  last position reported by any means.
- The open band is not a reportable position and never produces `PASS`.
- The target position does **not** produce a `PASS` — it produces `ARR`.

### 15.6 `BRAKE_HOLD_MS`

Defines a window after arrival during which the controller ignores new `REQ_MOVE`
requests, answering `ERR: busy`. It exists to let the mechanism settle before
another move starts. The brake itself remains applied indefinitely afterwards;
the constant bounds the *lockout*, not the braking.

### 15.7 `home` while a move is active

Accepted. Brakes, discards the current target, and begins homing. `home` is the
documented recovery path and must never be refused outside that.

### 15.8 Watchdog

The RP2040 hardware watchdog is **required**, not optional.

```c
watchdog_enable(100, true);      // 100 ms, pause on debug halt
```

Kicked once per tick from `controller_tick()`, never from the main loop — a
main loop that stalls is survivable, a dead control tick with the motor
energised is not.

`watchdog_caused_reboot()` is checked at the top of `main()`. If true, the boot
sequence emits `ERR: watchdog reset` before proceeding normally. The normal boot
path already leaves the motor braked and STBY low until initialisation
completes, so a watchdog reset is safe by construction.

The `pause_on_debug` argument must be `true`, or halting at a breakpoint would
reset the chip.

### 15.9 Deadline arithmetic

Deadlines use `to_ms_since_boot(get_absolute_time())`, stored as `uint32_t`.
Compare with subtraction, never with `>`:

```c
if (deadline_ms && (int32_t)(now_ms - deadline_ms) >= 0) { /* expired */ }
```

This is correct across the 32-bit wrap at ~49 days. A naive `now > deadline`
comparison is not.

### 15.10 ADC pin initialisation

`encoder_init()` calls `adc_init()`, then `adc_gpio_init(PIN_SENSE)` to disable
the pin's digital input and pull resistors, then `adc_select_input(ADC_CHANNEL)`.
Omitting `adc_gpio_init()` leaves the digital input buffer enabled and adds
noise.

### 15.11 `TIMEOUT_RECOVER_MS` risk

2000 ms at `DUTY_CREEP` may be marginal. Creep is roughly a quarter of normal
speed, and a single step at normal speed is expected around 400 ms — so a worst
case recovery from just past a reed could approach the limit. Measure this
during bring-up (`hardware.md` §10) and raise the constant if recoveries fault
spuriously. Do not raise it pre-emptively: it is the guard against creeping
past a limit.
