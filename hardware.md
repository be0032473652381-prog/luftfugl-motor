# Hardware: Aura luftfugl Motor & Position Control

Wiring, bill of materials, and bench-verification reference for the luftfugl
motor subsystem. Firmware behaviour is specified separately in `agent.md`;
this document covers only what is physically built and how to check it.

---

## 1. System Overview

```
   USB-C ──┐
           │
      ┌────┴──────────────┐         ┌──────────────────┐
      │  YD-RP2040        │  3.3V   │  TB6612FNG       │      ┌─────────┐
      │  (16 MB flash)     ├────────►│  breakout        │      │   N20   │
      │                   │         │                  │ AO1  │  motor  │
      │  GP2  ───────────►│ AIN1    │      channel A   ├─────►│  + gear │
      │  GP3  ───────────►│ AIN2    │                  │ AO2  │  + magnet
      │  GP14 ───────────►│ PWMA    │                  ├─────►│         │
      │  GP15 ───────────►│ STBY    │                  │      └────┬────┘
      │                   │         └────────▲─────────┘           │
      │                   │                  │ VM = +5 V           │
      │  GP26/A0 ◄────────┼──── SENSE ───────┼─────────────────────┘
      │                   │                  │      (reed ladder, 3 wires)
      │  GP0 TX ─────────►│                  │
      │  GP1 RX ◄─────────┤   ┌──────────────┴──┐
      │  SWCLK/SWIO ◄────►│   │  +5 V supply    │
      └───────────────────┘   └─────────────────┘
              ▲
              │  SWD + UART
       ┌──────┴────────┐
       │  RPi Debug    │
       │  Probe        │
       └───────────────┘
```

---

## 2. Bill of Materials

| # | Item | Spec | Qty | Notes |
|---|------|------|-----|-------|
| 1 | MCU board | YD-RP2040 "Ultimate Pico", 16 MB flash, USB-C | 1 | Purple board, BOOTSEL button |
| 2 | Motor driver | TB6612FNG breakout, SparkFun-style silkscreen | 1 | Channel A only; channel B unused |
| 3 | Motor | N20 DC gearmotor with permanent magnet on output | 1 | Stall current must be < 1.2 A at 5 V |
| 4 | Reed switches | SPST normally-open, glass envelope | 5 | One per position |
| 5 | Pull-up resistor | 10 kΩ, 1 % | 1 | SENSE to 3.3 V |
| 6 | Ladder resistors | 1.0 k, 2.2 k, 4.7 k, 10 k, 22 kΩ — **1 % metal film** | 1 each | See §5 |
| 7 | Filter capacitor | 100 nF ceramic, X7R | 1 | SENSE to AG, at the MCU end |
| 8 | Bulk capacitor | 100 µF or larger electrolytic, ≥ 10 V | 1 | Across VM / GND at the driver |
| 8a | STBY pull-down | 10 kΩ | 1 | STBY to GND — **see §7** |
| 9 | Supply | +5 V, ≥ 1.5 A | 1 | See §3 |
| 10 | Debug probe | Raspberry Pi Debug Probe | 1 | SWD + UART |
| 11 | Harness | 3-core to the motor assembly, plus 2-core motor leads | — | See §7 |

**Resistor tolerance matters.** The ADC bands in `agent.md` §2.7 are midpoints
between nominal values. 5 % resistors will still classify correctly, but 1 %
parts give substantially more margin at positions 4 and 5 where the bands are
closest in relative terms.

---

## 3. Power Tree

| Rail | Source | Feeds |
|------|--------|-------|
| +5 V | External supply or board VBUS | TB6612FNG `VM` only |
| +3.3 V | YD-RP2040 onboard regulator, `3V3` pad | TB6612FNG `VCC`, encoder pull-up |
| GND | Common | Everything |
| AG | YD-RP2040 analog ground pad | Encoder ladder returns, 100 nF cap |

TB6612FNG rails: VM 2.5–13.5 V, VCC 2.7–5.5 V. The 5 V / 3.3 V split in use is
well inside both.

**On sourcing the 5 V:** taking VM from USB VBUS works for bench testing, but
motor current then flows through the same rail feeding the RP2040's regulator.
Under acceleration this shows up as ADC noise on the sense line. A separate 5 V
supply, with grounds joined at a single point near the MCU board, is
preferable — and necessary if position readings prove unstable while moving.

**Grounding.** Route the encoder ladder returns and the 100 nF capacitor to
`AG`, and the driver/motor ground to a `GND` pad. Join them at the board rather
than daisy-chaining motor ground through the sensor ground. Motor return
current sharing a conductor with the sense ground is the most likely source of
false position readings.

---

## 4. Connection Tables

### 4.1 RP2040 → TB6612FNG

| RP2040 pin | Board label | Module pin | Direction |
|------------|-------------|------------|-----------|
| GP2 | `2` | AIN1 | out |
| GP3 | `3` | AIN2 | out |
| GP14 | `14` | PWMA | out (PWM) |
| GP15 | `15` | STBY | out |
| 3.3 V | `3V3` | VCC | power |
| GND | `GND` | GND | power |

