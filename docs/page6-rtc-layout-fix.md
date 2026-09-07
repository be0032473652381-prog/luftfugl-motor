# Page-6 duplicate RTC output

The duplicate was a display-layout error, not a repeated sensor command.
`frame_continue()` placed the cached temperature on row 23 during a redraw.
`dbg_fields_refresh()` then placed it on the configured temperature row 22.
The latter overwrote the final command row (`step`, `stop`, `tick`, `trace`),
and row 23 was also assigned to the RTC countdown.

The full-frame renderer now leaves the temperature to its existing live-field
writer. Page 6 has non-overlapping rows, all using the existing config constants:

| Rows | Content |
|---|---|
| 3–22 | Complete command index |
| 23 | Help hint |
| 24 | Cached temperature, updated by `ds3231 temp` |
| 25 | Timer countdown |
| 26 | Command prompt |
| 27–100 | Help, command results and events |

Other pages keep their existing coordinates. Sensor reads, timer scheduling,
plain output, and command dispatch are unchanged. Help and clear use the
page-dependent result-window boundary, preserving the menu and RTC fields.
This supersedes the original Page-6 row numbers in the help-format report.

Verification:

- `python3 test/test_debug_page6.py`: compiled real C frame/field writers with
  `-Wall -Wextra -Werror`. Before the fix, it failed with
  `cached temperature rendered 2 times after redraw`. After the fix, both
  redraw and changed-temperature/concurrent-countdown scenarios pass.
- `python3 test/test_debug_help.py`: all 119 help documents still pass, plus
  all 86 general references rendered on Page 6 with the relocated prompt.
- Debug and Release: configured and built with `-Wall -Wextra`, no warnings.
- Debug ELF flashed and verified through CMSIS-DAP; 4096 KiB flash detected.
  UART `reset\r` sent afterward. No manual motion commands issued.

Terminal checks used a host ANSI emulator. The user's active picocom session
was not interrupted, and no competing UART reader was started.
