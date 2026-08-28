# hardware.md — luftfugl-motor hardware configuration

Source: `AURA_LuftFugl_4_0.kicad_sch` / `.pdf`, rev 004, dated 26 Aug 2026.
HW/SW design: Henrik Andersen. Mechanical design: Jurgen. This supersedes
every prior version of this document in this project.

**A significant prior-documentation conflict was found and resolved
during this rewrite** — see §0 below before reading anything else.

Read from the schematic's PDF export across four revisions (001, 2.0, 2.1,
2.2, 4.0), not the raw `.kicad_sch` — no KiCad parser was available. Wire
color-to-pin mapping for the harness remains unconfirmed for the same
reason; marked below rather than guessed.

---

## §0. Confirmed directly from source: continuous potentiometer, not reed switches

`encoder.c` and `config.h` were read directly (uploaded, not fetched — GitHub's
file-tree view blocks automated access, three attempted paths all failed).
This settles it definitively, not by inference:

`position_at()` in `encoder.c` computes `delta = value > nominal ? value -
nominal : nominal - value`, checked against `CFG_POS_WINDOW` — an
unambiguous continuous-ADC-value-vs-five-targets comparison. No reed
switches, no resistor ladder, no discrete band classification anywhere in
this file. The GitHub README's reed-switch description is confirmed stale,
not just suspected — matching four independent schematic revisions.

**The README is stale on more than one point, not only this one** — its
documented duty-cycle constants (`DUTY_NORMAL=200`, etc.) also don't match
`config.h`'s actual values (`DUTY_NORMAL=50`, etc.). Treat the README as
historical/aspirational throughout, not as a source of current fact for
anything; `config.h`, `encoder.c`, and the schematic are ground truth.

**Console UART confirmed as GP20/GP21** (`PIN_UART_TX 20`, `PIN_UART_RX
21`) — not the README's GP0/GP1, not `sdc41`'s GP8/GP9. This matches this
project's own established fix for the earlier adjacent-pin TX/RX coupling
bug.

### A real discrepancy the source reveals — needs your decision, not mine

| Station | Code (`config.h`) | Schematic (confirmed) | Match |
|---|---|---|---|
| 1 | 200 | 200 | Yes |
| 2 | 525 | 611 | **No — off by 86** |
| 3 | 850 | 1022 | **No — off by 172** |
| 4 | 1250 | 1433 | **No — off by 183** |
| 5 | 1844 | 1844 | Yes |

The schematic's angles are perfectly evenly spaced at 36.1° per station;
the code's are not — this pattern suggests `config.h` holds earlier
bench-measured values from before the final mechanical design was settled,
while the schematic reflects the finished, deliberate geometric
calibration. **Which one is actually correct depends on whether the
physical mechanism changed since those code values were measured — that's
not something to be inferred from documents, it needs your direct
knowledge or a fresh bench measurement.** Do not silently pick one; this
needs resolving before Phase 1 work builds on top of either table.

---

## §1. Corrections confirmed across schematic revisions

| | Earlier documentation | Confirmed on schematic |
|---|---|---|
| Position sensing | 5 reed switches, resistor ladder | **Single continuous 4.7 kΩ potentiometer**, integrated into the motor, 5 calibrated stations |
| Console UART pins | GP0/GP1 (README) | Not GP0/GP1 — see §4, adjacent-pin coupling already a known risk in this project |
| Battery | 4× AA (early assumption); briefly misread as 3× D-cell mid-review | **3× AA alkaline, 4.5 V fresh / 3.0 V discharged**, stated explicitly on schematic |
| Buck-boost regulator | TPS630702 | **XL63070** |
| RP2040 board | Assumed bare-chip custom PCB at one point mid-review | **YD-RP2040 dev board**, 4 MB flash — a different flash-size variant from the separate physical unit used for `sdc41` bench testing (that unit measured 16 MB; not a contradiction, different boards) |
| RP2040 power | — | **Vin/Vout pins (39/40) deliberately unconnected** — power injected directly at the `3V3` pin, bypassing the dev board's own onboard regulator entirely |
| LED part | Cycled through `WS2812B` / typo'd `SK6818RGBWW` | **SK6812RGBWW, confirmed**, 4-channel RGBW, 32-bit protocol |
| Station 4 ADC | Schematic once read `433` (breaks ascending-order constraint) | **Confirmed `1433`** — evenly spaced at 411 counts with every other station |
| SCD41 I²C address | Assumed from datasheet | **Confirmed on schematic: `0x62`** |
| INA219 I²C address | Assumed from datasheet | **Confirmed on schematic: `0x40`** |

---

## §2. Bill of materials