STBY has no pull-up on the breakout. It is driven HIGH by firmware after
initialisation; before that the RP2040 pin is an input and the driver stays
safely in standby.

### 4.2 TB6612FNG → Motor and Supply

| Module pin | Connect to |
|------------|------------|
| VM | +5 V |
| GND (all pins) | Common ground |
| AO1 | Motor terminal 1 |
| AO2 | Motor terminal 2 |
| BIN1, BIN2, PWMB | **Tie to GND** — unused channel, must not float |
| BO1, BO2 | Leave unconnected |

If the motor runs backwards relative to the position numbering, **swap AO1 and
AO2** rather than inverting the direction logic in firmware.

### 4.3 Encoder Harness → RP2040

Three conductors run to the motor assembly:

| Wire | Function |
|------|----------|
| 1 | +3.3 V — feeds the top of the ladder |
| 2 | SENSE — common node of all five reed branches |
| 3 | GND / AG — common return for all five resistors |

SENSE terminates at GP26 (`A0`).

### 4.4 Debug Probe

| Probe | YD-RP2040 |
|-------|-----------|
| SWD SWCLK | `SWCLK` (4-pin header) |
| SWD SWDIO | `SWIO` (4-pin header) |
| SWD GND | `GND` (4-pin header) |
| UART TX | `1` (GP1, RP2040 RX) |
| UART RX | `0` (GP0, RP2040 TX) |
| UART GND | any `GND` |

Do not connect the probe's 3.3 V if the board is separately powered.

SWDIO and SWCLK are dedicated RP2040 package pins, not GPIOs. GP24 and GP25 on
the bottom row are ordinary GPIOs and have nothing to do with SWD.

---

## 5. Position Encoder Ladder

```
        +3.3 V
          │
         ┌┴┐
         │ │ 10 kΩ  (pull-up, at the MCU end)
         └┬┘
          │
  SENSE ──┼────────────────────────────────► GP26 / A0
          │                          │
          │                        ──┴──  100 nF
          │                        ──┬──  (at the MCU end)
          │                          │
    ┌─────┼─────┬───────┬───────┬────┴──┐
    │     │     │       │       │       │
   ─┴─   ─┴─   ─┴─     ─┴─     ─┴─      │
   / /   / /   / /     / /     / /      │   reed switches
   ─┬─   ─┬─   ─┬─     ─┬─     ─┬─      │   (one closed at a time)
    │     │     │       │       │       │
   ┌┴┐   ┌┴┐   ┌┴┐     ┌┴┐     ┌┴┐      │
   │ │   │ │   │ │     │ │     │ │      │
   └┬┘   └┬┘   └┬┘     └┬┘     └┬┘      │
   1.0k  2.2k  4.7k    10k     22k      │
    │     │     │       │       │       │
    └─────┴─────┴───────┴───────┴───────┴──► AG
    P1    P2    P3      P4      P5
```

Nominal ADC reading = `4095 × R / (R + 10 kΩ)`.

| Position | R | Nominal ADC | Firmware band | Measured (fill in) |
|----------|---|-------------|---------------|--------------------|
| 1 | 1.0 kΩ | 372 | 0 – 555 | |
| 2 | 2.2 kΩ | 738 | 556 – 1023 | |
| 3 | 4.7 kΩ | 1309 | 1024 – 1678 | |
| 4 | 10 kΩ | 2047 | 1679 – 2431 | |
| 5 | 22 kΩ | 2815 | 2432 – 3455 | |
| Between | open | 4095 | 3456 – 4095 | |

Band edges are midpoints between adjacent nominal values. If measured values
differ from nominal by more than about 8 %, recompute the midpoints from the
measured column and update `config.h` rather than adjusting the hardware.

The 100 nF cap and the pull-up form a low-pass filter with a time constant that
varies from ~0.09 ms (position 1 closed) to ~1.0 ms (all reeds open). Leaving a
reed therefore takes roughly 5 ms to register. This is accounted for in the
firmware's debounce timing and sets an upper limit on traverse speed.

---

## 6. Pin Allocation

| RP2040 | Board label | Use |
|--------|-------------|-----|
| GP0 | `0` | UART0 TX → probe |
| GP1 | `1` | UART0 RX ← probe |
| GP2 | `2` | AIN1 |
| GP3 | `3` | AIN2 |
| GP14 | `14` | PWMA |
| GP15 | `15` | STBY |
| GP26 | `A0` | SENSE (ADC0) |

Everything else is reserved for future expansion — SD card, audio DAC, VC-02
voice module, LEDs. See `agent.md` §9 for the reserved bus map.

The YD-RP2040 additionally uses GP23 for an onboard WS2812 RGB LED and GP25 for
the user LED on most variants. Confirm before assigning either.

---

## 7. Assembly Notes

- **Cap placement.** The 100 nF goes at the *MCU* end of the harness, not at
  the motor assembly. Its job is to filter noise picked up along the cable.
