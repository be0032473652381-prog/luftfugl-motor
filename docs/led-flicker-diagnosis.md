# LED flicker investigation — 6 September 2026

Baseline firmware: `43a0dac`. No firmware behavior was changed in this
investigation. The proposed continuous ALS-read / noise-driven multiplier
failure is ruled out by the current call path and host regression tests.
The reported visible flicker has not been reproduced or proven fixed.

## Sensor cadence and stored multiplier

`src/main.c` consumes `power_monitor_take_sensor_cycle()` before requesting an
ALS sample. `ambient_light_poll()` is serviced every main-loop iteration, but
polling the state machine is not equivalent to reading the sensor.

```c
if (power_monitor_take_sensor_cycle())
    ambient_light_request_sample();
ambient_light_poll();
led_update();
```

`src/power_monitor.c` sets `sensor_cycle_pending` in `PM_IDLE` only when the
scheduled deadline is due. It sets the next deadline to
`ms + INA219_SAMPLE_PERIOD_MS` (1000 ms) after the battery read/power-down
sequence. Thus this is approximately one scheduled measurement cycle per
second, not one measurement for every 1 ms WFI wake. Integration or bus delays
may lengthen the interval; requests during integration are coalesced.

`src/ambient_light.c` reads ALS only in `ALS_INTEGRATING`, after `ready_us`
(132500 us after activation). A completed read attempts shutdown, which
changes the state to `ALS_IDLE` or `ALS_NEEDS_SHUTDOWN`. It cannot repeat the
ALS read just because the main loop runs again.

```c
bool read_ok = read_register(REG_ALS, &raw);
bool down_ok = shutdown_sensor();
```

The successful-reading branch converts counts once and calls `confirm_zone()`
once. The zone table is consulted only on initialization, a confirmed zone
change, or explicit debug multiplier set/reset. The cache is static:

```c
static uint16_t confirmed_multiplier_percent;

uint16_t ambient_light_multiplier_percent(void) {
  return confirmed_multiplier_percent;
}
```

The LED color path is:

```text
led_update -> requested_colour -> indication color helper
 -> colour_word_rgbw -> led_brightness_multiplier_percent
 -> ambient_light_multiplier_percent -> stored uint16_t
```

`led_brightness_multiplier_percent()` still applies a fixed, deterministic
ceiling to the stored value on each call. This performs no lux conversion or
zone selection and cannot vary with raw sensor noise. Channel scaling also
runs on each color calculation, but its inputs remain stable for a steady
indication. The physical-frame cache still checks:

```c
if (colour == last_colour)
  return;
led_transmit_frame(colour);
```

There are no I2C calls, sensor requests, `confirm_zone()` calls, or
`update_multiplier_cache()` calls in `src/led.c`.

## Hysteresis — already correct, unchanged

`confirm_zone()` starts from static `sample.confirmed_zone`, separately records
`sample.instant_zone`, and applies both directional margins:

```c
(uint64_t)sample.millilux * 100u >=
    (uint64_t)zone_min_lux[candidate + 1] * 1000u *
        (100u + AMBIENT_HYSTERESIS_PERCENT)
```

The downward comparison uses `<` and `(100u - AMBIENT_HYSTERESIS_PERCENT)`.
Upward transitions require at least 12/120/600 lux; downward transitions
require below 8/80/400 lux. A candidate must survive three consecutive valid
readings. A different candidate, return into the deadband, or invalid reading
resets the pending count. Only confirmation updates the stored zone and cache:

```c
if (sample.candidate_samples >= AMBIENT_CONFIRM_SAMPLES) {
  sample.confirmed_zone = candidate;
  update_multiplier_cache();
  cancel_candidate();
}
```

The static confirmed state survives sensor shutdown and `__wfi()`.

## Verification

Both Debug and Release configure/build successfully with `-Wall -Wextra`,
without warnings. Existing `test/test_ambient_light.py` checks integration
timing, shutdown/readback, idle polling, all hysteresis boundaries, debounce,
outliers and failures.

Extended `test/test_led_ambient.py` links the real ALS and LED modules with
mock I2C/PIO/GPIO and tests alternating one-count ALS noise between scheduled
samples. For each of five stations, four zones and both RGB/RGBW modes, eight
measurements are followed by 1000 main-loop polls. Each poll services the idle
ALS state machine and LED, and asserts stable color/power/scale. Transaction
and transmission counters must remain unchanged between measurements.

Result, in both Debug and Release host configurations, with address/undefined
behavior sanitizers and `-Wall -Wextra -Werror`:

```text
PASS: 320000 LED/main-loop polls with one-count ALS noise: no extra I2C transactions, no retransmissions, stable power/colour/scale across five stations and four zones in RGB/RGBW
```

Read-only SWD observations sampled the running board 300 times, separated by
50 ms plus read overhead. No halt, reset, or motion command was used during
that capture. Symbol addresses and struct layout came from the baseline ELF.
Raw capture: `/tmp/flicker-swd-snapshots.log` in the development workspace.

| Observed state | Range / value |
|---|---|
| Averaged position ADC | 1027–1031 (Station 3 nominal 1022) |
| Cached LED word | `0x030a0000`, unchanged |
| LED software power flag | 1, unchanged |
| Controller state | `ST_IDLE`, unchanged |
| ALS counts | 146–195 |
| Lux | 78.490–104.832 |
| Instant zone | Dim and Indoor |
| Confirmed zone | Dim throughout |
| Cached multiplier | 200% throughout |
| Sample counter | 309–326 |
| ALS error counter | 0 |
| Last read-back shutdown configuration | `0x1001` |

These observations show the confirmed zone holding while raw readings cross
100 lux. They do not measure optical output, supply voltage, or capture every
short GPIO/PIO transient.

## Separate possible path and limits

`requested_colour()` checks `led_station_at_live_adc()` on every call. That
helper uses the latest averaged ADC and a +/-20-count window; crossing it
returns off immediately. This can power-cycle the LED independently of ALS
if a mechanism rests at the edge of a station window. In the live observations,
the ADC remained within 5–9 counts of Station 3, so that path was not observed
causing flicker. Motion/off gates and intentional alert/breathing patterns
were preserved.

No additional multiplier cache or hysteresis fix is justified by this evidence.
If visible flicker continues with stable software output, a synchronized
capture of GP0 supply voltage and GP18 data during the flicker is the next
measurement needed to distinguish an electrical problem from a short software
transition missed by the snapshots.
