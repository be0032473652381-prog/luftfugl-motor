# Debug help format completion

All **86 command entries**, **17 Page-5 variants**, and **16 configuration topics** use the approved format. No entries were skipped or dropped. `batt help` uses the same document as `help batt`; no-argument `help` lists all 86 names. The full per-entry checklist is in [the inventory](help-format-inventory.md), and [all rendered references](help-format-reference.md) are available for review.

The user approved the five-command sample, with one addition: start help with a blank line below the command prompt. Plain output emits a leading CR/LF. Fixed-screen output inserts a blank row at row 25, so the header begins at row 26; rows 1–24, including the menu and prompt, are preserved.

```text
 Command > help batt chirp

BATT CHIRP — tone for the repeating critical-battery audible alert

SYNTAX    batt chirp [<frequency> kHz] [/s]
          batt chirp -> report the active chirp frequency and default source
```

## Content preservation

[The approved before/after review](help-format-review.md) quotes the original and rendered text for status, led brightness, batt chirp, buzzer crips-3 and serial, including the Page-5 serial variant. [The baseline JSON](help-format-baseline.json) preserves all original command records, Page-5 text, detailed helpers and the command-resolution functions from commit `3c6c183`.

The automated audit verifies original example, limit and description wording for all 76 entries outside the ten index/detailed/approved cases. All 17 Page-5 factual descriptions survive verbatim. The detailed references retain their defaults, ranges, behavior and examples, checked individually for LED colours, wire formats, pins, power timing, scaling, hysteresis, station safety, chirp timing, persistence and sensor behavior. Wording is grouped into sections and shortened in purpose/table cells; no defaults, ranges or firmware behavior were changed. Additional syntax variants are taken from the existing command parser.

Existing discrepancies are explicitly retained for review, rather than presented as firmware fixes: the LED station-ceiling statement does not describe every runtime override; the old position help describes station 6 as reserved for CO2 errors; the buzzer description still contains historical play/play-2 terminology. The inventory records these differences. The generic RAM-only configuration help also predates the implemented end-stop persistence in `cfg_update()`; this formatting task does not change it.

## Representation and dispatch

`debug_help_document_t` contains purpose plus separate counted arrays of syntax lines, parameter rows, interactions and notes. Optional live-note arrays attach the current position, gap, battery thresholds and other diagnostic snapshots without allocation. One renderer controls conditional sections and order; a dedicated output sink wraps complete text at column 1, without the old truncated command-label column.

The recognition table retains every name in its original order, with its former display fields removed. `resolve()` and `help_detail()` are byte-for-byte unchanged; tests compare them with the baseline. They read only names. The old Page-5 display text moved out of `co2.c` into the catalog; actual sensor command dispatch and the production console are unchanged. No new command was added.

## Verification performed

`python3 test/test_debug_help.py` compiles the real renderer, output emitter and help snapshot preparation with host C11, `-Wall -Wextra -Werror`. It checks all 119 documents in plain and ANSI output, original names and resolution order, section order, conditional tables, leading CR/LF, wrapping without lost words, preservation of menu rows, Page-5 distinctions and live POS_WINDOW updates. The longest complete response uses **52 of 76 available result rows**, including the leading blank and live values.

Selected actual test output:

```text
PASS: command resolution and help abbreviation functions unchanged
PASS: all 86 command names and their resolution order unchanged
PASS: 76 standard entries retain every original example, limit and description
PASS: detailed LED, position, chirp and sensor facts spot-checked
PASS: live snapshots preserved; longest help uses 52/76 result rows (led)
PASS: 86 commands, 17 Page-5 variants, 16 settings; no missing topics
```

Both required configurations built successfully, with `-Wall -Wextra` present in the firmware compile flags and no warnings or errors:

```text
Debug:   [100%] Built target luftfugl
Release: [100%] Built target luftfugl
```

The debug ELF was flashed through CMSIS-DAP and verified:

```text
Info : RP2040 rev 2, QSPI Flash sfdp id = 0x164020 size = 4096 KiB in 1024 sectors
** Verified OK **
** Resetting Target **
```

The raw UART was configured for 115200 8N1 and `reset\r` was sent through the stable Debug Probe serial path (currently `/dev/ttyACM4`) after the required delay. No manual motion commands were issued. Resets run normal firmware startup.

The live UART regression script was updated for the new headings, but was **not run** because the user's picocom session owns the serial stream. Terminal layout was verified with the host ANSI emulator; no user terminal process was stopped or interrupted. Build/test/flash logs for this session are `/tmp/help-debug-build.log`, `/tmp/help-release-build.log`, `/tmp/help-tests.log`, and `/tmp/help-format-flash.log`.
