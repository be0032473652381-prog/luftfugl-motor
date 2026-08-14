# Task — INA219 Power Monitoring

Implement INA219 support and extend the debug console with battery and load
diagnostics.

Configuration is in `hardware.md`. Behaviour is in `power-monitor.md`. Read
both.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## Read this first: what this hardware can and cannot measure

Three requested features are not achievable and must not be implemented as
though they were. Implementing them anyway produces plausible-looking numbers
that are wrong, which is worse than not having them.

### Not possible — do not implement

**PWM ripple sampling.** Motor PWM is 5 kHz, a 200 µs period. Nyquist needs
≥ 10,000 samples/s. One INA219 register read over a 100 kHz I²C bus takes
~540 µs, giving ~1,850 samples/s at absolute best and well under 1,000 in
practice. The bus cannot go faster because the CO₂ sensor caps it at 100 kHz.

**Sleep-mode current detection.** The 0.1 Ω shunt gives 100 µA per LSB. Sleep
current is 238 µA — **2.4 LSB**. That is quantisation noise, not a measurement.
Report sleep current from the compiled budget constant, and state in the output
that it is modelled rather than measured.

**Inrush waveform capture.** At ~620 µs per sample, a 2–10 ms inrush yields 3
to 16 points. Peak *magnitude* capture is marginally viable at 9-bit
conversion; waveform *shape* is not. Implement peak capture only, and label it
as a lower bound.

### Possible but constrained

**Coulomb counting.** The INA219 is powered for ~1.2 s of every 1800 s — 0.07%
of the time. **99.93% of consumed charge happens while the meter is off.**

Integrating measured current alone would under-report by three orders of
magnitude. Instead:

```
charge_consumed = measured_charge_during_wake
                + (SLEEP_CURRENT_UA * sleep_duration)
```

where `SLEEP_CURRENT_UA` is a compiled constant set from a one-off bench
measurement with an inline meter. Make clear in `help` and in the output that
the sleep term is modelled.

**Everything else in the list below is achievable and should be implemented.**

---

## Driver

Registers: configuration 0x00, shunt voltage 0x01, bus voltage 0x02, power
0x03, current 0x04, calibration 0x05.

Configure per `hardware.md`: 0.1 Ω shunt, ±160 mV PGA, 16 V bus range,
100 µA current LSB, 4 mV bus LSB.

The calibration register must be written before current and power registers
return meaningful values. Compute it from the shunt value and chosen current
LSB rather than hardcoding.

**Power down between readings** — MODE bits 000, 6 µA. Converting draws
0.6 mA, which is more than twice the whole sleep budget. Use triggered
conversions, wait for the conversion-ready bit rather than a fixed delay, then
power down.

Bus voltage register bit 1 is the conversion-ready flag; bit 0 is the maths
overflow flag. Check overflow — it indicates the current exceeded the
configured range and the value is invalid.

---

## Measurements

### Instantaneous

| Quantity | Source |
|---|---|
| Bus voltage | register 0x02, 4 mV LSB |
| Shunt voltage | register 0x01, 10 µV LSB |
| Load current | register 0x04, 100 µA LSB |
| Power | register 0x03, computed internally by the device |

Read the power register rather than multiplying V × I in firmware; the device
does it with full internal precision.

### Battery state of charge

Map bus voltage to percentage against an alkaline discharge curve. Four AA
cells in series:

| Pack voltage | Approx. remaining |
|---|---|
| 6.4 V | 100% |
| 6.0 V | 85% |
| 5.6 V | 70% |
| 5.2 V | 50% |
| 4.8 V | 30% |
| 4.4 V | 15% |
| 4.2 V | 8% |
| 4.0 V | 0% |

Interpolate linearly between points. The curve is flat in the middle, so
voltage alone is a poor indicator there — this is an estimate and should be
presented as one.

**Measure under load.** Cells recover 100–200 mV at rest and read high. Use the
reading taken during the CO₂ light-source burst.

### Internal resistance

```
R_pack = (V_idle - V_load) / (I_load - I_idle)
```

Take `V_idle`/`I_idle` with the sensor powered but idle, and `V_load`/`I_load`
during the light-source burst. With ΔI only ~6 mA the estimate will be noisy —
average over at least 10 wake cycles before reporting, and report the sample
count alongside.

Rising resistance is a better end-of-life indicator than voltage.

### Remaining capacity and runtime

