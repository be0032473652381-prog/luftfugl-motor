# Debug Monitor: Menu and Function Description

Interactive diagnostic interface for the luftfugl motor subsystem. Layered on
top of the production console described in `agent.md` §7, compiled only when
`LUFTFUGL_DEBUG` is defined.

This document specifies every menu, every function behind it, and the safety
interlocks that make manual motor control survivable on a mechanism with no
physical end-stops.

---

## 1. Design Rules

Four rules constrain the whole design. Everything below follows from them.

**1. The production protocol stays flat and machine-parseable.**
`pos`, `move N`, `stop`, `status` and `home` must keep working exactly as
specified, with one-line responses and no prompts. Host software and scripts
depend on that. The menu system is a *mode*, entered deliberately and exited
deliberately, never something the normal protocol can fall into by accident.

**2. Debug code never writes the motor directly.**
Invariant 7 in `function-description.md` §9 says all `motor_*` calls originate
in `controller_tick()`. Debug is not an exception. Manual motion is requested
through the mailbox and executed by a new controller state, `ST_DEBUG`.
Bypassing this would put motor writes in the main loop, racing the control
tick.

**3. Manual motion is armed, bounded and interruptible.**
Nothing in the debug menu can start an unbounded movement. Every manual drive
carries a duration cap, requires an explicit arm step, and stops on any
keystroke.

**4. Debug output never blocks the tick.**
Menus render in the main loop. Telemetry streaming is driven by a main-loop
timer, not the 1 kHz IRQ, and shares the existing event queue discipline.

---

## 2. Entering and Leaving

| Input | Effect |
|-------|--------|
| `dbg` (from normal mode) | Enter debug mode, render root menu |
| `q` | Go up one menu level |
| `x` (any level) | Leave debug mode, return to normal protocol |
| `?` | Re-render the current menu |
| **any key during motion** | Immediate brake, abort the running action |

Leaving debug mode always brakes the motor, disarms manual control, stops
telemetry streaming, and discards any temporary configuration overrides that
were not explicitly committed.

```
> dbg
=== luftfugl debug 1.0 ===
state IDLE  pos 3  adc 1311  armed NO
 1 status & telemetry
 2 encoder & adc
 3 motor manual        [locked]
 4 calibration
 5 configuration
 6 faults & history
 7 self-test
 x exit
dbg>
```

The header line re-renders with every menu so state, position and arm status
are never more than one screen away.

### Core Functions

```c
void dbg_init(void);
void dbg_enter(void);
void dbg_exit(void);
bool dbg_active(void);
void dbg_poll(void);                    // called from main loop
void dbg_handle_key(char c);
void dbg_render(void);
void dbg_render_header(void);
void dbg_abort(void);                   // brake + cancel running action
```

| Function | Contract |
|----------|----------|
| `dbg_init` | Clears menu state, disarms, zeroes counters. Called once from `main()`. |
| `dbg_enter` | Only from normal mode and only when `state != ST_FAULT` unless forced. Renders root. |
| `dbg_exit` | Brakes, disarms, stops streaming, drops overrides, returns to normal parsing. |
| `dbg_poll` | Main-loop entry. Services streaming, action timeouts and re-render requests. |
| `dbg_handle_key` | Dispatches one character to the active menu handler. |
| `dbg_abort` | Posts `REQ_STOP`, clears the active action, prints `ABORTED`. |

---

## 3. Menu Tree

```
root
├── 1  status & telemetry
│    ├── s  state dump
│    ├── t  toggle telemetry stream
│    ├── r  set stream rate
│    ├── k  tick timing statistics
│    └── z  reset timing statistics
├── 2  encoder & adc
│    ├── a  single reading
│    ├── m  live monitor
│    ├── c  capture min/max
│    ├── b  band table
│    └── e  band margin
├── 3  motor manual              [arm required]
│    ├── A  arm / disarm
│    ├── f  pulse forward
│    ├── v  pulse reverse
│    ├── d  set pulse duty
│    ├── t  set pulse duration
│    ├── b  brake
│    ├── c  coast
│    ├── s  standby toggle
│    └── n  find minimum duty
├── 4  calibration               [coupled confirm for motion tests]
│    ├── p  record position readings
│    ├── s  measure step time
│    ├── w  measure full travel time
│    ├── o  measure overshoot
│    └── r  print suggested config
├── 5  configuration
│    ├── l  list constants
│    ├── s  set override
│    ├── d  reset to defaults
│    └── e  export as config.h
├── 6  faults & history
│    ├── f  last fault
│    ├── h  position history
│    ├── c  counters
│    ├── z  reset counters
│    └── k  clear fault
└── 7  self-test
     ├── s  static tests
     └── m  motion tests         [coupled confirm]
```

---

## 4. Menu 1 — Status & Telemetry

Read-only. Safe at any time, coupled or uncoupled.

```c
void dbg_status_dump(void);
void dbg_stream_toggle(void);
void dbg_stream_set_rate(uint16_t hz);
void dbg_timing_stats(void);
void dbg_timing_reset(void);
```

