# Task — Delete the Debug Menu, Start Over Minimal

The debug interface has caused every serious problem in this project: three
hard faults, a boot loop, an unintended full-arc move, and an input path that
has never worked. Delete it entirely and replace it with the smallest thing
that is useful.

Do not use `sed -i` by line number or `perl -0pi -e` on source files. Use
`apply_patch` with context.

---

## Delete

Remove completely:

- `src/debug.c` and `src/debug.h`
- every `#ifdef LUFTFUGL_DEBUG` block in `main.c`, `console.c`, `controller.c`,
  `encoder.c` and `config.h`
- the `LUFTFUGL_DEBUG` option from `CMakeLists.txt`
- the fixed-screen renderer, all ANSI escape sequences, the output ring, the
  menu state machine, `dbg_*` functions, simulation, manual drive, the config
  override table, the history ring, timing statistics, the ADC trace
- `controller_debug_request()`, `dbg_request_t`, `DBG_OP_*`, `ST_DEBUG`,
  `motor_set_inhibit()`, `encoder_sim_*`, `cfg_t` and the `CFG_*` macros

`config.h` goes back to plain `#define` constants read directly. There is one
build configuration.

Report the resulting flash and RAM figures.

---

## Keep

The controller, encoder, motor and console modules stay as they are. In
particular **keep the safe-range check in `controller_tick()`**. It is not part
of the menu, it costs nothing to keep, and it is the only thing standing
between a mistyped command and a torn wire harness on a mechanism with no
physical end-stops.

Keep the existing bounded jog path — `controller_request_jog()` with its
endpoint validation and per-tick range check. The two new commands go through
it unchanged.

---

## The whole console

Line-oriented. No escape sequences. No screen clearing. No cursor positioning.
Plain text only.

| Command | Response |
|---------|----------|
| `adc` | `ADC 1868` |
| `jog +200` | `JOG +200 from 1868` then `DONE 2064` |
| `jog -200` | `JOG -200 from 2064` then `DONE 1866` |
| anything else | `?` |

That is the entire command set for now. Nothing else.

Jog accepts any signed value from -500 to +500. Below 10 counts or outside that
range gives `?`.

If a jog cannot start, say why in one short line:

```
BUSY          another move is running
LIMIT         1868 + 500 would leave the safe range 272..2915
FAULT         clear with a reset
```

On completion, print `DONE <adc>`. On timeout, `TIMEOUT <adc>`. On leaving the
safe range, `OVERTRAVEL <adc>` and fault.

---

## Boot

```
luftfugl 2.0.0
ADC 1868
```

Two lines. Nothing else. No banner art, no menu, no welcome text.

---

## Input handling

This is where the old version failed, so be explicit:

```
1. accumulate printable characters into a 32-byte line buffer
2. on '\r' or '\n', terminate and dispatch; swallow a following '\n' after '\r'
3. on backspace, remove one character
4. echo each character as it is accepted
5. an empty line does nothing
6. buffer overflow: discard to the next newline, respond '?'
```

No character may trigger an action before its line is submitted. There are no
single-key shortcuts of any kind — not even a stop key. `stop` is not in this
build; a reset stops the board.

---

## Verification

Report the actual console output for:

1. Boot — exactly two lines
2. `adc` — one line
3. `jog +200` — `JOG` then `DONE` with real values
4. `jog -200` — the same in reverse
5. `jog +5000` — `?`
6. `xyz` — `?`
7. `jog +200` twice in a row — the second is accepted after the first completes
8. Typing `adc` character by character — each character echoes, nothing happens
   until Enter

Item 8 is the one that has failed every time. Show it working.

Build, flash at `adapter speed 100`, commit. Do not run the motor beyond what
items 3, 4 and 7 require, and report those rather than running them if you are
unsure.