| Ref | Part | Function |
|---|---|---|
| U1 | LM74700 | Ideal diode, reverse polarity protection |
| U2 | XL63070 | 3.3 V buck-boost regulator |
| U3 | INA219 | Current/voltage monitor, I²C, `0x40` |
| U4 | TB6612FNG | Dual motor driver |
| U5 | YD-RP2040 dev board, 4 MB flash | MCU |
| U6 | SCD41-D-R2 | CO₂ sensor, I²C, `0x62` |
| U7, U8 | TPS22918 ×2 | Load switches — CO₂ sensor, LED |
| D1 | SK6812RGBWW | Addressable RGBW LED |
| M1 | N20 DC motor, integrated 4.7 kΩ potentiometer | Position actuator + absolute analogue position feedback |
| BZ1 | Buzzer | Alarm |
| BT1 | 3× AA alkaline | Battery, 4.5 V fresh / 3.0 V discharged |
| SW1A, SW2A | ON-OFF | Power-path switches (exact roles not fully resolved from extracted text) |
| SW3A | ON-OFF | Serves both `CO2-LIMIT_AB` (room mode) and `Sound ON-OFF` — confirm which physical switch does which |

**N20 motor electrical spec, per schematic note**: 3–3.5 V, 18–150 mA
operating range, worm gear, plastic construction.

**Harness cable spec, per schematic note**: motor wire, potentiometer
wire, and buzzer wire each **20 cm, AWG 28**.

---

## §3. Position sensing — the confirmed design

Single continuous potentiometer, 4.7 kΩ, mechanically coupled to the N20's
output shaft — absolute analogue angular position feedback, not discrete
switch states.

| Station | ADC | Angle |
|---|---|---|
| 1 | 200 | 17.6° |
| 2 | 611 | 53.7° |
| 3 | 1022 | 89.8° |
| 4 | 1433 | 126.0° |
| 5 | 1844 | 162.1° |

Tolerance: ±20 ADC (≈1.76°). Ascending, evenly spaced at 411 counts —
consistent with the firmware's own ascending-order validation constraint.

Wiper on `ADC0 (GP26)`. **Discrepancy worth confirming, not silently
resolved**: earlier project guidance (before this final schematic existed)
recommended a 4.7 kΩ series resistor + 100 nF RC filter on the wiper
signal. This schematic shows `R3 = 100 Ω` near the potentiometer
connector, alongside `C4 = 10 µF` and `C5 = 100 nF` — not the 4.7 kΩ the
earlier guidance specified. Flattened PDF text can't confirm `R3` is
definitely in series with the wiper signal rather than serving some other
nearby role (same limitation as the harness wire-color mapping) — but the
proximity makes it likely enough to flag rather than ignore. **Confirm via
the `.kicad_sch` netlist or visual schematic before assuming either value
is what's actually filtering the ADC input** — the RC time constant
affects real signal settling time at the 1 kHz sample rate, so this isn't
cosmetic.

**I²C bus pull-ups confirmed**: `R1 = 10 kΩ` on SCL, `R2 = 10 kΩ` on SDA
(near the `J3`/`J4` breakout connectors). External pull-ups are present —
firmware enabling the RP2040's own internal I²C pull-ups as well would be
redundant, not harmful, but worth knowing they're not required.

---

## §4. RP2040 pin/peripheral reference

Full mapping from the schematic symbol, useful for allocating pins in the
two phases below without re-deriving it each time:

| Pin | GPIO | Alternate functions |
|---|---|---|
| 1 | GP0 | UART0_TX / I2C0_SDA / SPI0_RX |
| 2 | GP1 | UART0_RX / I2C0_SCL / SPI0_CSn |
| 4 | GP2 | I2C1_SDA / SPI0_SCK |
| 5 | GP3 | I2C1_SCL / SPI0_TX |
| 6 | GP4 | UART1_TX / I2C0_SDA / SPI0_RX |
| 7 | GP5 | UART1_RX / I2C0_SCL / SPI0_CSn |
| 9 | GP6 | I2C1_SDA / SPI0_SCK |
| 10 | GP7 | I2C1_SCL / SPI0_TX |
| 11 | GP8 | UART1_TX / I2C0_SDA / SPI1_RX |
| 12 | GP9 | UART1_RX / I2C0_SCL / SPI1_CSn |
| 14 | GP10 | I2C1_SDA / SPI1_SCK |
| 15 | GP11 | I2C1_SCL / SPI1_TX |
| 16 | GP12 | UART0_TX / I2C0_SDA / SPI1_RX |
| 17 | GP13 | UART0_RX / I2C0_SCL / SPI1_CSn |
| 19 | GP14 | I2C1_SDA / SPI1_SCK |
| 20 | GP15 | I2C1_SCL / SPI1_TX |
| 21 | GP16 | SPI0_RX / I2C0_SDA / UART0_TX |
| 22 | GP17 | SPI0_CSn / I2C0_SCL / UART0_RX |
| 24 | GP18 | SPI0_SCK / I2C1_SDA |
| 25 | GP19 | SPI0_TX / I2C1_SCL |
| 26 | GP20 | I2C0_SDA |
| 27 | GP21 | I2C0_SCL |
| 29 | GP22 | — |
| 31 | GP26 | ADC0 / I2C1_SDA — **potentiometer wiper** |
| 32 | GP27 | ADC1 / I2C1_SCL |
| 34 | GP28 | ADC2 |
| 35 | GP29 | ADC3 |

