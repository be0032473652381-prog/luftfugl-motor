# VEML7700 ambient brightness — implementation and bench verification

Verified 6 September 2026. Debug and Release firmware both include the sensor
and the shared LED scaling. The connected device responded at I2C0 address
`0x10`; the normal `led` debug status now includes its readings and shutdown
verification. No new command verb or production-protocol response was added.
Existing unrelated working-tree changes were retained; no commit was created.

## Sensor transaction and power behavior

The driver is `src/ambient_light.c`. It uses the already initialized shared
I2C0 bus and `power_monitor_i2c_claim/release()`. No GPIO, interrupt pin,
power-switch pin, or new periodic timer was allocated.

[Vishay's VEML7700 datasheet](https://www.vishay.com/docs/84286/veml7700.pdf)
defines the register layout, low-byte-first words, and software shutdown bit.
[Vishay application note 84323, revision 06-Mar-2025](https://www.vishay.com/docs/84323/designingveml7700.pdf)
specifies a 2.5 ms startup, integration tolerance of ±30%, and a minimum
integration wait. The chosen gain is 1/8 and integration time is 100 ms.
The driver therefore waits at least **132.5 ms after the wake write completes**.
It releases the bus during this wait and returns to the main loop/WFI.

The actual transaction sequence, with a 7-bit address of `0x10`, is:

```text
Initialization:
  write [00 01 10]                         ALS_CONF = 0x1001, shutdown
  write [00], repeated START, read 2 bytes verify ALS_CONF == 0x1001
Each existing scheduled battery cycle:
  write [03 00 00]                         PSM disabled
  write [00 00 10]                         ALS_CONF = 0x1000, awake
  wait >= 132500 us                        nonblocking, I2C bus released
  write [04], repeated START, read LSB/MSB  ALS counts
  write [00 01 10]                         ALS_CONF = 0x1001, shutdown
  write [00], repeated START, read LSB/MSB  verify ALS_CONF == 0x1001
```

Each full register write ends with STOP. Each register read ends with STOP
after its two-byte read. On the wire the address bytes are `0x20` for write
and `0x21` for read. Raw ALS counts are converted using 0.5376 lux/count;
Vishay's polynomial correction applies above 1000 lux. These measurement
constants are grouped in `src/config.h:112`.

A sample is accepted only after its read succeeds and shutdown is verified.
A failed wake/read still attempts shutdown. A failed shutdown stays marked
unverified, and recovery retries only when another existing sensor cycle
requests it. No millisecond retry storm is introduced. Invalid samples cancel
pending zone changes and retain the last confirmed brightness zone; before
any successful confirmation the default is Night.

Readback verifies the chip's shutdown configuration, not a measured 0.5 uA
supply current. No current probe or logic-analyzer capture was used.

## Scheduling and confirmation

The current firmware has WFI between 1 kHz safety interrupts, not a completed
long-duration sleep/wake coordinator. The integration therefore reuses the
existing scheduled INA219/battery cycle, nominally once per second.
`power_monitor_tick()` sets a volatile `sensor_cycle_pending` flag only when
its existing scheduled deadline is due. `power_monitor_take_sensor_cycle()`
atomically consumes it in main context. Manual battery reads and motor inrush
reads do not independently trigger ambient measurements. Requests coalesce
while an ambient conversion is in progress.

The flags and call sites are in `src/power_monitor.c:65`,
`src/power_monitor.c:433`, `src/power_monitor.c:488`, and `src/main.c:166`.
All VEML7700 I2C work, conversion arithmetic, zone decisions, and LED updates
remain in main context. The watchdog and motor-controller IRQ path are intact.

The four zone floors and four multipliers are named constants in
`src/config.h:139`:

| Zone | Nominal lux range | Multiplier | Station base 3% | Alert base 30% |
|---|---|---|---|---|
| Night | 0 to <10 | 1x | 3% | 30% |
| Dim | 10 to <100 | 2x | 6% | 60% |
| Indoor | 100 to <500 | 4x | 12% | 100%, saturated |
| Bright | >=500 | 8x | 24% | 100%, saturated |

`AMBIENT_HYSTERESIS_PERCENT = 20`. Brighter transitions require at least
12 / 120 / 600 lux, and darker transitions require below 8 / 80 / 400 lux.
The four zone floors include Night's zero floor; there are three actual
inter-zone boundaries. Equality at a nominal boundary affects only the raw
instantaneous zone, not a confirmed transition inside the hysteresis band.

`AMBIENT_CONFIRM_SAMPLES` currently aliases the established
`BATTERY_ASSERT_SAMPLES` value of three, and can be tuned separately later.
Three consecutive readings must qualify for the same candidate zone. Returning
to the deadband, changing candidate, or encountering a failed sample resets
confirmation progress. `sample.confirmed_zone` is stored separately from
`sample.instant_zone` and `sample.candidate_zone`; it persists across sensor
shutdown and WFI. Only firmware initialization resets it. This logic is in
`src/ambient_light.c:39`.

## One shared LED scale

All existing base-percentage indications pass through this common code in
`src/led.c:79`:

```c
uint32_t brightness = base_percent * led_brightness_multiplier_percent();
if (brightness > LED_MAX_BRIGHTNESS_PERCENT * 100u)
  brightness = LED_MAX_BRIGHTNESS_PERCENT * 100u;
r = scaled_channel(r, brightness);
g = scaled_channel(g, brightness);
b = scaled_channel(b, brightness);
w = scaled_channel(w, brightness);
```

The value retains hundredths of a percent until the final channel rounding.
The five station colors, both battery alerts, CO2-error red, all 32 warm-up
breathing steps, and startup accepted-sample white use this same scale.
The formerly hardcoded RGBW sample word is now:

```c
return colour_word_rgbw(LED_WARM_RGBW_R, LED_WARM_RGBW_G, LED_WARM_RGBW_B,
                        LED_WARM_RGBW_W, LED_SAMPLE_BRIGHTNESS_PERCENT);
```

Breathing uses the same helper with its current envelope percentage.
The night-time channel values are preserved, including the white die.
Explicit `led raw` wire bytes retain their diagnostic meaning; that mode has
no base percentage. Movement, between-position and forced-off gates still
suppress the LED, including in raw mode.

Scaling is proportional until the physical 100% ceiling. A 30% base cannot
remain exactly proportional at 4x or 8x; alerts saturate at 100%, without
clipping individual RGB channels or changing their intended hue. Station
brightness reaches 24% with these defaults. A configurable shared-multiplier
ceiling also prevents future tuning from raising station brightness to the
existing 30% alert base (`LED_AMBIENT_STATION_CEILING_PERCENT`, currently 29%).
These are bench defaults; perceived brightness still needs physical calibration.

Tests exposed a pre-existing LED power-down divergence from AGENTS.md:
GP0 could go low without first latching an off frame. `led_update()` now
transmits and latches off before dropping GP0 whenever a pixel was lit.
A separate `pixel_lit` flag keeps this correct even when a debug command has
invalidated the cached frame. PIO still runs only for changed frames and is
disabled after the latch interval.

## Verification results

Both required builds completed with no compiler warnings:

```text
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DLUFTFUGL_DEBUG=ON
cmake --build build-debug -j4
[100%] Built target luftfugl

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DLUFTFUGL_DEBUG=OFF
cmake --build build-release -j4
[100%] Built target luftfugl
```

Both target flag files contain `-std=gnu11 -Wall -Wextra`.
No SDK sources or `pico_sdk_import.cmake` were modified.

The actual production C modules were compiled against timed hardware fakes
with `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined`, in both
Debug and Release preprocessor configurations:

```text
python3 test/test_ambient_light.py
PASS: VEML7700 wire sequence, 132.5 ms minimum, bus arbitration,
      endian/lux conversion, shutdown readback, failure/reconnect,
      no independent polling
PASS: all three hysteresis boundaries in both directions, three-sample
      debounce, outlier/error rejection, multi-zone changes and
      persistence across idle/wrap

python3 test/test_led_ambient.py
PASS: all five stations, battery warning/critical, CO2 error,
      32 breathing steps and sample flash at all four scales in RGB/RGBW;
      night values, saturation, raw mode, motion/off gates, PIO cache
      and off-before-power-down
```

After each successful firmware build, the debug ELF was flashed using the
mandated OpenOCD command. Both operations returned:

```text
RP2040 rev 2, QSPI Flash sfdp id = 0x164020 size = 4096 KiB in 1024 sectors
** Verified OK **
** Resetting Target **
```

The specified one-second wait, `stty` setup, and `reset\r` console sequence
were performed after both flashes. OpenOCD's only warning was normal image
padding / an extra erase range; there were no compiler warnings.

Actual UART status after flashing:

```text
00:00:26 status   state IDLE pos 6 target 0 dir STP duty 0 adc 2990
00:00:36 ambient  VEML7700 0x10: valid raw=143 lux=76.877 zone=dim scale=200%
00:00:36 ambient  shutdown=verified config=0x1001 samples=36 errors=0 instant=dim candidate=dim/0
00:00:38 ambient  VEML7700 0x10: valid raw=149 lux=80.102 zone=dim scale=200%
00:00:38 ambient  shutdown=verified config=0x1001 samples=38 errors=0 instant=dim candidate=dim/0
00:01:48 status   state IDLE pos 6 target 0 dir STP duty 0 adc 2990
00:01:53 led      power on on GP0; forced deep red 192,4,8; PIO0 SM0 offset 28
00:01:53 ambient  VEML7700 0x10: valid raw=108 lux=58.061 zone=dim scale=200%
00:01:53 ambient  shutdown=measuring config=0x1001 samples=111 errors=0 instant=dim candidate=dim/0
00:01:56 tick     1 kHz tick alive; min 2 us, average 3 us, max 38 us, overruns 0; watchdog 100 ms
```

`shutdown=measuring` explicitly distinguishes an in-progress conversion from
verified shutdown; the displayed `config` is the most recent shutdown
readback, not a live read while integrating.

The live LED test issued `led on`, observed power on at the confirmed 2x
scale, then restored `led auto`. No manual motion command or motion test was
issued. Post-reset observations show the mechanism idle at EVENT_POS, ADC
2990. The initial UART capture was interrupted during SWD flashing, so it
cannot establish whether startup parking moved the mechanism before those
observations. No full motion trace is claimed. The host tests simulate LED
motion gates without running a motor or issuing controller movement requests.

The existing picocom process was paused only during diagnostic captures and
resumed in `finally` blocks. The console is returned to a fresh debug menu
with the working console reset sequence after diagnostics.

Full command logs for this session are in `/tmp/luftfugl-veml-verification/`.
The live checks establish bus response, valid readings, repeated shutdown
readback, and an operating safety tick. Daylight/bright-room physical zone
sweeps, photometric accuracy against a reference meter, and shutdown current
measurements were not performed; zone behavior was exercised in host tests.

## Specification and debug-output notes

The four protected specification files were not edited. Their older fixed
brightness, five-position, UART and LED-load-switch descriptions remain
stale relative to AGENTS.md and this task. The present firmware still uses
awake-mode WFI, rather than a long-duration sleep cycle. The proportional
scaling limit at 100% and the chosen three-sample bench default are explicit
above.

The existing `led` command gained two human-facing `ambient` result lines:
`VEML7700 ... valid/no valid sample ... raw ... lux ... zone ... scale`, and
`shutdown=verified/measuring/unverified ... config ... samples ... errors ...
instant ... candidate`. Help now describes ambient scaling, existing-cycle
sampling, software shutdown, and raw-byte bypass. No production response or
new debug verb was introduced.