- **Pull-up placement.** Also at the MCU end, off the same 3.3 V pad feeding
  VCC.
- **Cable routing.** Keep the 3-wire encoder harness away from the motor leads.
  If they must run together, twist the motor pair to cancel its field, and
  twist SENSE with its ground return.
- **Bulk capacitor.** Across VM and GND, physically close to the driver module.
  Observe polarity. Without it, motor inrush will dip the supply and corrupt
  ADC readings during acceleration.
- **STBY pull-down (10 kΩ to GND).** The breakout has no pull-down and GP15 is
  an input during reset, BOOTSEL and every moment before firmware runs. A
  floating STBY can read as HIGH, enabling the driver with undefined direction
  inputs. The resistor guarantees standby whenever the MCU is not actively
  driving the pin. Fit it — it is the cheapest safety part in the build.
- **Unused channel B.** BIN1, BIN2 and PWMB tied to GND. Floating CMOS inputs
  on an H-bridge are a real hazard, not a formality.
- **Harness strain relief.** The moving part carries wires. Anchor them so that
  travel flexes the cable rather than pulling on solder joints.

---

## 8. Hazards

**There are no physical end-stops on this mechanism.** Positions 1 and 5 are
detected in firmware only. Nothing prevents the motor from driving past them
and twisting the harness attached to the moving part until it tears.

This has direct consequences for bench work:

- Never run the motor open-loop while coupled to the mechanism.
- Never bypass the firmware limits to "just test the motor".
- Test homing and recovery with the motor uncoupled first.
- Keep the supply within reach during the first coupled tests.

Secondary hazards: the TB6612FNG has no fault output, so thermal shutdown and
overcurrent are invisible to firmware — they present only as a move timeout.
And the driver module gets warm under sustained stall; do not leave a stalled
motor energised while investigating.

---

## 9. Pre-Power Checklist

Work through this with the supply disconnected.

- [ ] Continuity: every RP2040 pin in §6 reaches its intended module pin
- [ ] No short between +5 V and GND at the driver
- [ ] No short between +3.3 V and GND at the driver or at the ladder
- [ ] No short between +5 V and +3.3 V
- [ ] VM is on the 5 V rail and VCC on the 3.3 V rail — **not swapped**
- [ ] Bulk capacitor polarity correct
- [ ] BIN1, BIN2, PWMB tied to GND
- [ ] Grounds common between MCU board, driver, supply, and probe
- [ ] Encoder ladder: each reed measures its expected resistance to ground when
      manually closed
- [ ] Pull-up measures 10 kΩ from SENSE to the 3.3 V pad
- [ ] Motor uncoupled from the mechanism
- [ ] Debug probe SWD and UART wired, 3.3 V *not* connected

Then power up with the motor supply off, confirm the firmware banner on UART,
and only then apply VM.

---

## 10. Bench Measurements

Fill these in during bring-up (`agent.md` §13). Every value feeds a constant in
`config.h`.

| Measurement | Method | Value | Feeds |
|-------------|--------|-------|-------|
| ADC at each of 5 positions | Rotate by hand, log averaged reading | | Band table, §5 |
| ADC between reeds | Park between positions | | Open-band threshold |
| ADC ripple while moving | Log min/max during a traverse | | Band margin check |
| Minimum duty that turns the motor | Ramp duty until it moves | | `DUTY_MIN` |
| Duty giving smooth slow motion | Observe at creep speeds | | `DUTY_CREEP` |
| Single-step time at `DUTY_NORMAL` | Time position 3 → 4 | | `TIMEOUT_STEP_MS` |
| Full travel time at `DUTY_CREEP` | Time position 5 → 1 homing | | `TIMEOUT_HOME_MS` |
| Overshoot past target at `DUTY_APPROACH` | Observe at position 3 | | `DUTY_APPROACH` |
| Free-running current at 5 V | Inline ammeter on VM | | Supply sizing |
| Stall current at 5 V | Brief stall, ammeter on VM | | Must be < 1.2 A |

---

## 11. Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Motor never moves, no UART errors | STBY not driven HIGH, or VM not connected |
| Motor moves only one direction | AIN1/AIN2 swapped or one not connected |
| Motor runs backwards | Swap AO1 and AO2 |
| Motor jitters or hums but does not turn | Duty below `DUTY_MIN`, or VM sagging under load |
| Position always reads `?` | SENSE open, pull-up missing, or ladder ground not connected |
| Position 4 reads as 5 | Old band table in use — see `agent.md` §2.7 |
| Readings unstable only while moving | Motor current sharing the sense ground, or missing bulk cap |
| Readings drift with temperature | 5 % ladder resistors; move to 1 % |
| `PASS:N` never fires in transit | Traverse too fast for the RC filter — lower `DUTY_NORMAL` |
| Arrivals overshoot the reed | `DUTY_APPROACH` too high, or firmware coasting instead of braking |
| Repeated unexplained faults | TB6612FNG thermal shutdown — check VM current draw |
| SWD not detected | Probe 3.3 V connected while board separately powered, or SWCLK/SWIO swapped |