| Key | Function | Behaviour |
|-----|----------|-----------|
| `s` | `dbg_status_dump` | Multi-line dump: state, position, target, direction, duty, deadline remaining, last direction, filter output, arm status, uptime. |
| `t` | `dbg_stream_toggle` | Starts or stops periodic telemetry. |
| `r` | `dbg_stream_set_rate` | Prompts for rate in Hz, 1–50. Above 50 Hz the UART becomes the bottleneck at 115200 baud. |
| `k` | `dbg_timing_stats` | Min, max and mean `controller_tick()` duration in microseconds, plus the count of ticks that overran 1000 µs. |
| `z` | `dbg_timing_reset` | Zeroes the timing accumulators. |

Telemetry line format, one per interval, chosen to be both readable and
trivially parseable by a host plotting script:

```
T <ms> <state> <pos> <target> <dir> <duty> <raw> <avg>
T 14320 APPROACH 3 4 FWD 60 1998 2011
```

### Tick Timing Instrumentation

`controller_tick()` brackets itself with `time_us_32()` and accumulates min,
max and sum into a small struct. The cost is two timer reads per tick. Keep it
compiled in during bring-up: the overrun counter is the fastest way to notice
that something added to the control path has started costing too much.

```c
typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
    uint32_t count;
    uint32_t overruns;      // ticks exceeding 1000 us
} tick_stats_t;
```

---

## 5. Menu 2 — Encoder & ADC

Read-only. This is the menu that fills in the measured column of
`hardware.md` §5.

```c
void dbg_adc_read_once(void);
void dbg_adc_monitor_toggle(void);
void dbg_adc_capture_toggle(void);
void dbg_band_table(void);
void dbg_band_margin(void);
```

| Key | Function | Behaviour |
|-----|----------|-----------|
| `a` | `dbg_adc_read_once` | Prints raw, averaged, classified band, and whether the classification is currently confirmed. |
| `m` | `dbg_adc_monitor_toggle` | Continuous readout at 10 Hz until any key. For rotating the mechanism by hand and watching the bands change. |
| `c` | `dbg_adc_capture_toggle` | Starts capture; records min and max of the averaged value until stopped. Run this across a full traverse to measure ripple under motor load. |
| `b` | `dbg_band_table` | Prints the active band edges from RAM — the compiled defaults, or overrides from menu 5. |
| `e` | `dbg_band_margin` | For the current reading, distance to the nearest band edge, absolute and as a percentage of band width. |

`dbg_band_margin` is the one to use when deciding whether the ladder resistors
are good enough. Margins under about 15 % at any position mean the
classification is one supply dip away from being wrong.

Capture output:

```
CAPTURE pos=4 samples=1843 min=1966 max=2071 ripple=105 band=1679..2431 margin=287/13%
```

---

## 6. Menu 3 — Motor Manual

**This is the only part of the system that can move the motor without limit
enforcement.** It exists because bring-up stage 3 requires it, and it is
interlocked accordingly.

```c
bool dbg_motor_arm(void);
void dbg_motor_disarm(void);
bool dbg_motor_armed(void);
void dbg_motor_pulse(direction_t dir, uint8_t duty, uint16_t ms);
void dbg_motor_brake(void);
void dbg_motor_coast(void);
void dbg_motor_standby(bool on);
void dbg_motor_find_min(direction_t dir);
```

### The Arming Interlock

`dbg_motor_arm()` prompts:

```
Manual drive bypasses position limits.
The mechanism has NO physical end-stops.
Type UNCOUPLED to confirm the motor is disconnected:
```

Only the exact string `UNCOUPLED` arms it. Anything else aborts. Once armed:

- A banner line appears in every menu header: `armed YES`
- Arming expires automatically after 120 s of inactivity
- Leaving debug mode, entering `ST_FAULT`, or any abort disarms immediately

Requiring the operator to type a word that asserts a physical fact — rather
than pressing `y` — is deliberate. It is hard to do by reflex.

### Two Interlocks, Not One

Arming with `UNCOUPLED` gates **menu 3 only**. It is the interlock for
*unguarded* motion — `ST_DEBUG`, limits bypassed, mechanism disconnected.

Menus 4 and 7 are different. Their motion routines run closed-loop through the
normal state machine with full limit enforcement, and they require the
mechanism to be **coupled** — measuring step time or verifying arrivals is
meaningless with the motor spinning free. Gating them behind `UNCOUPLED` would
force the operator to assert the opposite of what is true.

They therefore use a separate, weaker interlock:

```c
bool dbg_coupled_confirm(void);
void dbg_coupled_clear(void);
bool dbg_coupled(void);
```

`dbg_coupled_confirm()` prompts:

```
This test moves the mechanism under closed-loop control.
Position limits ARE enforced. The mechanism must be connected.
Type COUPLED to confirm:
```

| | Menu 3 | Menus 4 and 7 |
|---|---|---|
| Phrase | `UNCOUPLED` | `COUPLED` |
| Limits | bypassed | enforced |
| State | `ST_DEBUG` | normal states |
| Mechanism | disconnected | connected |
| Header | `armed YES` | `coupled YES` |

**The two are mutually exclusive.** Confirming either clears the other, and the
header never shows both. This is the point of splitting them: the firmware can
never be in a mode where limits are bypassed *and* the operator has asserted
the mechanism is attached. Both expire after 120 s of inactivity and clear on
exit, abort, or `ST_FAULT`.

`dbg_cal_positions` is motion-free and needs neither.

