# Help format — five-command review checkpoint

This is the approved five-command checkpoint: `status`, `led brightness`, `batt chirp`, `buzzer crips-3`, and `serial` (general and Page 5). The remainder is now complete; see [completion report](help-format-completion.md).

**Before** blocks quote original documented strings and labels, in their source order, before the old terminal renderer added/truncated command-label columns and inserted rows. They are content transcripts, not claimed UART captures. **After** blocks come from the compiled new renderer, wrapped to the existing 78-column text width. Firmware, defaults, limits and command behavior are unchanged.

The parameter table appears only for LED brightness. Its syntax uses <parameter> to keep the syntax on one terminal line; all six accepted names remain in the table. Single-parameter commands use syntax and notes. Empty sections are omitted. Existing generic diagnostic notes are retained where the old renderer supplied them, so their wording can be explicitly reviewed rather than silently discarded.

## status (page 1)

Before:

```text
Example: Command > status
Parameters: read-only
Syntax: status
Purpose: Shows the full controller state.
Operational notes: Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.
```

After (now preceded by the requested blank line below the prompt):

```text

STATUS — Shows the full controller state.

SYNTAX    status

NOTE
Read-only.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## led brightness (page 6)

Before:

```text
LED BRIGHTNESS COMMAND — complete reference
Purpose: set the base percentage for one automatic LED indication.
Syntax: led brightness [station|warning|critical|error|sample|breathe] [0..100]
With no argument, lists all six current RAM values.
station: base for all five CO2 station colours (default 3%).
warning: battery-warning orange double flash (default 30%).
critical: battery-critical orange hazard flash (default 30%).
error: CO2 sensor-error red flash (default 30%).
sample: startup accepted-sample warm-white flash (default 10%).
breathe: maximum SCD41 warm-up breathing level (default 10%).
Example: led brightness station 5
Example: led brightness warning 40
Example: led brightness critical 40
Example: led brightness error 25
Example: led brightness sample 15
Example: led brightness breathe 7
Example: led brightness
Reset: led brightness reset restores all compiled defaults.
Values are RAM-only and return to defaults after reset; no flash is written.
The selected base is multiplied by the confirmed VEML7700 zone multiplier.
The final channel value is rounded and saturated at 100%; alert pulses may saturate.
Station brightness remains below the alert base through the station ceiling.
LED is still forced off while moving, between stations, or in forced-off mode.
led raw <hex> is a diagnostic wire word and bypasses this percentage control.
```

After (now preceded by the requested blank line below the prompt):

```text

LED BRIGHTNESS — base % for one automatic indication

SYNTAX    led brightness <parameter> <0..100>
          led brightness -> lists all six current RAM values
          led brightness reset -> restores all compiled defaults

PARAMETER  MEANING                        DEFAULT  EXAMPLE
station    all five CO2 station colours   3%       led brightness station 5
warning    battery-warning flash          30%      led brightness warning 40
critical   battery-critical flash         30%      led brightness critical 40
error      CO2 sensor-error red flash     30%      led brightness error 25
sample     startup accepted-sample flash  10%      led brightness sample 15
breathe    SCD41 warm-up maximum          10%      led brightness breathe 7

INTERACTIONS, most consequential first
- The selected base is multiplied by the confirmed VEML7700 zone multiplier.
  The final channel value is rounded and saturated at 100%; alert pulses may
  saturate.
- Station brightness remains below the alert base through the station ceiling.
- LED is still forced off while moving, between stations, or in forced-off
  mode.
- led raw <hex> is a diagnostic wire word and bypasses this percentage
  control.

NOTE
Values are RAM-only and return to defaults after reset; no flash is written.
Warning uses an orange double flash; critical uses an orange hazard flash; the
  startup accepted-sample flash is warm-white.
```

## batt chirp (page 6)

Before:

```text
Syntax: batt chirp [<frequency> kHz] [/s]
Range: 0.10 to 10.00 kHz (100 to 10000 Hz)
Status: batt chirp - report the active chirp frequency and default source
Set RAM: batt chirp 3.50 kHz - use 3.5 kHz until reboot
Save flash: batt chirp 3.50 kHz /s - save it as the power-on default
Trigger: Sounds only while a valid battery reading is below the critical threshold
Timing: Use help batt chirp time to configure sequence interval, repeat, pause, and duration
Recovery: Stops immediately when voltage is no longer critical; no boundary chirp
Independence: Audible alert remains active regardless of LED auto/on/off/raw mode
Persistence: /s saves range, warning, critical, and chirp defaults together
Default: Compiled default is 2.700 kHz when no valid flash record exists
Examples: batt chirp | batt chirp 0.10 kHz | batt chirp 10.00 kHz /s
```

After (now preceded by the requested blank line below the prompt):

```text

