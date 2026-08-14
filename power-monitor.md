# power-monitor.md — low-power operation

Sleep scheduling, battery state of charge, and alarm behaviour.

Pin assignments, device addresses and electrical configuration are in
`hardware.md`.

---

## 1. Wake cycle

```
wake on RTC_INT falling
  CO2_EN high
  INA219 triggered conversion, read V and I at light load
  DORMANT until CO2_RDY falls
  read CO2 over I2C
  INA219 read V and I under load
  compute pack resistance
  update LED, sound alarm if due
  CO2_EN low, LED_PWR low, INA219 power down
  clear RTC timer flag
  DORMANT until RTC_INT
```

Total awake time ~1.2 s per cycle, of which 730 ms is the sensor measurement.

**Enter DORMANT during the measurement rather than waiting.** The MCU has
nothing to do while the sensor works, and staying awake for it triples the
average current.

Wake sources are GPIO only — DORMANT stops all clocks, including the timer.
The watchdog cannot be serviced while stopped, so disable it before entering
DORMANT and re-enable on wake.

## 2. Power budget

| Item | Sleep current |
|---|---|
| **RP2040 DORMANT** | **180 µA** |
| Buck-boost quiescent | 25 µA |
| MCU wake burst, 1.2 s per 30 min | 23 µA |
| INA219 powered down | 6 µA |
| CO₂ sensor, EN low | 2.5 µA |
| TB6612 standby | 1 µA |
| PCF8563T | 0.25 µA |
| LED, power switched off | 0 |
| **Total** | **~238 µA** |

~437 days on 4× AA alkaline at 2500 mAh.

The RP2040 is 76% of the total. Nothing else is worth optimising.

Three loads must be actively switched off or they dominate:

| If left on | Draw |
|---|---|
| LED, controller powered | ~1 mA |
| INA219, converting | 0.6 mA |
| CO₂ sensor, EN high | 4 mA |

## 3. Measurement interval

| Interval | Average | Life |
|---|---|---|
| 1 min | 981 µA | 106 days |
| 5 min | 366 µA | 285 days |
| 10 min | 289 µA | 361 days |
| **30 min** | **238 µA** | **437 days** |
| 60 min | 225 µA | 463 days |

Returns flatten beyond 10 minutes because sleep current dominates. Changing the
interval is a single write to the RTC countdown register, so it can be set at
runtime.

## 4. Battery state of charge

Voltage alone is unreliable — the alkaline discharge curve is flat for most of
its life, and cells recover 100–200 mV at rest. Measure under load and derive
internal resistance:

```
V_idle,  I_idle    with the sensor idle
V_load,  I_load    during the light-source burst

R_pack = (V_idle - V_load) / (I_load - I_idle)
```

Resistance rises well before voltage sags, and identifies a single failing cell
rather than just a tired pack.

| | Warn | Critical |
|---|---|---|
| Resting voltage | 4.4 V | 4.0 V |
| Pack resistance | 1.5 Ω | 3.0 Ω |

Whichever triggers first.

With the sensor drawing only ~6 mA, ΔI may be too small for a stable estimate.
If the motor runs during the wake window, take the loaded reading then. If not,
a brief buzzer pulse provides a known load.

Sleep current at 238 µA is 2 LSB on the 0.1 Ω shunt and is not measurable by
the INA219. Verify it once with an inline meter during bring-up.

## 5. Alarm

| State | Buzzer | LED |
|---|---|---|
| Normal | silent | station colour |
| Battery warn | 1 chirp, 200 ms | amber pulse, 1 s |
| Battery critical | 3 chirps | amber hazard blink, 5 s |
| CO₂ alarm | 3 chirps | red |

**Sound only at scheduled wakes.** Never continuously, and never wake outside
the normal interval to make noise:

| Pattern | Extra current | Life |
|---|---|---|
| 30 s every 5 min | +4.17 mA | **24 days** |
| 3 chirps of 200 ms per 30-min wake | +0.014 mA | 414 days |

The first turns a 14-month device into a 24-day one.

`ack` silences the battery alarm for 24 hours. A warning that cannot be quieted
gets the batteries removed.

## 6. CO₂ auto-calibration

ABC is disabled. It calibrates after `calibration_cycle × 48` measurements —
336 by default, which at 30-minute intervals is 7 days — and assumes the sensor
sees genuine 400 ppm outdoor air within that window.

In a space that never reaches outdoor levels it will drift the baseline wrong.
Leave disabled and calibrate manually.

## 7. Console commands

| Command | Reports |
|---|---|
| `batt` | Voltage, current, pack resistance, estimated days remaining |
| `power` | Sensor EN, LED switch, INA219 mode |
| `co2` | Last reading, age, RDY state |
| `sleep` | Time to next wake, last wake duration |
| `interval <min>` | Set the measurement interval |
| `buzz` | Test the buzzer |
| `ack` | Silence the battery alarm for 24 h |

`batt` should report a trend. A single reading taken during a burst looks
alarming and means nothing alone.

## 8. Open items

- Motor current during the wake window. If the mechanism moves every cycle it
  dominates this budget and the interval table needs redoing.
- Measured DORMANT current. 180 µA is typical; some designs reach lower by
  stopping ROSC as well.
- Buzzer resonant frequency, for the PWM slice.