| Key | Function | Behaviour |
|-----|----------|-----------|
| `A` | `dbg_motor_arm` / `dbg_motor_disarm` | Toggles the interlock. |
| `f` | `dbg_motor_pulse(DIR_FWD, …)` | Drives forward at the configured duty for the configured duration, then brakes. |
| `v` | `dbg_motor_pulse(DIR_REV, …)` | As above, reverse. |
| `d` | — | Prompts for pulse duty, 0–255. Values below `DUTY_MIN` are accepted here but flagged. |
| `t` | — | Prompts for pulse duration, 10–2000 ms. Hard-capped at 2000. |
| `b` | `dbg_motor_brake` | Short brake. |
| `c` | `dbg_motor_coast` | Outputs high-impedance. Prints a reminder that the mechanism can now be moved by hand — and will coast. |
| `s` | `dbg_motor_standby` | Toggles STBY. Off releases the motor entirely. |
| `n` | `dbg_motor_find_min` | Automated ramp, see below. |

### `dbg_motor_pulse` Contract

*Pre-conditions:* armed, and `controller_state() == ST_IDLE` or `ST_DEBUG`.
Refuses otherwise with `ERR: not armed` or `ERR: busy`.

*Behaviour:* posts `REQ_DEBUG_DRIVE` with direction, duty and duration. The
controller enters `ST_DEBUG`, drives, and brakes when the duration expires.
Duration is enforced by the controller's own deadline mechanism, so it holds
even if the main loop stalls.

*Post-condition:* motor braked, state back to `ST_DEBUG` idle.

### `dbg_motor_find_min`

Ramps duty upward from `DUTY_MIN - 20`, in steps of 5, driving 150 ms at each
level and watching the averaged ADC for any change beyond the measured noise
floor. Reports the first duty that produces detectable motion and brakes.

```
FINDMIN dir=FWD
  duty 25  no motion
  duty 30  no motion
  duty 35  no motion
  duty 40  MOTION (adc 1309 -> 1487)
FINDMIN result=40  suggest DUTY_MIN=45 (result +10% margin)
```

Aborts on any key. Aborts automatically if duty reaches 120 without motion —
that indicates a wiring or supply problem, not a stiction threshold.

---

## 7. Menu 4 — Calibration

Guided measurement routines. Each produces a number that feeds a constant in
`config.h`. This menu is why the bring-up numbers stop being guesses.

```c
void dbg_cal_positions(void);
void dbg_cal_step_time(void);
void dbg_cal_travel_time(void);
void dbg_cal_overshoot(void);
void dbg_cal_report(void);
```

Motion routines here require `dbg_coupled()`, not `dbg_motor_armed()`. They
drive through the normal state machine and limits stay enforced throughout.

| Key | Function | Feeds | Interlock |
|-----|----------|-------|-----------|
| `p` | `dbg_cal_positions` | `BAND_*_MAX` | none — motion-free |
| `s` | `dbg_cal_step_time` | `TIMEOUT_STEP_MS` | `COUPLED` |
| `w` | `dbg_cal_travel_time` | `TIMEOUT_HOME_MS` | `COUPLED` |
| `o` | `dbg_cal_overshoot` | `DUTY_APPROACH`, `DUTY_CREEP` | `COUPLED` |
| `r` | `dbg_cal_report` | — | none |

### `dbg_cal_positions`

Interactive and motion-free — safe with the mechanism coupled. Prompts the
operator to place the mechanism at each position by hand, samples for 500 ms at
each, and records mean and spread.

```
CAL POSITIONS
 Move to position 1, press SPACE (q to abort)
  pos1 mean=374 spread=6
 Move to position 2, press SPACE
  pos2 mean=741 spread=8
 ...
 Move between reeds, press SPACE
  open mean=4093 spread=3
COMPUTED BANDS
  1: 0..557    (nominal 0..555)
  2: 558..1026 (nominal 556..1023)
  ...
```

Recomputes midpoints from measured means rather than nominal values, and flags
any adjacent pair whose separation is under 200 counts as marginal.

### `dbg_cal_step_time`

Requires a coupled mechanism and a known valid position. Runs closed-loop
single steps through the normal state machine — it does **not** bypass limits.
Moves 3→4, 4→3, 3→2, 2→3, timing each from drive start to `ARR`.

```
CAL STEP
  3->4  412 ms
  4->3  398 ms
  3->2  405 ms
  2->3  421 ms
  worst=421  suggest TIMEOUT_STEP_MS=900 (worst x2, rounded up)
```

### `dbg_cal_travel_time`

Times a full home from position 5 at `DUTY_CREEP`. Suggests
`TIMEOUT_HOME_MS` as twice the measured time. Refuses to run unless the current
position is confirmed as 5.

### `dbg_cal_overshoot`

Commands `move 3` from position 2 and from position 4, then reports whether the
averaged ADC settles inside band 3 and how far from the nominal centre it
lands. Repeats at three approach duties so the operator can see the trend.

```
CAL OVERSHOOT target=3 nominal=1309
  duty 80  settle=1402  offset=+93   MARGINAL
  duty 60  settle=1338  offset=+29   OK
  duty 40  settle=1315  offset=+6    OK
  suggest DUTY_APPROACH=60
```

### `dbg_cal_report`

Prints every calibration result gathered this session as a block ready to paste
into `config.h`, with unmeasured values marked so nothing silently keeps a
default.