`Vin`(39)/`Vout`(40) deliberately unconnected — see §1.

**Given this project's own documented history of TX/RX coupling on
adjacent pins**, whichever UART is used for the console should not be
placed on an adjacent GPn/GPn+1 pair without deliberately checking for the
same failure mode this project has already hit once.

### Currently allocated, confirmed from `config.h`

| Pin | Signal |
|---|---|
| GP2 | AIN1 (motor direction) |
| GP3 | AIN2 (motor direction) |
| GP4 | I2C0 SDA |
| GP5 | I2C0 SCL |
| GP14 | PWMA (motor speed) |
| GP15 | STBY (motor driver enable) |
| GP18 | LED data |
| GP20 | UART TX (console) |
| GP21 | UART RX (console) |
| GP26 | ADC0 — potentiometer wiper |

**No `SDC41 enable` or `CO2-LIMIT_AB` pin exists in `config.h` yet** —
confirms Phase 1 (CO₂ integration) genuinely hasn't started in this
codebase. **No `BIN1`/`BIN2`/`PWMB` definitions exist either** — the
TB6612's channel B (buzzer, per the schematic) is also unallocated,
confirming buzzer control is Phase 2 work, not already partially done.

---

## §5. Net labels confirmed on schematic

| Signal | Notes |
|---|---|
| `ADC0 (GP26)` | Potentiometer wiper |
| `AIN1`, `AIN2` | Motor driver channel A |
| `BIN1`, `BIN2` | Buzzer driver channel B |
| `PWM-A`, `PWM-B` | Motor/buzzer PWM |
| `STBY` | TB6612FNG standby |
| `SDC41 enable` | GPIO-driven TPS22918 enable for the CO₂ sensor's power switch |
| `LED enable` | GPIO-driven TPS22918 enable for the LED's power switch |
| `CO2-LIMIT_AB` | Living/sleeping-room mode switch — GPIO number not confirmed from extracted text, treated as GP10 pending confirmation |
| `Sound ON-OFF` | Hardware mute for the buzzer, independent of any firmware alarm-acknowledge logic |

Both TPS22918 enable notes on the schematic read "High = Enable" —
consistent with the part's standard active-high `ON` pin behavior.

---

## §6. CO₂ air-quality zones (confirmed on schematic)

Two profiles, switched by `CO2-LIMIT_AB`:

**[A] Living room (GP10 high)**

| Band | Range |
|---|---|
| Excellent | < 800 ppm |
| Good | 800 – 1,000 |
| Fair | 1,000 – 1,500 |
| Poor | 1,500 – 2,000 |
| Very Poor | > 2,000 |

**[B] Sleeping room (GP10 low)**

| Band | Range |
|---|---|
| Excellent | < 1,000 ppm |
| Good | 1,000 – 1,500 |
| Fair | 1,500 – 2,000 |
| Poor | 2,000 – 2,500 |
| Very Poor | > 2,500 |

---

## §6a. Confirmed from `main.c` — real-time constraints for Phase 1/2

**Watchdog timeout: 100 ms** (`watchdog_enable(100, true)`). This is a hard
number, not a general caution: the `sdc41` retry-based `write_command()`
can block up to **1000 ms** in its worst case — ten times this system's
entire watchdog budget. Calling that code path anywhere that doesn't kick
the watchdog throughout the wait doesn't just stall control — it forces an
unplanned reset partway through the very first retry. Any ported CO₂
driver code must either kick the watchdog inside its own retry loop, or
run with a substantially shorter worst-case timeout than `sdc41`'s
original 1000 ms design.

**Confirmed execution-context split, from the 1 kHz timer callback vs. the
main loop:**

