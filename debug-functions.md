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
