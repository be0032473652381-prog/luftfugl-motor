# Debug Page 8 — VEML7700

Verified on 6 September 2026. Open with `8` or `page 8`.

The page displays the four configured lux ranges, zone multipliers, actual
ALS lux and raw counts, confirmed zone, hysteresis, and sample/error status.
The zone table reads the existing `AMBIENT_*` constants in `src/config.h`.

`ambient_page_fields()` in `src/debug.c:690` sends the lux text through
`field(4u, line)`. The field renderer uses `ESC[s ESC[4;1H ... ESC[K ESC[u`:
save cursor, address row 4 column 1, overwrite/clear the line, restore cursor.
The existing 1000 ms refresh loop calls it. No new sensor sampling schedule
or scrolling result/log output was added. Unchanged values are not resent;
a failed sample displays `unavailable (no valid sample)` rather than stale lux.

Navigation, the general-page index, page counter, title, help and frame-completion
checks include Page 8. `AGENTS.md` now records eight pages. The four protected
specification inputs were not edited; their older page layouts remain historical.
No new debug verb or production-protocol response was added; the existing
`page` command now accepts 8, and the direct `8` key selects the new page.
No commit was created.

Both required configurations built successfully with `-Wall -Wextra`, without
compiler warnings in the final builds. An initial signedness warning in the
page-range check was fixed before final verification.

```text
Debug:   [100%] Built target luftfugl
Release: [100%] Built target luftfugl
OpenOCD: size = 4096 KiB in 1024 sectors
         ** Verified OK **
```

The debug ELF was flashed after each successful build. Each flash was followed
by the prescribed one-second wait, UART configuration and `reset\r` command.
The probe is currently `/dev/ttyACM1`, rather than the older `/dev/ttyACM0` path.

Live UART capture verified both navigation methods and every zone-range label:

```text
Night               0 to <10 lux
Dim                 10 to <100 lux
Normal indoor day   100 to <500 lux
Bright              >= 500 lux
ALS actual          65.050 lux
```

The capture contained one initial clear-screen sequence and one ALS write to
row 4, column 1. Lux stayed unchanged during the nine-second observation;
the shadow renderer therefore suppressed duplicate ALS writes. The sample
field at row 14 demonstrated continued one-second refreshes:

```text
Capture seconds   Sample count   Errors
1.691             35             0
2.691             36             0
3.690             37             0
4.690             38             0
5.691             39             0
6.691             40             0
7.690             41             0
8.690             42             0
```

No manual motion command or motion test was issued. The post-reset status
reported `state IDLE pos 6 target 0 dir STP duty 0 adc 2986`. Startup travel
during SWD/reset was not captured, so no claim of zero physical movement is
made. The existing picocom reader was temporarily paused for the capture,
and sent SIGCONT afterward. Its shell had reclaimed the terminal foreground,
so picocom stopped again on terminal input; run `fg` in that terminal to
resume it. Page 8 was repainted and remains selected on the firmware.

Full build, flash and timestamped UART evidence is in
`/tmp/luftfugl-page8-verification/`.
