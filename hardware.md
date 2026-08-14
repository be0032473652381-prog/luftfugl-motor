# hardware.md — luftfugl hardware configuration

What is physically connected, and the electrical facts the firmware must
respect.

---

## Board

| | |
|---|---|
| MCU board | YD-RP2040 |
| Silicon | RP2040-B2 |
| Flash | 16 MB, Zbit zb25vq128 |
| System clock | 125 MHz |
| SWD adapter speed | 1000 kHz |

---

## Pin connections

| Pin | Connected to | Peripheral required | Direction | Idle state |
|-----|--------------|--------------------|-----------|------------|
| GP2 | TB6612 AIN1 | GPIO | out | low |
| GP3 | TB6612 AIN2 | GPIO | out | low |
| GP4 | I²C SDA, three devices | I2C0 | bidir | pulled up |
| GP5 | I²C SCL, three devices | I2C0 | bidir | pulled up |
| GP6 | TB6612 BIN1 | PWM slice 3 channel A | out | low |
| GP7 | TB6612 BIN2 | PWM slice 3 channel B | out | low |
| GP8 | CM1106SL-NS EN | GPIO | out | low |
| GP9 | LED load switch enable | GPIO | out | low |
| GP10 | CM1106SL-NS RDY | GPIO | in | high, active low |
| GP11 | PCF8563T INT | GPIO | in | high, active low, 10 kΩ external pull-up |
| GP14 | TB6612 PWMA | PWM slice 7 channel A | out | low |
| GP15 | TB6612 STBY | GPIO | out | low, 10 kΩ external pull-down |
| GP16 | TB6612 PWMB | GPIO | out | low |
| GP18 | SK6812RGBWW DIN | PIO | out | low |
| GP20 | Debug probe UART RX | UART1 TX | out | — |
| GP21 | Debug probe UART TX | UART1 RX | in | — |
| GP26 | Potentiometer wiper | ADC0 | in | — |

Unconnected: GP0, GP1, GP12, GP13, GP17, GP19, GP22, GP27, GP28.

GP6 and GP7 share one PWM slice. GP7 requires inverted output polarity.

---

## I²C bus

Single bus, three devices, 4.7 kΩ pull-ups to 3.3 V.

| Device | Address (7-bit) | Max clock | Notes |
|--------|-----------------|-----------|-------|
| CM1106SL-NS | 0x34 | 100 kHz | uses clock stretching |
| INA219 | 0x40 | 400 kHz | |
| PCF8563T | 0x51 | 400 kHz | |

Bus speed limited to 100 kHz by the CO₂ sensor.

---

## Motor driver — TB6612FNG

Channel A drives the motor. Channel B drives the buzzer.

| STBY | AIN1 | AIN2 | PWMA | Output |
|------|------|------|------|--------|
| low | x | x | x | high impedance |
| high | 0 | 0 | x | coast |
| high | 1 | 1 | x | short brake |
| high | 1 | 0 | duty | forward |
| high | 0 | 1 | duty | reverse |

Duty is applied directly on PWMA; there is no inverted-duty scheme on this part.

VM 5 V, VCC 3.3 V. Motor on AO1/AO2, buzzer on BO1/BO2.

---

## Position sensor

360° continuous potentiometer, 10 kΩ, linear taper.

- 3.3 V and AG across the track, wiper to GP26
- 100 nF from wiper to AG, fitted at the MCU
- Full rotation spans the complete ADC range, 0–4095

Measured station positions:

| Station | ADC | Angle |
|---------|-----|-------|
| 1 | 200 | 17.6° |
| 2 | 525 | 46.2° |
| 3 | 850 | 74.7° |
| 4 | 1175 | 103.3° |
| 5 | 1500 | 131.9° |

Spacing 325 counts. Scale 11.375 counts per degree.

The mechanism has no travel limits and no end-stops. The full 0–4095 range is
reachable and valid.

---

## Motor characteristics

Measured on this rig:

| | |
|---|---|
| Lowest duty producing motion | 25 of 255 |
| Travel at duty 25 | ~690 counts in 150 ms |
| Travel duty | 30 |
| Approach duty | 25 |
| Creep duty | 25 |
| PWM frequency | 5 kHz nominal |

Approach and creep are at the stiction threshold, so the mechanism cannot be
driven more slowly than it approaches.

At 5 kHz the closest achievable frequency with an 8-bit wrap is 4998.4 Hz.

---

## Buzzer

Passive, driven differentially between BO1 and BO2 on TB6612 channel B.
Antiphase drive on BIN1/BIN2 doubles the voltage swing.

| | |
|---|---|
| Resonant frequency | 2.7 kHz |
| PWMB | high while sounding |

---

## Status LED — SK6812RGBWW

