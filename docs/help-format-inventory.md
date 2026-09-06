# Debug help conversion inventory

Baseline: `3c6c183`. The five-command sample was approved. All 86 command entries, 17 Page-5 presentations, and 16 configuration topics are now converted.

## Command table: 86 entries

| # | Command | Stage |
|---|---|---|
| 1 | `help` | Converted |
| 2 | `diag` | Converted |
| 3 | `sel` | Converted |
| 4 | `jog` | Converted |
| 5 | `step` | Converted |
| 6 | `save` | Converted |
| 7 | `stations` | Converted |
| 8 | `limits` | Converted |
| 9 | `lowendstop` | Converted |
| 10 | `highendstop` | Converted |
| 11 | `export` | Converted |
| 12 | `reset` | Converted |
| 13 | `bootsel` | Converted |
| 14 | `move` | Converted |
| 15 | `pos` | Converted |
| 16 | `goto` | Converted |
| 17 | `home` | Converted |
| 18 | `stop` | Converted |
| 19 | `status` | Converted (approved sample) |
| 20 | `adc` | Converted |
| 21 | `angle` | Converted |
| 22 | `led` | Converted |
| 23 | `led brightness` | Converted (approved sample) |
| 24 | `led zone` | Converted |
| 25 | `buzzer` | Converted |
| 26 | `buzzer crips-1` | Converted |
| 27 | `buzzer crips-2` | Converted |
| 28 | `buzzer crips-3` | Converted (approved sample) |
| 29 | `buzzer crips-4` | Converted |
| 30 | `buzzer crips-5` | Converted |
| 31 | `buzzer tone-2` | Converted |
| 32 | `buzzer tone-3` | Converted |
| 33 | `page` | Converted |
| 34 | `selftest` | Converted |
| 35 | `tick` | Converted |
| 36 | `trace` | Converted |
| 37 | `pins` | Converted |
| 38 | `pwm` | Converted |
| 39 | `cfg` | Converted |
| 40 | `sim` | Converted |
| 41 | `cal` | Converted |
| 42 | `arm` | Converted |
| 43 | `disarm` | Converted |
| 44 | `drive` | Converted |
| 45 | `findmin` | Converted |
| 46 | `batt` | Converted |
| 47 | `batt raw` | Converted |
| 48 | `batt res` | Converted |
| 49 | `batt log` | Converted |
| 50 | `batt events` | Converted |
| 51 | `batt reset` | Converted |
| 52 | `batt sim` | Converted |
| 53 | `batt sim range` | Converted |
| 54 | `batt sim warning` | Converted |
| 55 | `batt sim critical` | Converted |
| 56 | `batt chirp` | Converted (approved sample) |
| 57 | `batt chirp time` | Converted |
| 58 | `load` | Converted |
| 59 | `ina` | Converted |
| 60 | `ds3231` | Converted |
| 61 | `ds3231 start` | Converted |
| 62 | `ds3231 temp` | Converted |
| 63 | `ds3231 stop` | Converted |
| 64 | `ds3231 timer` | Converted |
| 65 | `ds3231 timeset` | Converted |
| 66 | `adc0offset` | Converted |
| 67 | `co2` | Converted |
| 68 | `co2living` | Converted |
| 69 | `co2sleeping` | Converted |
| 70 | `co2cfg` | Converted |
| 71 | `co2sim` | Converted |
| 72 | `co2limit` | Converted |
| 73 | `co2save` | Converted |
| 74 | `co2defaults` | Converted |
| 75 | `ready` | Converted |
| 76 | `serial` | Converted (approved sample) |
| 77 | `asc` | Converted |
| 78 | `offset` | Converted |
| 79 | `altitude` | Converted |
| 80 | `mode` | Converted |
| 81 | `sdc41` | Converted |
| 82 | `menu` | Converted |
| 83 | `clean` | Converted |
| 84 | `clear` | Converted |
| 85 | `plain` | Converted |
| 86 | `exit` | Converted |