| Runs in `on_tick()` (1 kHz timer context) | Runs in the main `for(;;)` loop |
|---|---|
| `encoder_tick()` | `console_poll()` |
| `controller_tick()` | `console_drain_events()` |
| `power_monitor_tick()` | `led_update()` |
| | `dbg_poll()` / `dbg_out_drain()` |

**Any new CO₂ polling belongs in the main-loop list, patterned after
`dbg_poll()` — never added to `on_tick()`.** Calling a blocking I²C
operation from the 1 kHz context would stall motor control for the
duration of the call, every single tick it happens to run.

**Worth verifying directly, not assumed either way**: `power_monitor_tick()`
already runs inside the 1 kHz context. If it performs a blocking INA219
I²C read there, that's an existing instance of the exact hazard class this
project spent significant time on in `sdc41` — normally fast, capable of
hanging indefinitely on a real bus fault. Needs `power_monitor.c` to
confirm; not yet checked.

**Boot init order, confirmed**: `console_init()` → `motor_init()` →
`encoder_init()` → `led_init()` → `power_monitor_init()` →
`controller_init()` → `motor_enable()` → start 1 kHz timer → first tick
completes → `watchdog_enable(100, true)`. A future CO₂ sensor init likely
belongs alongside `power_monitor_init()`, sharing the same already-brought-up
I²C0 bus, before `controller_init()`.

**`watchdog_update()` confirmed, and it changes the risk assessment for
the better.** It's called as the first substantive line in
`controller_tick()`, which runs inside `on_tick()` — the independent 1 kHz
hardware timer callback, not the main loop.

**Correction to earlier guidance in this document**: because the watchdog
is serviced from a timer interrupt independent of the main loop, a
blocking main-loop operation (a slow CO₂ I²C retry, worst case up to
1000 ms) does **not** directly risk a watchdog reset — the 1 kHz IRQ can
still preempt it and call `watchdog_update()` on schedule, as long as the
blocking call doesn't disable interrupts (a normal SDK blocking I²C call
doesn't). Motor control (`controller_tick()`) is entirely unaffected
either way, since it never touches the main loop.

**What a main-loop CO₂ block actually costs**: `console_poll()`,
`console_drain_events()`, and `led_update()` delayed for the block's
duration — up to ~1 s of delayed UART responsiveness and delayed LED
updates. A UX cost, not a safety-critical one. Still worth keeping the
retry's worst case as short as reasonably possible, but this is no longer
the load-bearing safety concern it was described as before `controller.c`
was available to check directly.

## §6b. Console and LED architecture, confirmed from source

**UART1 on GP20/GP21, IRQ-driven RX, blocking TX.** `console_uart_rx_irq()`
feeds a ring buffer from a real interrupt handler — RX cannot lose
characters to a busy main loop. TX (`write_text()`) is a blocking
`uart_putc_raw()` loop with no interrupt; it measures its own blocking time
into `tx_spin_us` for diagnostics, meaning this was already identified as
worth watching, not overlooked.

**A real defensive precedent already exists for the ANSI-coupling failure
mode.** `console_poll()` contains a CSI escape-sequence state machine that
strips escape sequences from input — but **only when `dbg_active()` is
true.** Plain/production console mode has no such filtering; an escape
byte reaching RX in that mode would be appended into the line buffer
unfiltered. This is direct evidence the coupling risk is real and has
already needed a specific mitigation here, not just a caution carried over
from `sdc41`.

**A per-character debug UI already exists as an established pattern**:
`ST_DEBUG` state, `dbg_enter()`/`dbg_exit()`, `dbg_handle_key()` consuming
one character at a time — structurally separate from the plain line-based
command parser. **Any future CO₂ debug page belongs inside this existing
system, `LUFTFUGL_MONITOR`-gated**, reusing its established escape-safety
handling — not a new, separate mechanism that would need to rebuild the
same protection from scratch.

**LED color display only activates at `ST_IDLE`** — explicitly commented
*"Passing through a station must never display its colour."* A future
CO₂→station mapping needs no separate LED logic: once the motor completes
a CO₂-driven move and goes idle, the existing per-station color mapping
fires automatically. Station 5 already pulses rather than displaying
solid (`hazard_lit()`) — an existing "alert" treatment that a Very-Poor
CO₂ mapping to station 5 would inherit for free. Battery-critical/warn
states override station colors entirely; air-quality display already
yields priority to battery alerting.

**Power/battery console commands (`batt`, `batt raw/res/log/events/reset`,
`load`, `ina`) are in the base command set, not `LUFTFUGL_MONITOR`-gated**
— power diagnostics are treated as a production feature here, not a debug
extra. Worth matching that precedent: a plain `co2` command alongside a
deeper `LUFTFUGL_MONITOR` view, rather than gating all CO₂ visibility
behind debug mode.