| | |
|---|---|
| Data | GP18 |
| Supply | 3.3 V, switched by GP9 |
| Devices in chain | 1 |
| Data format | **32 bits, G-R-B-W order** |
| Bit timing | 800 kHz, 0.3 µs / 0.6 µs nominal |
| Time to accept data after power-up | 300 µs |
| Current with all channels off | ~1 mA |

The white channel is warm white. A non-zero white value desaturates the colour
and shifts it toward yellow.

| Station | R | G | B | W |
|---------|---|---|---|---|
| 1 | 0 | 224 | 24 | 0 |
| 2 | 96 | 144 | 8 | 0 |
| 3 | 160 | 48 | 0 | 0 |
| 4 | 160 | 24 | 48 | 0 |
| 5 | 192 | 4 | 8 | 0 |

| Condition | R | G | B | W |
|-----------|---|---|---|---|
| Battery warning | 255 | 180 | 0 | 0 |
| CO₂ alarm | 255 | 0 | 0 | 0 |

---

## CO₂ sensor — CM1106SL-NS

I²C mode; COMSEL tied to GND. VBB and VDDIO permanently on 3.3 V. DVCC unused.

EN high starts a measurement. RDY falls when the result is ready.

| Phase | Duration |
|-------|----------|
| Preheat | 500 ms |
| Light source | 100 ms |
| Calculation | 100 ms |
| Communication | 30 ms |
| Total | 730 ms |

| | |
|---|---|
| Current while measuring | 6.1 mA |
| Current with EN low | negligible |
| EEPROM write time | up to 25 ms |
| ABC auto-calibration | disabled |

Power must not be removed during an EEPROM write.

---

## Current monitor — INA219

High-side, between the reverse-protection diode and the buck-boost input, so it
measures battery voltage and battery current.

| | |
|---|---|
| Shunt | 0.1 Ω, 1% |
| Shunt voltage LSB | 10 µV |
| Resulting current resolution | 100 µA |
| Bus voltage LSB | 4 mV |
| Conversion time, 12-bit | 532 µs |
| Current while converting | 0.6 mA |
| Current powered down | 6 µA |

Supply from 3.3 V so its I²C lines match the bus. VIN+, VIN− and VS see the
battery rail.

Maximum expected load is the motor. Range must cover motor stall.

---

## Real-time clock — PCF8563T

Provides the wake interrupt for RP2040 DORMANT mode. INT is open drain, active
low, with a 10 kΩ external pull-up.

Countdown timer registers:

| Register | Contents |
|----------|----------|
| 0x01 | Control/status 2, contains the timer interrupt enable and the timer flag |
| 0x0E | Timer control, contains the timer enable and the clock source select |
| 0x0F | Countdown value, 1–255 |

Clock source options:

| Source | Range with an 8-bit countdown |
|--------|-------------------------------|
| 4096 Hz | 244 µs – 62 ms |
| 64 Hz | 16 ms – 4 s |
| 1 Hz | 1 – 255 s |
| 1/60 Hz | 1 – 255 minutes |

The timer reloads automatically. INT remains asserted until the timer flag is
cleared.

| | |
|---|---|
| Supply current | 250 nA |
| Crystal accuracy | ±20 ppm |
| Backup cell and trickle resistor | not fitted |

---

## Power

| Rail | Source | Loads |
|------|--------|-------|
| VBAT | 4× AA alkaline through an ideal diode | shunt, buck-boost |
| 5 V | TPS630702 | TB6612 VM |
| 3.3 V | YD-RP2040 regulator | TB6612 VCC, INA219, CM1106SL-NS, PCF8563T, SK6812RGBWW (switched), potentiometer, I²C pull-ups |
| GND | common | |
| AG | YD-RP2040 analog ground | potentiometer return, wiper filter capacitor |

| Battery | |
|---|---|
| Cells | 4× AA alkaline |
| Fresh | 6.4 V |
| Discharged | 4.0 V |
| Capacity | 2500 mAh |

Sleep currents:

| Item | |
|------|---|
| RP2040 DORMANT | 180 µA |
| Buck-boost quiescent | 25 µA |
| INA219 powered down | 6 µA |
| CO₂ sensor, EN low | 2.5 µA |
| TB6612 standby | 1 µA |
| PCF8563T | 0.25 µA |
| SK6812RGBWW, switch off | 0 |

---

## Console

| | |
|---|---|
| Interface | UART1 |
| TX | GP20 to probe RX |
| RX | GP21 from probe TX |
| Baud | 115200, 8N1 |
| Flow control | none |
| Ground | not connected; common through SWD |

---

## Debug

SWDIO and SWCLK are dedicated RP2040 package pins, available on the board's
4-pin header alongside GND and 3V3. The probe's 3V3 is not connected.

---

## Wake sources

RP2040 DORMANT stops all clocks. The only wake source available is a GPIO
transition. The PCF8563T INT line on GP11 is the wake source.

The hardware watchdog cannot be serviced while clocks are stopped.
