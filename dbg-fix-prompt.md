# Task — Make the Debug Interface Actually Usable

The debug build has hard-faulted the board three separate ways today and
currently does not respond to `dbg` at all. Before adding anything, find out
why it hangs.

Do not use `sed -i` by line number or `perl -0pi -e` on source files. Use
`apply_patch` with context.

---

## D0 — One firmware, everything always available

The debug menu must be present in the production build. `LUFTFUGL_DEBUG=OFF` no
longer removes any menu, including manual drive (menu 3), simulation (menu 9)
and config overrides (menu 5).

This is a deliberate development-phase decision. Keep the build option defined
so the distinction can be restored later, but it gates nothing for now.

Report the resulting production flash and RAM sizes.

> Note for the record, not for action: manual drive bypasses limit enforcement
> and simulation overrides the ADC. On a mechanism with no physical end-stops,
> neither belongs in a shipped build. This decision is scoped to development.

---

## D1 — Find the hang first

`dbg` produces no output whatsoever on the current debug build. Not a partial
menu, not a corrupted frame — nothing. The production build on identical
hardware answers normally, so the console path itself works.

Before writing any new code, determine which of these it is and report:

1. Does `dbg_enter()` get called at all? The dispatch is
   `else if (!strcmp(line, "dbg") && !argument) dbg_enter();` — confirm the
   comparison matches after the lowercasing and trailing-whitespace strip in
   `console_handle_line()`.
2. Does `dbg_enter()` return, or does it block? It calls `dbg_render()`, which
   in full-screen mode calls `dbg_screen_init()` and `dbg_frame_draw()`.
   `dbg_frame_draw()` writes roughly 2 KB through `console_debug_write()` →
   `write_text()` → `uart_putc_raw()`, which **spins when the TX FIFO is
   full**. At 115200 baud that is ~170 ms of blocking inside a single call.
3. Is the watchdog the actual killer? The tick runs from the timer IRQ so it
   should still kick during a blocking main-loop write — confirm that, because
   if `dbg_frame_draw()` is called from anywhere in tick context it is fatal.
4. Does the scratch-register restore run before the console is ready?
   `dbg_restore_mode(watchdog_reset)` is called in `main()` — check what it
   emits and when.

Report which of these it is with evidence before proceeding. If it is
something else, say so.

---

## D2 — Non-blocking output, always

The root cause of every debug crash today has been synchronous bulk UART
writes. Fix the mechanism, not the symptoms.

Add an output ring buffer, 512 bytes, drained from the main loop:

```c
static char out_buf[DEBUG_OUT_BUFFER];
static volatile uint16_t out_head, out_tail;

void dbg_out_push(const char *text);   /* enqueue, drop on overflow */
void dbg_out_drain(void);              /* main loop: emit while uart_is_writable() */
```

`dbg_out_drain()` writes only while `uart_is_writable(uart0)` returns true and
returns immediately when the FIFO is full. It never spins.

**Every** debug write goes through this — frame draws, field updates, menu
renders, event lines, trace dumps. `console_debug_write()` and
`console_debug_line()` are rerouted into it. No debug path may call
`uart_putc_raw()` directly.

Dropping output on overflow is correct and preferable: a lost menu line is a
redraw, a blocked main loop is a dead board.

---

## D3 — Echo typed input

`§14.1` already requires this and it is not working. In debug mode, every
printable character the operator types is echoed back as it arrives, and
backspace erases. `dbg_enter()` turns echo on, `dbg_exit()` turns it off.

Typing blind into a terminal is why several of today's sessions were spent
unable to tell a wedged board from a silent one.

---

## D4 — Draw the menu on entry

`dbg` must render the frame immediately, once, and return. Currently nothing
appears.

With D2 in place the frame is enqueued rather than written synchronously, so
`dbg_enter()` returns in microseconds and the main loop drains the buffer over
the following ~200 ms. That is the correct behaviour and it cannot block.

---

## D5 — Auto-enter on connect

The operator wants the interface without typing `dbg` every session.

Do **not** use the watchdog scratch register for this. That mechanism trapped
the board earlier today: the crash resumed on every reset and the only escape
was flashing a production build.

Instead: if the debug build boots and receives **any** character on UART0
within the first 5 seconds, enter debug mode automatically and consume that
character as the first keystroke. Otherwise stay in the plain console.

This gives the behaviour wanted — open the terminal, press a key, the menu
appears — with no persistent state that can survive a crash. If the interface
wedges, a reset always returns a working plain console.

`agent.md` §15.7 and `debug-functions.md` §15.7 both describe the scratch
persistence. Do not edit those documents; report that they need amending and I
will do it.

---

## D6 — Remove the trace dump

`dbg_adc_trace_dump()` has hard-faulted the board twice and produced zero
usable samples in three attempts. It is not worth further debugging effort.

Delete the dump command, the 256-entry ring, the freeze flag and the
`controller_adc_trace_*` API. Keep the tick timing statistics in menu 1 — those
work and are useful.

That reclaims about 1 KB of RAM in the debug build and removes the single
largest source of instability.

---

## Verification

Build both configurations. Then, on the flashed debug build, report actual
console output for each:

1. Board boots with no key pressed within 5 s → plain console; `status` answers
2. Board boots, a key is pressed → menu frame appears
3. In the menu, typed characters are visible as they are typed
4. `q` returns to root, `x` exits to plain console, `status` answers after exit
5. Board stays up two minutes in the menu with no watchdog reset

If any of these fail, report the failure rather than working around it. A debug
interface that is not trustworthy is worse than none — the production build has
`adc`, `jog`, `setpos` and `savepos` and is sufficient for calibration.

## D7 — Position setup menu

Add a dedicated calibration menu, reachable from the root and from any
submenu. It is built entirely around jog and save, because the integrated worm
gearbox cannot be moved by hand.

| Key | Action |
|-----|--------|
| `+` | Jog forward by the current step |
| `-` | Jog back by the current step |
| `[` / `]` | Change step size, cycling 10 / 25 / 100 / 250 / 500 |
| `1`–`5` | Select the station being set up |
| `w` | Save the current filtered ADC as the selected station's nominal |
| `e` | Export all five as paste-ready `#define POS_n_ADC` lines |

Default step 100. The selected station is shown in the header and persists
until changed.

**After every jog and every save, print the current ADC and the selected
station**, so the operator never needs a separate command to see where things
stand.

Selecting a station and saving must both be available at any time, in any
order, and repeatable. The operator will iterate: pick a station, jog toward
it, save, pick the next, and revisit earlier ones.

Saving uses the existing `setpos` validation — the resulting table must stay
strictly ascending with `POS_WINDOW` below a quarter of the smallest gap. On
rejection, say which rule failed, not just that it failed.

Every movement goes through the existing jog request path. **Do not add a route
that bypasses the safe-range check.**

### Jog range

Raise `JOG_MAX_COUNTS` to 500 and enforce a minimum of 10:

```c
#define JOG_MIN_COUNTS   10
#define JOG_MAX_COUNTS   500
#define JOG_TIMEOUT_MS   3000
```

A jog with magnitude below 10 or above 500 returns `ERR: invalid jog`. Below 10
counts is under a degree — inside the noise band and not usefully repeatable.

The timeout rises to 3000 ms because 500 counts at `DUTY_CREEP` needs roughly
110 ms of travel plus backlash and startup lag.

The safety properties are unchanged by the larger range: the endpoint is still
validated against the safe range before the motor starts, and the range is
still checked every tick during the move. A longer jog spends more time in
motion, but cannot leave the safe range.

Update `agent.md` §7 and §8 for the new limits and the setup menu.

---

Do not run the motor.