**`power_monitor.h`'s API surface is substantially larger than previously
credited** — `power_monitor_motion_start()`/`_motion_stop()` suggest INA219
sampling is deliberately synchronized to motor motion (likely for
inrush/stall current capture specifically during moves), plus a full set
of console-formatting functions already present. The actual
`power_monitor.c` implementation still hasn't been seen — this is
inferred from the header's shape, not confirmed line-by-line.

## §7. Still open — confirm before treating as final

- **RC filter component values on the potentiometer wiper**: schematic
  shows `R3 = 100 Ω`, not the `4.7 kΩ` earlier project guidance
  recommended — see §3. Needs netlist/visual confirmation of what `R3` is
  actually wired to before treating either value as correct.
- **`controller.c` now received and reviewed in full** — no longer open.
  Confirmed `watchdog_update()` location and the corrected, less severe
  real-time risk picture above (§6a). Confirmed `controller_request(REQ_MOVE,
  position)` as the integration point for a future CO₂→station mapping,
  with `MOVE_BUSY`/`MOVE_ALREADY`/`MOVE_FAULT` as the result codes that
  integration must handle.
- **`power_monitor.c`, `debug.c`, `ws2812.pio`** also failed to come
  through — lower priority than `controller.c`, but would round out the
  power-monitoring and debug-menu pictures fully.
- **Reed-switch conclusion in §0** — now confirmed directly from
  `encoder.c`/`config.h`, not just inferred; no longer genuinely open.
- **`CO2-LIMIT_AB` GPIO number** — currently assumed GP10, not confirmed
  from schematic text extraction.
- **Wire-color-to-pin mapping** for the motor/potentiometer/buzzer harness
  — needs the visual schematic or `.kicad_sch` file directly.
- **LM74700 active-mode quiescent current** — needs datasheet, not
  resolvable from a schematic.
- **Full battery-life recalculation** against the confirmed 3× AA / 4.5 V
  basis and the XL63070's real input range/efficiency — not yet done, was
  previously based on the wrong regulator and cell count entirely.

---

## §8. Two-phase implementation structure

**Architectural point that reframes both phases**: `config.h` already
defines five station-indexed RGB colors (`LED_STATION1_R/G/B` through
`LED_STATION5_...`), forming a severity gradient, and the LED is already
correctly configured single-unit RGBW. **The motor's position is not a
separate feature from CO₂ display — it is the CO₂ display.** This is a
wall sculpture where a physical needle points to one of five positions
representing air-quality severity, colored to match. CO₂ integration is
not "add a sensor, show a number on a console" — it's "a CO₂ reading must
select which of the five existing stations the motor drives to."

**Phase 1 — CO₂ sensing integration.** Port the protocol-layer driver
already proven working on real hardware in the `sdc41` project (`sdc41.c`
— CRC, retry-based `write_command`, idle-mode pattern) into this project,
against the pin table above — no `SDC41 enable`/`CO2-LIMIT_AB` GPIO is
allocated yet, both need adding to `config.h`. Single-shot power-cycled
measurement via the TPS22918 enable line, not continuous mode — see
`integration-with-luftfugl-motor.md` from that project for why. Send
`persist_settings` once during bring-up after calibrating `offset` — a
real bug flagged there this project would otherwise ship with. **New
requirement this source review surfaces**: map each CO₂ zone band (§6) to
one of the five existing motor stations — this mapping doesn't exist yet
and is a real design decision, not an implementation detail (does
Excellent→station 1 through Very Poor→station 5, one profile per
`CO2-LIMIT_AB` setting, or something else?). The call site is confirmed:
`controller_request(REQ_MOVE, target_station)` — handle `MOVE_BUSY` by
skipping and retrying next reading cycle, `MOVE_ALREADY` as a no-op
success, and `MOVE_FAULT` by not retrying blindly (the motor is disabled
and needs an explicit `home` command first).

**Phase 2 — Full system integration.** Wire the CO₂→station mapping from
Phase 1 into `encoder_set_nominal()`/the existing station-drive logic —
the motor and LED color-per-station code already exists and needs no
rewrite, only a new caller. Buzzer alarm via the TB6612's unallocated
channel B, respecting the hardware `Sound ON-OFF` mute. `CO2-LIMIT_AB`
room-mode switch read once per wake, no debounce needed at a 30-minute
polling interval. INA219 power monitoring (config already substantially
present — `INA219_NORMAL_CONFIG` etc. already defined). DORMANT sleep
tying it together on the confirmed 3× AA / XL63070 power budget, once §7's
recalculation is done and the station-table discrepancy above is resolved.