```
remaining_mah = BATTERY_CAPACITY_MAH - consumed_mah
runtime_hours = remaining_mah / average_current_ma
```

`consumed_mah` accumulates per §"Coulomb counting" above. Persist it across
resets if there is somewhere to persist it; if not, state in the output that it
counts from last boot.

`average_current_ma` from consumed charge over elapsed time, not the
instantaneous reading.

---

## Load diagnostics

### Motor stall detection

Stall current exceeds running current substantially. Capture both during
bring-up and set thresholds from measurement, not assumption. Detect as: current
above `STALL_CURRENT_MA` sustained for more than `STALL_DETECT_MS` while the
motor is commanded to move.

Note this is a *diagnostic*, not a protection — `v3-simulation-rig.md` removed
motion guards deliberately. Report it, do not act on it.

### Startup inrush

On a motion command, switch the INA219 to 9-bit conversion and sample as fast
as the bus allows for 20 ms, recording the maximum. Report as a lower bound —
the true peak is likely higher and falls between samples.

Restore the normal conversion setting afterwards.

### Mechanical binding

Track running current per move over time. A gradual upward trend across many
moves indicates increasing friction. Keep a rolling average of the last N moves
and report the trend as a percentage change, not an alarm.

### No-load versus loaded

Compare running current against the no-load figure recorded during bring-up.
Report the ratio.

### Short circuit

Current above `SHORT_CIRCUIT_MA` for more than a few milliseconds, or the maths
overflow flag set in the bus voltage register. Report immediately.

### Poor connection

Compare pack resistance against its value when fresh. A step increase not
matched by falling voltage suggests a contact problem rather than depletion.
Requires the resistance history from above.

---

## Session logging

Accumulate per session and report on request:

| | |
|---|---|
| Total charge consumed | mAh |
| Total energy consumed | mWh |
| Session duration | |
| Wake count | |
| Peak current seen | with a timestamp |
| Minimum bus voltage seen | with a timestamp |

Keep a ring of the last 16 notable events — peak current, minimum voltage,
stall detections — each with a timestamp and the current and voltage at that
moment. Size it to fit comfortably in RAM.

---

## Console

Extend the existing console. Follow the conventions already in use: fixed-width
columns, plain-language rejections, examples in `help`.

| Command | Output |
|---|---|
| `batt` | Voltage, current, power, SOC%, remaining mAh, estimated runtime |
| `batt raw` | Bus and shunt register values, current, power, overflow flag |
| `batt res` | Pack resistance, sample count, trend against fresh |
| `batt log` | Session totals |
| `batt events` | The event ring |
| `batt reset` | Clear session counters |
| `load` | Last move: running current, peak, no-load ratio, trend |
| `ina` | Configuration register, calibration value, conversion setting |

`batt` is the one that matters. It should show enough to judge battery state at
a glance:

```
 BATTERY
   bus voltage      5.84 V        state of charge   ~78%
   current          6.2 mA        remaining         1950 mAh
   power            36 mW         runtime est.      341 days
   pack resistance  0.62 R        (12 samples)
   consumed         550 mAh       since 14 d 3 h
   sleep current    238 uA        (modelled, not measured)
```

Mark modelled values as modelled. Mark estimates as estimates.

---

## Constants

Add to `config.h`. Set the ones marked *measured* from bench readings, not
guesses — if a value has not been measured, say so in your report rather than
inventing one.

| Constant | Purpose |
|---|---|
| `BATTERY_CAPACITY_MAH` | 2500 for 4× AA alkaline |
| `SLEEP_CURRENT_UA` | *measured*, inline meter |
| `NO_LOAD_CURRENT_MA` | *measured*, motor free-running |
| `STALL_CURRENT_MA` | *measured* |
| `STALL_DETECT_MS` | |
| `SHORT_CIRCUIT_MA` | |
| `RPACK_FRESH_MOHM` | *measured*, fresh cells |
| `INRUSH_SAMPLE_MS` | 20 |
| `EVENT_LOG_DEPTH` | 16 |

---

## Verification

Report actual console output for:

1. `batt` with the values populated
2. `batt raw` — register values, and confirmation the calibration register was
   written before current was read
3. `ina` — configuration matching `hardware.md`
4. Current draw with the INA219 powered down, confirming MODE 000 is set
5. `batt` immediately after boot, showing sensible output with no history yet
6. Which constants are measured and which are placeholders

Item 6 matters. I would rather have three real constants and five marked
unknown than eight plausible-looking guesses.

Do not run the motor.