## Page-5 overrides: 17 entries

These override general help on Page 5. `status` and `selftest` have different controller/sensor meanings and must remain page-sensitive.

| Command | Stage |
|---|---|
| `co2` | Converted |
| `co2living` | Converted |
| `co2sleeping` | Converted |
| `co2cfg` | Converted |
| `co2limit` | Converted |
| `co2save` | Converted |
| `co2defaults` | Converted |
| `ready` | Converted |
| `serial` | Converted (approved sample) |
| `selftest` | Converted |
| `asc` | Converted |
| `offset` | Converted |
| `altitude` | Converted |
| `mode` | Converted |
| `status` | Converted |
| `sdc41` | Converted |
| `menu` | Converted |

## Configuration-setting help: 16 topics

- `DUTY_NORMAL`
- `DUTY_APPROACH`
- `DUTY_CREEP`
- `DUTY_MIN`
- `APPROACH_COUNTS`
- `POS_WINDOW`
- `DEBOUNCE_MS`
- `BRAKE_HOLD_MS`
- `POS_1_ADC`
- `POS_2_ADC`
- `POS_3_ADC`
- `POS_4_ADC`
- `POS_5_ADC`
- `POS_6_ADC`
- `LOW_ENDSTOP_ADC`
- `HIGH_ENDSTOP_ADC`

All 16 topics use the shared structured renderer. POS_WINDOW retains its live count, angle, gap and limit descriptions. The other 15 retain the runtime-setting, live-value lookup and persistence notes.

## Additional help sources

- All six former detailed renderers (pos, led, led brightness, led zone, batt chirp, batt chirp time) are now structured documents.
- All dynamic descriptions are retained by `help_live_description()` and the snapshot preparation in `help_show()`: batt/batt sim/range/warning/critical, batt res, batt events, load, ina, led, lowendstop, highendstop, cal, buzzer, pwm, page, drive, pos and POS_WINDOW.
- Inline `batt help` now displays the same complete reference as `help batt`.
- No-argument `help` now uses the template and lists every command in four columns. Each individual command reference retains its examples.
- All 17 Page-5 help strings formerly in `co2_command_help()` have moved into the structured catalog. That display-only function and its declaration are removed; sensor command dispatch is unchanged.
- Page-6 navigation, Page-5 live-menu hints, letter shortcuts and command rejection/usage messages retain their existing behavior. They are not separate full help documents.
- Production-console help in `src/console.c` is outside the debug-menu interface and is unchanged.

## Existing discrepancies to preserve/report, not silently rewrite

- Canonical commands are `buzzer crips-1` through `crips-5`. `help_live_description("buzzer")` still says `play/play-2`; no `buzzer play-2` or `buzzer play-3` entry exists in the current table.
- The LED brightness help asserts a station ceiling relative to the alert base. Runtime brightness overrides can raise the station base; this formatting pass retains the documented statement without claiming a behavior fix.
- The existing LED raw help says it bypasses percentage/ambient scaling. It does not say it bypasses motion safety gates. The sample preserves that narrower statement.
- General `help pos` still describes station 6 as reserved for CO2 errors; AGENTS.md uses EVENT_POS. This is retained in the baseline for review, not silently changed.

## Compatibility

All 86 command names and their order remain intact. The recognition table now stores only names; the old example/limits/notes display fields have been replaced by `debug_help_document_t` with separate counted arrays for syntax, parameters, interactions and notes. Existing `resolve()` and `help_detail()` functions are byte-for-byte unchanged. Their reads use only the retained name field.

Presentation lookup follows command resolution. Page-5 status and selftest remain sensor-specific. Unknown help still follows the rejection path. No command was added, removed or renamed, and no motor, LED, sensor or battery control behavior was changed.

See [exact baseline](help-format-baseline.json), [approved before/after review](help-format-review.md), and [completion and verification report](help-format-completion.md).
