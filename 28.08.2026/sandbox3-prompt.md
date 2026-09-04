# Task — Sandbox 3: Trace Every Character Through the Real Console

Sandboxes 1 and 2 have eliminated the hardware and the timing.

| Sandbox | Result |
|---------|--------|
| 1 — bare echo | Every character echoed. **UART, wiring, probe and ground are sound.** |
| 2 — echo under 264-byte blocking writes every 200 ms | `worst block 23088 us` — the loop does stall as predicted. But `overruns 24` in 1181 blocks, and `rx 186973` vs `echoed 186972`, a difference of exactly one (the `b` keypress). **0.4% worst-case loss. Blocking is not the cause.** |

Characters reach `console_poll()` reliably even under full renderer load. So
the fault is between reading the character and the command buffer acting on it.

This sandbox finds out where. **It is a diagnostic, not a fix. Do not change
any behaviour.**

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## What to build

Add a compile-time flag `LUFTFUGL_TRACE_INPUT`, off by default. When on, the
**real firmware** — real `console_poll()`, real `dbg_handle_key()`, real
renderer — emits one line for every character received, before any handler runs
and again after.

```
IN  0x73 's'  active=1 plain=0 prompt=0 len=0 action=0
OUT 0x73 's'  consumed_by=CMDLINE len=1
IN  0x65 'e'  active=1 plain=0 prompt=0 len=1 action=0
OUT 0x65 'e'  consumed_by=CMDLINE len=2
IN  0x0d '\r' active=1 plain=0 prompt=0 len=2 action=0
OUT 0x0d '\r' consumed_by=SUBMIT  line="se"
```

Fields on the `IN` line:

| Field | Source |
|-------|--------|
| hex and printable form | the byte itself |
| `active` | `dbg_active()` |
| `plain` | `plain_mode` |
| `prompt` | the `prompt` enum value |
| `len` | `input_len` before handling |
| `action` | the `action` enum value |

The `OUT` line names **which branch consumed it**. Add a distinct tag at every
`return` in `dbg_handle_key()` and every branch in `console_poll()` that can
swallow a character:

`STOP_KEY`, `ESCAPE`, `PROMPT_CHAR`, `SUBMIT`, `BACKSPACE`, `CMDLINE`,
`DISCARD_CR`, `DISCARD_OVERFLOW`, `PROD_PARSER`, `TRACE_DUMP`, `SCREEN_SUSPEND`,
`ACTION_ABORT`, `IGNORED`, and any other exit path that exists.

If a character reaches no branch at all, print `consumed_by=NOWHERE` — that
result would be as informative as any.

---

## Where the trace output goes

**Not through the debug renderer.** It must not share a path with the thing
under test.

Write it directly with `uart_putc_raw()` at the moment it happens, before any
buffering or screen positioning. Sandbox 2 proved a 23 ms blocking write is
survivable, and this trace is far smaller. Correctness of the observation
matters more than its cost here.

Emit `\r\n` around each line so it stays readable even if the frame is being
drawn concurrently.

---

## Suppress the renderer during tracing

With `LUFTFUGL_TRACE_INPUT` on, disable the 5 Hz field refresh and the frame
draw entirely. The screen will be blank; that is intended. We are watching the
character path, not the display, and 2 KB of escape sequences would bury the
trace.

The one thing that must still run is whatever sets `dbg_active()`, since that
is a prime suspect.

---

## The test to run and report

Build with `-DLUFTFUGL_TRACE_INPUT=ON`, flash at `adapter speed 1000`, then
capture with logging:

```sh
picocom -b 115200 --logfile /tmp/s3.log /dev/ttyACM0
```

Type these, slowly, one character at a time, and report the **full trace** for
each:

1. `adc` then Enter
2. `sel 1` then Enter
3. a single `x`
4. a single `.`
5. Enter on its own

Then report which of these the trace shows:

| Observation | Conclusion |
|-------------|------------|
| No `IN` line appears when typing | Characters never reach `dbg_handle_key()` — the dispatch in `console_poll()` is wrong |
| `IN` appears with `active=0` | `dbg_active()` is false; characters go to the production parser while the debug screen is displayed |
| `IN` appears, `consumed_by` is not `CMDLINE` | A branch is stealing the character — name it |
| Characters reach `CMDLINE` but no `SUBMIT` on Enter | The line terminator handling is wrong |
| `SUBMIT` fires with the right line but nothing happens | The command lookup or its result path is broken |

**Report the raw trace lines, not a summary.** The whole point is to see the
actual bytes and branches rather than infer them.

---

## Constraints

- Change no behaviour. Every existing branch stays exactly as it is; you are
  adding observation, not altering control flow.
- The flag defaults off, and with it off the binary must be unchanged in
  behaviour and near-identical in size. Report both sizes.
- Commit as a diagnostic, clearly named.
- Do not run the motor.