```
// generated by dbg_cal_report, session uptime 00:14:22
#define BAND_P1_MAX   557
#define BAND_P2_MAX   1026
#define BAND_P3_MAX   1681
#define BAND_P4_MAX   2429
#define BAND_P5_MAX   3450
#define DUTY_MIN      45
#define DUTY_APPROACH 60
#define TIMEOUT_STEP_MS 900
#define TIMEOUT_HOME_MS /* NOT MEASURED - default 6000 retained */ 6000
```

---

## 8. Menu 5 — Configuration

Temporary RAM overrides so constants can be tried without a rebuild-flash
cycle. Overrides are volatile and lost on reset — deliberately, so no tuning
session can accidentally become permanent behaviour.

```c
void dbg_cfg_list(void);
bool dbg_cfg_set(const char *key, int32_t value);
void dbg_cfg_reset(void);
void dbg_cfg_export(void);
```

| Key | Function | Behaviour |
|-----|----------|-----------|
| `l` | `dbg_cfg_list` | Lists every override-able constant with its compiled default, current value, and whether it is overridden. |
| `s` | `dbg_cfg_set` | Prompts for key and value. Validates range. |
| `d` | `dbg_cfg_reset` | Restores all compiled defaults. |
| `e` | `dbg_cfg_export` | Prints current values as `#define` lines. |

Override-able: all four duty constants, all three timeouts, the five band
edges, `DEBOUNCE_MS`, `BRAKE_HOLD_MS`.

**Not** override-able: pin numbers, `PWM_WRAP`, `PWM_CLKDIV`, `FILTER_DEPTH`,
`TICK_HZ`. These are structural — changing them at runtime would require
reinitialising hardware or reallocating buffers, and the failure modes are
worse than a rebuild.

`dbg_cfg_set` validation rejects anything that would break an invariant:
duties above 255, `DUTY_APPROACH` or `DUTY_CREEP` below `DUTY_MIN`, band edges
out of ascending order, timeouts under 100 ms.

```
dbg cfg> s
key: DUTY_APPROACH
value: 30
ERR: below DUTY_MIN (45) - motor would stall
```

---

## 9. Menu 6 — Faults & History

Post-mortem data. The TB6612FNG has no fault output, so this menu is the only
forensic trail available when something goes wrong.

```c
void dbg_fault_show(void);
void dbg_history_dump(void);
void dbg_counters_show(void);
void dbg_counters_reset(void);
void dbg_fault_clear(void);
```

| Key | Function | Behaviour |
|-----|----------|-----------|
| `f` | `dbg_fault_show` | Last fault kind, uptime at which it occurred, state and position at the time, target, and elapsed time against the deadline. |
| `h` | `dbg_history_dump` | The position history ring, newest last. |
| `c` | `dbg_counters_show` | Cumulative counters, see below. |
| `z` | `dbg_counters_reset` | Zeroes counters. |
| `k` | `dbg_fault_clear` | Clears `ST_FAULT` without homing. Prompts for confirmation and warns that the position is unknown until a `home`. |

### Position History Ring

Sixteen entries, written by `controller_tick()` on every confirmed position
change. Two words each: uptime milliseconds and the position, with a flag for
whether it was a `PASS` or a confirmed `ARR`.

```c
typedef struct {
    uint32_t  ms;
    position_t pos;
    uint8_t   kind;      // 0 = pass, 1 = arrive, 2 = unknown entered
} hist_entry_t;
```

This is what tells you whether a timeout happened because the motor never moved
or because it moved and the reed was missed — two failures with identical
symptoms from the outside and completely different causes.

```
HISTORY (newest last)
  12040  ARR 3
  14210  PASS 4
  14980  UNKNOWN
  16500  UNKNOWN     <- never re-acquired
  17710  TIMEOUT
```

### Counters

| Counter | Meaning |
|---------|---------|
| `moves_ok` | Moves completing with `ARR` |
| `moves_timeout` | Moves hitting their deadline |
| `recover_entered` | Times the position went unknown mid-operation |
| `recover_ok` | Recoveries that re-acquired a position |
| `faults` | Transitions into `ST_FAULT` |
| `limit_rejects` | Commands rejected as `ERR: at end-stop` |
| `pass_events` | Reeds crossed in transit |
| `tick_overruns` | Control ticks exceeding 1000 µs |

A rising `recover_entered` with a healthy `recover_ok` means the encoder is
marginal but the recovery logic is working. A rising `limit_rejects` means
whatever is commanding the system does not know where it is.

---

## 10. Menu 7 — Self-Test

```c
bool dbg_selftest_static(void);
bool dbg_selftest_motion(void);
```

### `dbg_selftest_static` — no motion, always safe

| Check | Pass condition |
|-------|----------------|
| ADC responds | Ten reads, not all identical, none 0 or 4095 unless genuinely open |
| Band table ordered | Every edge strictly greater than the previous |
| Band coverage | No gaps between bands, open band starts immediately after `BAND_P5_MAX` |
| Duty ordering | `DUTY_MIN <= DUTY_CREEP <= DUTY_APPROACH <= DUTY_NORMAL` |
| Timeout sanity | `TIMEOUT_HOME_MS >= 4 × TIMEOUT_STEP_MS` |
| Tick alive | Tick counter advancing at 1000 ± 20 Hz |
| Event queue | Not persistently full |
| Position valid | Confirmed position is `POS_UNKNOWN` or 1–5 |

