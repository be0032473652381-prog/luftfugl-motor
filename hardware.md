# hardware.md — luftfugl-motor hardware configuration

Source: schematic PDF, rev 007, dated 26 Aug 2026 (`AURA LuftFugl
670.kicad_sch` per the title block — the "670" naming appears to be an
internal file-numbering quirk; the "Rev: 007" field is treated as
authoritative). HW/SW design: Henrik Andersen. Mechanical design:
Jurgen. This document describes the hardware exactly as shown on this
schematic — no revision history, no comparison against earlier designs.

Read from the schematic's flattened PDF text export, not the raw
`.kicad_sch` — no KiCad parser was available. Exact GPIO-to-net-label
association is reconstructed from proximity in the extracted text where
possible; wire color-to-pin mapping for the physical harness is not
extractable this way and is marked unconfirmed below rather than
guessed. One specific pin-mapping tension is flagged explicitly in §4
rather than silently resolved either way.

---

## §1. System overview

A wall-mounted air-quality indicator. An N20 DC gearmotor with an
integrated 4.7 kΩ potentiometer drives a physical needle to one of six
positions — five indicating CO₂ severity, a sixth reserved for
calibration/warning/error/wait states — with an addressable RGBW LED
providing color confirmation, a secondary discrete LED, and a buzzer
providing an audible chirp count matching the indicated station. A
DS3231 real-time clock provides a wake/interval reference, an ambient
light sensor supports display-brightness adaptation, and an INA219
monitors battery voltage with a documented low-battery alert. Power
comes from 3× AA alkaline cells through a buck-boost regulator.

---

## §2. Bill of materials

| Ref | Part | Function |
|---|---|---|
| U1 | LM74700 | Ideal diode, reverse polarity protection |
| U2 | XL63070 | 3.3 V buck-boost regulator |
| U3 | INA219 | Current/voltage monitor, I²C address `0x40` |
| U4 | TB6612FNG | Dual motor driver — channel A: motor, channel B: buzzer |
| U5 | YD-RP2040 dev board, 4 MB flash | MCU |
| U6 | SCD41-D-R2 | CO₂ sensor, I²C address `0x62` |
| U7 | TPS22918 | Load switch |
| U8 | VEML7700 (labeled "WENI7700"/"WENl7700" on this schematic — OCR artifact, this is the VEML7700) | Ambient light sensor, I²C address `0x10` |
| U9 | DS3231 + AT24C32 module | RTC (I²C `0x68`) + EEPROM (I²C `0x57`) |
| M1 | N20 DC motor, integrated 4.7 kΩ potentiometer | Position actuator + feedback |
| D1 | SK6812RGBWW | Addressable RGBW LED, primary CO₂-severity display |
| D2 | LED | Secondary discrete indicator, driven via `R11` from GP25 |
| BZ1 | Buzzer | Audible alert |
| BT1 | 3× AA alkaline | Battery, 4.5 V fresh, 3× 1.5 V nominal |

**U8 confirmed as a 5-pin module** — `VIN`/`3Vo`/`GND`/`SCL`/`SDA` — this
pin layout (including a separate `3Vo` regulated-output pin distinct
from `VIN`) matches the Adafruit-style breakout convention. Power via
`VIN`; `3Vo` is that board's own regulator output, not a power input —
leave unconnected unless deliberately tapping it for something else.

**N20 motor spec, per schematic note**: 3–3.5 V, 18–150 mA operating
range, worm gear, plastic construction.

**Harness cable spec, per schematic note**: motor wire, potentiometer
wire, and buzzer wire each 20 cm, AWG 28.

---

## §3. Power

VM and VCC are the same unified 3.3 V rail — no separate motor supply
voltage exists. The XL63070 buck-boost regulator produces this single
rail from the battery pack; the RP2040 dev board's own `Vin`/`Vout` pins
(39/40) are deliberately left unconnected, with power injected directly
at the board's `3V3` pin.

3× AA alkaline in series: 4.5 V fresh, per-cell nominal 1.5 V.

**Low battery limits, per schematic**:

| Level | Threshold |
|---|---|
| Warning | < 3.600 V |
| Critical | < 3.300 V |
| Buzzer alert | Fixed 3500 Hz tone pulses |