BATT CHIRP — tone for the repeating critical-battery audible alert

SYNTAX    batt chirp [<frequency> kHz] [/s]
          batt chirp -> report the active chirp frequency and default source

INTERACTIONS, most consequential first
- Sounds only while a valid battery reading is below the critical threshold.
- Stops immediately when voltage is no longer critical; no boundary chirp.
- Audible alert remains active regardless of LED auto/on/off/raw mode.
- Use help batt chirp time to configure sequence interval, repeat, pause, and
  duration.

NOTE
Frequency range: 0.10 to 10.00 kHz (100 to 10000 Hz).
Compiled default is 2.700 kHz when no valid flash record exists.
batt chirp 3.50 kHz - use 3.5 kHz until reboot.
batt chirp 3.50 kHz /s - save it as the power-on default.
/s saves range, warning, critical, and chirp defaults together.
Examples: batt chirp | batt chirp 0.10 kHz | batt chirp 10.00 kHz /s
```

## buzzer crips-3 (page 6)

Before:

```text
Example: Command > buzzer crips-3 10
Parameters: 1..200 complete calls; off stops
Syntax: buzzer crips-3 10
Purpose: Listening test only: three-part clean-sine sequence; 8/8/12 bursts, 50 ms breaks; 444 ms total, 100 ms repeat gap.
Operational notes: Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.
```

After (now preceded by the requested blank line below the prompt):

```text

BUZZER CRIPS-3 — Listening test only: three-part clean-sine sequence.

SYNTAX    buzzer crips-3 <1..200>

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
Count is 1..200 complete calls. Example: buzzer crips-3 10.
Listening test only: three-part clean-sine sequence; 8/8/12 bursts, 50 ms
  breaks; 444 ms total, 100 ms repeat gap.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## serial (page 1)

Before:

```text
Example: Command > serial
Parameters: no arguments
Syntax: serial
Purpose: Stops periodic measurement briefly and reads the SCD41 48-bit serial number.
Operational notes: Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.
```

After (now preceded by the requested blank line below the prompt):

```text

SERIAL — read the 48-bit SCD41 serial number

SYNTAX    serial

INTERACTIONS, most consequential first
- Stops periodic measurement briefly and reads the SCD41 48-bit serial number.

NOTE
No arguments.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## serial (page 5)

Before:

```text
Purpose: serial: read the 48-bit SCD41 serial number
SDC41 Page-5 command
```

After (now preceded by the requested blank line below the prompt):

```text

SERIAL — read the 48-bit SCD41 serial number

SYNTAX    serial

NOTE
SDC41 Page-5 command.
```

## Factual spot-checks

| Entry | Preserved details |
|---|---|
| status | Full controller state; read-only; existing diagnostic/RAM boilerplate. |
| led brightness | Six names; defaults 3/30/30/30/10/10%; 0..100 range; all seven original examples; reset; RAM/no-flash behavior; scaling, saturation, station ceiling, motion/off gates and raw bypass; orange double/hazard, red and warm-white colors. |
| batt chirp | 0.10..10.00 kHz / 100..10000 Hz; 2.700 kHz default; default-source reporting; valid critical-battery trigger; immediate recovery/no boundary chirp; LED independence; timing reference; bundled flash persistence; all examples. |
| buzzer crips-3 | 1..200 complete calls; example count 10; off stops; listening-only; three parts; 8/8/12 bursts; 50 ms breaks; 444 ms total; 100 ms repeat gap; existing generic note. |
| serial | 48-bit SCD41 serial number; general help retains no-argument and temporary measurement-stop facts; Page-5 help retains its page-specific marker. |

No factual corrections were made. Purposes and parameter meanings are condensed; repeated facts are grouped. The baseline source strings remain available in help-format-baseline.json for comparison.

## Implementation and verification

- `src/debug_help.h`: structured document with counted syntax, parameter, interaction and note arrays.
- `src/debug_help.c`: shared conditional-section renderer. Natural order for plain output; reversed logical lines for the fixed screen insertion mechanism.
- `src/debug.c`: separate display lookup after existing command resolution; structured emitter avoids label indentation and 160-byte truncation.
- `test/test_debug_help.py`: compiles real renderer and actual ANSI emitter; checks section order, conditional sections, reverse ordering, defaults/examples, Page-5 context selection, complete text after wrapping, and preservation of the fixed menu.
- Debug and Release builds: `-Wall -Wextra`; full results reported at the review checkpoint.

**Approved:** the user accepted the five-command sample and requested a leading CR/LF. That blank line is implemented in both plain and fixed-screen help; all remaining entries are converted.