Run this after every configuration change. The band-ordering and coverage
checks in particular would have caught the arithmetic error in the original
band table, where position 4's nominal reading fell inside position 5's band.

### `dbg_selftest_motion` — requires `COUPLED` confirmation

Sequence: home, then step 1→2→3→2→1, verifying that each `ARR` matches the
commanded target and that no step exceeds its deadline. Reports pass or fail
per step and brakes on the first failure.

```
SELFTEST MOTION
  home        ARR 1   4980 ms  PASS
  1->2        ARR 2    410 ms  PASS
  2->3        ARR 3    402 ms  PASS
  3->2        ARR 2    398 ms  PASS
  2->1        ARR 1    431 ms  PASS
RESULT: 5/5 PASS
```

---

## 11. Controller Integration

The debug menu needs one addition to the state machine in
`function-description.md`.

### New State: `ST_DEBUG`

```c
ST_DEBUG        // manual control active, limits NOT enforced
```

| From | Trigger | To |
|------|---------|-----|
| `ST_IDLE` | `REQ_DEBUG_ENTER` while armed | `ST_DEBUG` |
| `ST_DEBUG` | `REQ_DEBUG_DRIVE` | `ST_DEBUG`, motor driven, deadline armed |
| `ST_DEBUG` | deadline expired | `ST_DEBUG`, brake |
| `ST_DEBUG` | `REQ_STOP` or disarm | `ST_IDLE`, brake |

`ST_DEBUG` is the *only* state in which invariants 3 and 4 — never reverse at
position 1, never forward at position 5 — do not hold. That is the entire
reason for the arming interlock, and the reason `ST_DEBUG` can only be entered
by a human typing `UNCOUPLED` at a prompt.

### New Request Kinds

```c
REQ_DEBUG_ENTER,
REQ_DEBUG_DRIVE,    // arg: direction, duty, duration_ms
REQ_DEBUG_EXIT
```

`controller_request()` rejects all three unless `dbg_motor_armed()` returns
true.

### Instrumentation Hooks in `controller_tick()`

Three additions, all `#ifdef LUFTFUGL_DEBUG`:

1. Timing brackets at entry and exit
2. `hist_push()` on every confirmed position change and every `PASS`
3. Counter increments at each terminal transition

None allocates, none formats, none touches the UART. Total cost per tick: two
timer reads and, occasionally, one ring-buffer write.

---

## 12. Build Integration

```cmake
option(LUFTFUGL_DEBUG "Build interactive debug monitor" ON)
if(LUFTFUGL_DEBUG)
    target_compile_definitions(luftfugl PRIVATE LUFTFUGL_DEBUG=1)
    target_sources(luftfugl PRIVATE src/debug.c)
endif()
```

Keep it `ON` for the whole bring-up. Turn it `OFF` only once the constants in
`config.h` are measured and stable — at which point the arming interlock, the
`ST_DEBUG` state and every path that can move the motor without limit
enforcement disappear from the binary entirely.

That is the real reason for the compile guard. Not code size — the fact that a
shipped build should contain no route to unguarded motion at all.

---

## 13. Interface Resolutions

These five points were under-specified. The resolutions below are binding and
take precedence over anything implied earlier in this document.

### 13.1 Leaving `ST_FAULT` — `dbg_fault_clear()`

`agent.md` §6.9 governs production behaviour and is not weakened: **after a
fault, the mechanism cannot be moved without homing.**

`dbg_fault_clear()` exists only under `LUFTFUGL_DEBUG` and does not return the
system to a normal, movable state. It performs:

```c
state    = ST_IDLE;
position = POS_UNKNOWN;      // this is the point
last_direction = DIR_REV;
```

Because `position` is `POS_UNKNOWN`, every subsequent `move` returns
`ERR: position unknown` until a `home` completes. The operator has cleared the
fault *flag*, not the requirement to re-establish position. §6.9's guarantee
holds intact.

The prompt requires typing `CLEAR`, for the same reason the other interlocks
require typed words:

```
Clearing a fault does not restore position.
A home sequence will still be required before any move.
Type CLEAR to confirm:
```

### 13.2 The debug mailbox

`controller_request(request_kind_t, position_t)` is the **production** API and
does not change. A one-byte argument cannot carry direction, duty and duration,
so debug operations use a separate single-slot mailbox.

Remove `REQ_DEBUG_ENTER`, `REQ_DEBUG_DRIVE` and `REQ_DEBUG_EXIT` from
`request_kind_t` — they are superseded by:

```c
#ifdef LUFTFUGL_DEBUG
typedef enum {
    DBG_OP_NONE = 0,
    DBG_OP_ENTER,
    DBG_OP_EXIT,
    DBG_OP_DRIVE,        // dir, duty, ms
    DBG_OP_BRAKE,
    DBG_OP_COAST,
    DBG_OP_STANDBY,      // flag: true = STBY high
    DBG_OP_FAULT_CLEAR
} dbg_op_t;

typedef struct {
    dbg_op_t    op;
    direction_t dir;
    uint8_t     duty;
    uint16_t    ms;
    bool        flag;
} dbg_request_t;

bool controller_debug_request(const dbg_request_t *req);
#endif
```