**Note**: the 3500 Hz figure on this schematic describes the low-battery
alert tone specifically. It has not been reconciled against whatever the
currently-implemented firmware value actually is for that specific
alert — confirm directly rather than assume either number is current.

---

## §4. RP2040 pin allocation

| Pin | Signal |
|---|---|
| GP0 | LED Power (TPS22918 enable, active HIGH = 3.3 V power to D1) |
| GP1 | SDC41 enable |
| GP2 | AIN1 (motor direction) |
| GP3 | AIN2 (motor direction) |
| GP4 | I²C0 SDA |
| GP5 | I²C0 SCL |
| GP6 | BIN1 (buzzer direction) |
| GP7 | BIN2 (buzzer direction) |
| GP8 | Console UART TX (Debug Probe TX, connector J10 pin 3, RED) |
| GP9 | Console UART RX (Debug Probe RX, connector J10 pin 1, YELLOW) |
| GP10 | CO2-LIMIT_AB (room-mode switch) |
| GP14 | PWMA (motor speed) |
| GP15 | STBY (motor driver enable) |
| GP16 | PWMB (buzzer) |
| GP17 | DS3231-Wake-UP (SQW alarm interrupt) |
| GP18 | LED data (D1, SK6812RGBWW) |
| GP22 | MOTOR_POT_POWER (switched supply to the position potentiometer) |
| GP25 | Onboard LED (drives D2 via `R11`, 470 Ω) |
| GP26 | ADC0 — potentiometer wiper |

**Confirmed by direct ohmmeter continuity measurement on physical
hardware — settled, not a schematic reading.** Console UART is `GP8`
(TX) / `GP9` (RX), not `GP20`/`GP21`. The debug-probe connector `J10`'s
own pin 2 (GND) is not connected — the actual ground reference for this
UART comes from `J11` pin 1 separately.

**Confirmed already fixed in firmware, not an open bug**: `config.h`
defines `PIN_UART_TX=8`/`PIN_UART_RX=9`, matching this ohmmeter
verification exactly. Both pins belong to the RP2040's `uart1`
peripheral (confirmed from the schematic's own pin-alternate-function
labels), the same instance `console.c` already initializes — no
additional peripheral-instance change was needed alongside the pin
numbers.

**GP20/GP21's actual status is not separately confirmed** — they are not
asserted as free here, only that the ohmmeter measurement did not
implicate them in the console UART specifically. Confirm directly if
their status matters for a future allocation.

I²C0 (GP4/GP5) is shared by the SCD41, INA219, DS3231/AT24C32 module,
and VEML7700 — five devices total on one bus, no address collision
(`0x10`, `0x40`, `0x57`, `0x62`, `0x68`, all distinct).

---

## §5. Position sensing

Single continuous 4.7 kΩ potentiometer, mechanically coupled to the N20
motor's output shaft, providing absolute analog angular position
feedback. Wiper on `ADC0 (GP26)`.

**Station table** — confirmed matching `config.h` exactly, resolving the
discrepancy an earlier revision of this document carried between the
schematic and firmware:

| Station | ADC | Angle |
|---|---|---|
| 1 | 200 | 17.6° |
| 2 | 611 | 53.7° |
| 3 | 1022 | 89.8° |
| 4 | 1433 | 126.0° |
| 5 | 1844 | 162.1° |
| 6 | 3000 | 263.7° |

Tolerance: ±20 ADC (≈1.76°).

**Station 6 (`EVENT_POS` — confirmed canonical name from `AGENTS.md`,
not `EVENT_POSITION`) is structurally different from stations 1–5** —
reserved for calibration, warning, error, or wait states, not a sixth
CO₂-severity level. **Confirmed as the actual firmware high limit**
(`HIGH_ENDSTOP_ADC` is defined as `POS_6_ADC` directly in `config.h`) —
not station 5, correcting an earlier assumption.

**Position sensing RC filter** — schematic-stated characteristics:

- Time constant: approximately 1.0–2.2 ms
- ~95% settled: 3–6.5 ms
- ~99% settled: 5–11 ms
- 1 ms ADC sampling, five-sample digital average, 12 ms position debounce

Component values: `R3 = 1 kΩ`, `C4 = 10 µF`.

I²C bus pull-ups: `R1 = 10 kΩ` (SCL), `R2 = 10 kΩ` (SDA).

---