`controller_debug_request()` is called from the main loop, validates against a
state snapshot, and posts to the single-slot mailbox. It returns `false` — and
posts nothing — when a debug request is already pending, or when
`dbg_motor_armed()` is false for any op other than `DBG_OP_EXIT` and
`DBG_OP_FAULT_CLEAR`.

`controller_tick()` consumes the debug mailbox in the same step where it
consumes the production mailbox, and executes the op inside `ST_DEBUG`. This
resolves §13.3 as well: `dbg_motor_brake()`, `dbg_motor_coast()` and
`dbg_motor_standby()` post `DBG_OP_BRAKE`, `DBG_OP_COAST` and `DBG_OP_STANDBY`
respectively. **No debug function calls `motor_*` directly** — invariant 7 in
`function-description.md` §9 holds without exception.

`DBG_OP_DRIVE` arms the controller's own deadline with `ms`, so the duration
cap survives a stalled main loop.

### 13.3 Runtime configuration overrides

Overrides are a debug-only facility and must cost nothing in a production
build.

`config.h` keeps the compiled constants exactly as they are. Add:

```c
#ifdef LUFTFUGL_DEBUG
typedef struct {
    uint8_t  duty_normal, duty_approach, duty_creep, duty_min;
    uint16_t band_p1_max, band_p2_max, band_p3_max, band_p4_max, band_p5_max;
    uint16_t debounce_ms, brake_hold_ms;
    uint32_t timeout_step_ms, timeout_home_ms, timeout_recover_ms;
} cfg_t;

extern volatile cfg_t cfg;
void cfg_reset(void);          // load compiled defaults
#endif
```

Every runtime read in `encoder.c` and `controller.c` goes through an accessor
macro:

```c
#ifdef LUFTFUGL_DEBUG
#  define CFG_DUTY_NORMAL   (cfg.duty_normal)
#  define CFG_BAND_P1_MAX   (cfg.band_p1_max)
   /* ...one per overridable value... */
#else
#  define CFG_DUTY_NORMAL   DUTY_NORMAL
#  define CFG_BAND_P1_MAX   BAND_P1_MAX
#endif
```

With `LUFTFUGL_DEBUG` off, each macro collapses to the original constant and
the struct does not exist. Ownership: `cfg` lives in `config.c`, is written
only by `dbg_cfg_set()` and `cfg_reset()` from the main loop, and is read by
the tick. It is `volatile` for that reason. Single-word writes are atomic on
Cortex-M0+, so no critical section is needed.

`cfg_reset()` is called from `dbg_init()`. Pin numbers, `PWM_WRAP`,
`PWM_CLKDIV`, `FILTER_DEPTH` and `TICK_HZ` are not in the struct and are not
overridable, as §8 already states.

### 13.4 Exactness of debug output

The prohibition on inventing strings applies to the **production protocol**
in `agent.md` §7–§8. Those responses are machine-parsed and every character
matters.

Debug monitor output is human-facing. The rule is:

- Where this document shows an exact output format — the telemetry `T` line,
  `CAPTURE`, `FINDMIN`, `CAL STEP`, `CAL OVERSHOOT`, `CAL POSITIONS`,
  `HISTORY`, `SELFTEST MOTION`, the arming prompts — reproduce it exactly.
- Where it does not, choose clear wording and **list every such choice in the
  final report**.
- Never emit a string that could be mistaken for a production response. Debug
  output must not begin with `POS:`, `OK:`, `ERR:`, `PASS:` or `ARR:` unless
  this document shows it doing so.

That last rule matters because the two share one UART. A host script watching
for `ARR:` should never be tripped by a debug menu.

---

## 14. Revision 2 — Menu UX and Dry-Run Testing

This section supersedes the menu tree in §3 and adds two menus. Everything else
in §1–§13 stands.

Two goals: make the monitor legible without a copy of this document open, and
make as much of the firmware testable as possible with **only the RP2040 board**
— no driver, no motor, no encoder ladder.

### 14.1 Interaction Rules

These apply to every menu and are not optional.

**Echo typed characters.** The production console deliberately does not echo.
Debug mode does, because an operator typing blind into a terminal cannot tell a
frozen board from a silent one. `dbg_enter()` turns echo on, `dbg_exit()` turns
it off. Backspace erases.

**Every menu line shows current state, not just a label.** A menu that reads
`d  set pulse duty` is worse than one that reads `d  pulse duty ......... 60`.
The operator should never have to open another menu to find out what a value
currently is.

**Every prompt states its range and its unit.**
`duty (0-255, current 60): ` — not `duty: `.

**Every rejection says why.** Not `invalid`, but
`rejected: 30 is below DUTY_MIN (45), motor would stall`.

**The header re-renders on every screen** and carries live state:

```
=== luftfugl debug 1.0 ============================
 state IDLE   pos 3   adc 1311 (band 3, margin 22%)
 armed NO     coupled NO     sim OFF     faults 0
===================================================
```

**`?` shows contextual help** for the current menu — what each item does and
what it requires — not a generic key list.

**`w` answers "what can I run right now?"** on any menu. It lists the tests
currently available given the interlock state and what is connected, and for
each unavailable one, names the single thing blocking it:

```
AVAILABLE NOW
  2/a  adc reading            ready
  7/s  static self-test       ready
  8/*  bench tests            ready
  9/*  simulation             ready
BLOCKED
  3/*  manual drive           needs UNCOUPLED (menu 3, key A)
  4/s  step time              needs COUPLED and a known position
  7/m  motion self-test       needs COUPLED and a known position
```

This is the most useful single addition in this revision. It replaces reading
the specification to find out why a key did nothing.

### 14.2 Revised Menu Tree

```
root
├── 1  status & telemetry
├── 2  encoder & adc
├── 3  motor manual              [UNCOUPLED]
├── 4  calibration               [COUPLED for motion tests]
├── 5  configuration
├── 6  faults & history
├── 7  self-test
├── 8  bench tests               ← new, no driver or motor needed
│    ├── p  pin state readout
│    ├── g  gpio walk            [UNCOUPLED]
│    ├── f  pwm configuration report
│    ├── t  tick & watchdog health
│    ├── r  reset reason
│    ├── o  protocol string listing
│    └── e  echo toggle
└── 9  simulation                ← new, no hardware at all
     ├── e  enable / disable simulation
     ├── v  set adc value
     ├── b  jump to position band
     ├── t  travel sequence
     ├── p  park between reeds
     ├── l  drift past a limit
     └── s  sweep band boundaries
```

---

### 14.3 Menu 8 — Bench Tests

Everything here works with a bare board. Only `g` touches the control pins.

```c
void dbg_bench_pins(void);
void dbg_bench_gpio_walk(void);
void dbg_bench_pwm_report(void);
void dbg_bench_tick_health(void);
void dbg_bench_reset_reason(void);
void dbg_bench_protocol_list(void);
void dbg_bench_echo_toggle(void);
```

#### `p` — Pin state readout

Live levels on every pin this subsystem owns, refreshed at 5 Hz until any key.
Lets you verify wiring with a multimeter without moving anything.

```
PIN STATE (any key to stop)
  GP2  AIN1   0
  GP3  AIN2   0
  GP14 PWMA   pwm, level 0 / 255
  GP15 STBY   0   <- driver in standby
  GP26 SENSE  adc 1311
```

#### `g` — GPIO walk

Drives AIN1, AIN2 and STBY high for 1 s each in turn, then returns all low.
Confirms continuity from MCU pin to driver input with a meter or LED.

Requires `UNCOUPLED`, because if a driver *is* attached this toggles its
direction inputs. PWMA is held at 0 throughout so no drive can occur even then.

```
GPIO WALK  (UNCOUPLED confirmed, PWMA held 0)
  AIN1 high ... low
  AIN2 high ... low
  STBY high ... low
  done
```

#### `f` — PWM configuration report

Reads back the live PWM registers and reports the frequency the hardware is
actually producing, computed from `clk_sys`, the divider and the wrap. This is
the check that would otherwise need a scope.

```
PWM CONFIG
  slice        7, channel A (GP14)
  clk_sys      125000000 Hz
  clkdiv       97.6875  (raw 0x61B, 8.4 fixed point)
  wrap         255
  frequency    4998.4 Hz
  spec         5000 Hz, tolerance 1%   PASS
```

If the divider was rounded wrongly, or `clk_sys` is not what the build assumed,
this line is where it shows up.

#### `t` — Tick and watchdog health

```
TICK HEALTH
  measured rate   1000.0 Hz over 2.00 s     PASS (1000 +/- 20)
  duration        min 11 us  max 34 us  mean 14 us
  overruns        0
  watchdog        enabled, 100 ms, pause_on_debug true
  time to expiry  87 ms at last kick
```

#### `r` — Reset reason

Reports whether the last boot followed a watchdog reset, a power-on, or a debug
reset, plus uptime. Answers "did it silently reset while I was away", which is
otherwise invisible on a board with no LED.

#### `o` — Protocol string listing

Prints the production command and response strings as a **reference table**, so
they can be checked against `agent.md` §7–§8 without a rebuild. It lists them;
it does not emit them as responses, and every line is indented under a header so
nothing can be mistaken for live protocol output.

```
PROTOCOL STRINGS (listing only, not emitted)
  commands   pos | move N | stop | status | home
  ok         OK: moving to N
             OK: already at N
             OK: stopped
             OK: homing
  errors     ERR: invalid target
             ERR: at end-stop
             ...
```

#### `e` — Echo toggle

Turns character echo off for operators on a terminal that echoes locally.

---

### 14.4 Menu 9 — Simulation

**This is the menu that makes the safety logic testable with nothing
connected.**

Simulation replaces the ADC sample at the source. `encoder_tick()` still runs
the real rolling average, the real classifier and the real debounce; the
controller still runs its real state machine, timeouts and limit checks. Only
the twelve bits coming out of the ADC are substituted.

That means a simulated run exercises the actual shipping code paths —
`recover_direction()`, the arrival split, timeout escalation, `PASS`/`ARR`
ordering — rather than a parallel test harness that could diverge from them.

```c
#ifdef LUFTFUGL_DEBUG
void     encoder_sim_enable(bool on);
bool     encoder_sim_active(void);
void     encoder_sim_set(uint16_t adc);
uint16_t encoder_sim_value(void);
#endif
```

`encoder_tick()` gains one branch at the top: when `encoder_sim_active()`, the
sample is `encoder_sim_value()` instead of `adc_read()`. Nothing else changes.