## §6. Motor driver

TB6612FNG, channel A drives the motor (`AIN1`/`AIN2`/`PWMA`/`STBY`),
channel B drives the buzzer (`BIN1`/`BIN2`/`PWMB`).

---

## §7. Buzzer

Passive piezo buzzer (`BZ1`), driven differentially via TB6612 channel B.

**Confirmed resonance behavior**: two points, 2.0 kHz and 4.3 kHz.

**RC filter on the drive lines** — `R4 = 1.5 Ω`, `R5 = 1.5 Ω` (one in
each drive leg), `C8 = 3.3 µF` across the buzzer terminals.

**Schematic-stated filter cutoff: 16 kHz.**

**Buzzer function, per schematic** — chirp count tied to motor station:

| Station | Buzzer behavior |
|---|---|
| 1 | Silent |
| 2 | 1 bird chirp |
| 3 | 2 bird chirps |
| 4 | 3 bird chirps |
| 5 | 4 bird chirps |

---

## §8. LED indication

**D1 (SK6812RGBWW)** — primary CO₂-severity display, addressable RGBW,
data on GP18, power switched via GP0/TPS22918.

**D2 (plain LED)** — secondary indicator, driven directly from GP25
(this board variant's onboard LED pin) through `R11` (470 Ω)
current-limiting resistor. Purpose not stated on the schematic.

---

## §9. Ambient light sensing

VEML7700 (`U8`), I²C address `0x10`, on the shared I²C0 bus. Adafruit-
style 5-pin module (`VIN`/`3Vo`/`GND`/`SCL`/`SDA`) — power via `VIN`,
`3Vo` is that module's own regulated output, not an input. No dedicated
GPIO allocated for this sensor — I²C only.

---

## §10. CO₂ sensing and station mapping

SCD41 (`U6`), I²C address `0x62`, power-switched via `U7`/GP1
(`SDC41 enable`).

**Room-mode zones, selected by `CO2-LIMIT_AB` (GP10)**:

**[A] Living room (GP10 high)**

| Band | Range |
|---|---|
| Excellent | < 799 ppm |
| Good | 800–999 |
| Fair | 1,000–1,499 |
| Poor | 1,500–2,000 |
| Very Poor | > 2,000 |

**[B] Sleeping room (GP10 low)**

| Band | Range |
|---|---|
| Excellent | < 999 ppm |
| Good | 1,000–1,499 |
| Fair | 1,500–1,999 |
| Poor | 2,000–2,500 |
| Very Poor | > 2,500 |

Threshold source, per schematic: ASHRAE (2023), WHO (2021), REHVA
(Europe), OSHA (2023). Schematic notes this matters particularly for
infants, elderly, or respiratory patients.

**CO₂-to-station mapping**:

| CO₂ level | Motor station |
|---|---|
| Excellent | 1 |
| Good | 2 |
| Fair | 3 |
| Poor | 4 |
| Very Poor | 5 |

---

## §11. Real-time clock / wake reference

DS3231 + AT24C32 module (`U9`), I²C addresses `0x68` (RTC) and `0x57`
(EEPROM), on the shared I²C0 bus. `SQW` alarm output wired to GP17
(`DS3231-Wake-UP`).

---

## §12. Debug/programming interface

Standard SWD (`SWDIO`, `SWCLK`, `GND`) plus UART (`GP8` TX / `GP9` RX,
confirmed by ohmmeter — see §4), both via the Debug Probe connector.

---

## §13. Still open

- **Firmware pin mismatch — resolved, not open.** Confirmed:
  `config.h` already defines `PIN_UART_TX=8`/`PIN_UART_RX=9`, matching
  the ohmmeter-verified physical wiring exactly. Both GP8 and GP9
  belong to the RP2040's `uart1` peripheral (confirmed from the
  schematic's own pin labels), the same instance already used in
  `console.c` — so no peripheral-instance change was needed alongside
  the pin-number fix.
- **Battery-alert buzzer frequency** — schematic states 3500 Hz; not
  reconciled against current firmware implementation.
- **D2's purpose** — hardware exists, intended use not stated.
- **Wire-color-to-harness-pin mapping** — not extractable from flattened
  PDF text; needs the actual `.kicad_sch` file or a visual schematic.
- **LM74700 quiescent current** — needs datasheet.