#### Simulation forces the motor off

Entering simulation is only permitted with the driver hardware inhibited:

```c
void motor_set_inhibit(bool on);   // true: motor_enable() no-ops, STBY forced low
bool motor_inhibited(void);
```

`DBG_OP_SIM_ENABLE` calls `motor_set_inhibit(true)` before enabling simulation,
and `motor_set_inhibit(false)` on exit. While inhibited, `motor_enable()` is a
no-op and STBY stays low, so the controller can request drive all it likes and
the hardware cannot respond.

This is a new motor API, added deliberately. It is the mechanism that makes
simulation safe even with a driver and motor fully wired: the controller runs
its real logic against simulated position while the H-bridge is held in
standby. Simulation and real motion are mutually exclusive by construction, not
by convention. `dbg_exit()` always clears both.

The header shows `sim ON` in reverse video for as long as it is active. There
is no way to be in simulation and not know it.

#### `e` — Enable / disable

```
SIM ENABLE
  motor inhibited (STBY forced low)
  adc source: simulated, starting at 372 (position 1)
  sim ON
```

#### `v` — Set ADC value

`v` then a raw value 0–4095. Prints the resulting classification and margin.

#### `b` — Jump to a position band

`b` then 1–5, or `0` for the open band. Sets the injected value to that band's
nominal from the table, so you do not have to remember that position 4 is 2047.

#### `t` — Travel sequence

`t`, then from, to, and milliseconds per band. Ramps the injected value through
the intervening bands at that rate and prints what the controller emits.

This is a full closed-loop test of a move with no motor:

```
SIM TRAVEL 1 -> 5, 300 ms/band
  > move 5
  OK: moving to 5
  PASS:2   at 312 ms
  PASS:3   at 604 ms
  PASS:4   at 901 ms
  ARR:5    at 1214 ms
  final state IDLE, pos 5
  RESULT: PASS  (4 events, expected 4, order correct)
```

Set the rate below the debounce window and arrivals should start being missed —
which is exactly the failure mode `DUTY_APPROACH` exists to prevent, now
demonstrable on the bench.

#### `p` — Park between reeds

Injects the open-band value and reports what the controller does. Expect
`RECOVER`, then a fault after `TIMEOUT_RECOVER_MS` since no motion follows.

#### `l` — Drift past a limit

**The single most valuable test in the debug monitor.**

`l` then `1` or `5`. Sets the confirmed position to that limit, then injects the
open band — simulating the mechanism drifting just past reed 1 or reed 5 — and
reports which direction `recover_direction()` chose.

```
SIM DRIFT PAST LIMIT 5
  established pos 5 (adc 2815)
  injected open band (adc 4095)
  state RECOVER
  recovery direction: REV
  EXPECTED: REV (inward, away from the upper limit)
  RESULT: PASS
```

Run it for both limits. A `FAIL` here means the firmware would drive the
mechanism further past the limit and tear the harness — and you have found it
on a bare board rather than with a motor attached.

#### `s` — Sweep band boundaries

Injects every value from 0 to 4095 and reports each classification transition,
then compares against the configured table.

```
SIM SWEEP
  0..555      -> 1
  556..1023   -> 2
  1024..1678  -> 3
  1679..2431  -> 4
  2432..3455  -> 5
  3456..4095  -> ?
  RESULT: PASS, 6 regions, matches config, no gaps or overlaps
```

Empirical proof that the classifier matches the table — the arithmetic error in
the original band specification would have failed this immediately.

---

### 14.5 Dry-Run Test Sequence

With only the board and a USB serial connection, this covers most of the
firmware. Run it before any hardware is attached.

| Step | Menu | Verifies |
|------|------|----------|
| 1 | `8/t` | Tick at 1 kHz, no overruns, watchdog live |
| 2 | `8/f` | PWM at 4998.4 Hz, divider arithmetic correct |
| 3 | `7/s` | All eight static checks pass |
| 4 | `9/s` | Band table empirically correct |
| 5 | `9/t` 1→5 | Move, `PASS` ordering, arrival |
| 6 | `9/t` 5→1 | Reverse move |
| 7 | `9/l` 1 | Recovery goes forward at the lower limit |
| 8 | `9/l` 5 | Recovery goes reverse at the upper limit |
| 9 | `9/p` | Recovery timeout escalates to fault |
| 10 | `6/f`, `6/h` | Fault snapshot and history ring populated |
| 11 | `8/p` | Pin states, STBY low throughout |

Steps 7 and 8 are the ones that matter. Everything else can be re-tested later
with hardware; those two validate the logic that protects the harness, and they
are far easier to run now than with a motor bolted to a mechanism that has no
end-stops.

### 14.6 Constraints

- Simulation exists only under `LUFTFUGL_DEBUG`. In a production build
  `encoder_sim_active()` does not exist and `encoder_tick()` has no branch.
- Simulation always inhibits the motor. There is no override.
- `dbg_exit()`, any abort, and entry to `ST_FAULT` all clear simulation and
  release the inhibit.
- Simulated values feed the same filter and debounce as real samples. No test
  may bypass them — a test that skipped the filter would not be testing the
  firmware.
- Every simulated sequence reports `RESULT: PASS` or `RESULT: FAIL` with the
  expectation stated, so results do not depend on the operator interpreting the
  output correctly.
