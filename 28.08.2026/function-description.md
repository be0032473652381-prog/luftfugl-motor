# Function Description: Aura luftfugl Motor & Position Control

Module and function-level specification. `agent.md` defines *what* the firmware
must do and `hardware.md` defines what is physically built; this document
defines *how the code is organised* — every module, every public function, its
contract, and the execution context it runs in.

Written to be implementable directly. Anything left as a judgement call is
marked explicitly.

---

## 1. Execution Model

Two contexts only. Everything else follows from keeping them separate.

| Context | Runs | Contains |
|---------|------|----------|
| **Timer IRQ**, 1 kHz | `encoder_tick()` then `controller_tick()` | All sampling, all state transitions, all motor writes |
| **Main loop**, free-running | `console_poll()` then `console_drain_events()` | All UART I/O, all text formatting |

The motor is never written from the main loop. The UART is never written from
the IRQ. This is the single most important structural rule in the codebase —
it removes every race and every possibility of a blocking write inside the
control path.

Two lock-free structures cross the boundary:

- **Command mailbox** — main loop writes a pending request, IRQ consumes it.
- **Event ring buffer** — IRQ pushes short event codes, main loop formats and
  prints them.

```
  ┌──────────────── Main loop ────────────────┐
  │  console_poll()                           │
  │    └─ parse line ──► controller_request() ├──► [mailbox] ──┐
  │  console_drain_events()  ◄────────────────┤               │
  └───────────────────────▲───────────────────┘               │
                          │ [event ring]                      │
  ┌───────────────────────┴───────────────────┐               │
  │  1 kHz timer IRQ                          │               │
  │    encoder_tick()   ── sample, filter     │               │
  │    controller_tick() ── consume mailbox ◄─┼───────────────┘
  │                      ── state machine     │
  │                      ── motor_*() writes  │
  └───────────────────────────────────────────┘
```

### 1.1 Tick Budget

The 1 kHz tick must complete in well under 1 ms. Expected cost: one ADC
conversion (~2 µs at default clock divider), a 5-element average, a band
comparison, and a handful of state comparisons. There is no reason for it to
exceed 20 µs. **No printf, no float division, no blocking calls inside the
tick.**

The filter uses integer arithmetic throughout. Sum five `uint16_t` samples into
a `uint32_t` and divide by 5.

---

## 2. Shared Types

Declared in `config.h` (or a small `types.h` included by it).

```c
typedef enum {
    DIR_STOP = 0,
    DIR_FWD,          // toward position 5
    DIR_REV           // toward position 1
} direction_t;

typedef enum {
    ST_BOOT = 0,
    ST_IDLE,
    ST_MOVING,
    ST_APPROACH,
    ST_HOMING,
    ST_RECOVER,
    ST_FAULT
} sys_state_t;

// 0 is reserved for "unknown / between reeds". Valid positions are 1..5.
#define POS_UNKNOWN   0
#define POS_MIN       1
#define POS_MAX       5
typedef uint8_t position_t;

typedef enum {
    REQ_NONE = 0,
    REQ_MOVE,
    REQ_STOP,
    REQ_HOME
} request_kind_t;

typedef enum {
    MOVE_OK = 0,        // accepted
    MOVE_ALREADY,       // already at target
    MOVE_INVALID,       // target outside 1..5
    MOVE_ENDSTOP,       // would drive past a limit
    MOVE_BUSY,          // a move is already active
    MOVE_POS_UNKNOWN,   // between reeds, home first
    MOVE_FAULT          // in ST_FAULT
} move_result_t;

typedef enum {
    EV_PASS = 0,        // arg = position crossed
    EV_ARRIVE,          // arg = position reached
    EV_TIMEOUT,
    EV_FAULT_HOME,
    EV_FAULT_RECOVER,
    EV_HOMING
} event_kind_t;
```

`POS_UNKNOWN = 0` is deliberate: it makes "no valid position" falsy and lets
range checks read as `pos >= POS_MIN && pos <= POS_MAX`.

---

## 3. `config.h`

Constants only — no code, no state. Every magic number in the system lives
here and nowhere else.

| Group | Constants |
|-------|-----------|
| Pins | `PIN_AIN1` 2, `PIN_AIN2` 3, `PIN_PWMA` 14, `PIN_STBY` 15, `PIN_SENSE` 26, `ADC_CHANNEL` 0, `PIN_UART_TX` 0, `PIN_UART_RX` 1 |
| PWM | `PWM_WRAP` 255, `PWM_CLKDIV` 97.6875f |
| Duty | `DUTY_NORMAL` 200, `DUTY_APPROACH` 60, `DUTY_CREEP` 50, `DUTY_MIN` 45 |
| Bands | `BAND_P1_MAX` 555, `BAND_P2_MAX` 1023, `BAND_P3_MAX` 1678, `BAND_P4_MAX` 2431, `BAND_P5_MAX` 3455 |
| Timing | `TICK_HZ` 1000, `FILTER_DEPTH` 5, `DEBOUNCE_MS` 12, `BRAKE_HOLD_MS` 100 |
| Timeouts | `TIMEOUT_STEP_MS` 1500, `TIMEOUT_HOME_MS` 6000, `TIMEOUT_RECOVER_MS` 2000 |
| Console | `CONSOLE_LINE_MAX` 32, `EVENT_QUEUE_DEPTH` 8, `UART_BAUD` 115200 |

Values above `BAND_P5_MAX` are the open band. There is no `BAND_OPEN_MIN`
constant — the classifier falls through to unknown.

---

## 4. `motor` — TB6612FNG Driver Abstraction

The only module that touches GP2, GP3, GP14 or GP15. Stateless apart from a
cached direction and duty for reporting.

### `void motor_init(void)`
Configures GP2, GP3, GP15 as outputs driven LOW, and GP14 as PWM with
`PWM_WRAP` and `PWM_CLKDIV`, level 0. Leaves STBY LOW so the driver stays in
standby. Must be called before any other `motor_*` function.
*Post-condition:* driver in standby, motor unpowered, cached state `DIR_STOP` /
duty 0.

### `void motor_enable(void)`
Sets AIN1 = AIN2 = 0, then raises STBY HIGH. The order matters — raising STBY
with a stale direction latched would produce a motion glitch.

### `void motor_disable(void)`
Drops STBY LOW. **Releases the motor into a free spin.** Only for `ST_FAULT`
and shutdown. Never use as a way to stop — see §4.1.

### `void motor_drive(direction_t dir, uint8_t duty)`
Applies direction and speed per the TB6612FNG truth table.

| `dir` | AIN1 | AIN2 | PWMA |
|-------|------|------|------|
| `DIR_FWD` | 1 | 0 | `duty` |
| `DIR_REV` | 0 | 1 | `duty` |
| `DIR_STOP` | — | — | delegates to `motor_brake()` |

Duty is written **directly**, no inversion. Clamps `duty` to `DUTY_MIN` when
non-zero and below it; a duty that cannot break stiction only heats the motor.
A `duty` of 0 delegates to `motor_brake()`.
*Pre-condition:* `motor_enable()` has been called.
*Side effect:* updates cached direction and duty.

### `void motor_brake(void)`
Sets AIN1 = AIN2 = 1 and PWMA to full. Short brake. This is the normal way to
stop. Cached direction becomes `DIR_STOP`, duty 0.

### `void motor_coast(void)`
Sets AIN1 = AIN2 = 0, PWMA full. Outputs high-impedance. Provided for bench use
only; the state machine never calls it.

### `direction_t motor_direction(void)` / `uint8_t motor_duty(void)`
Return cached values for the `status` command. No hardware access.

### 4.1 Why Stopping Is Always a Brake

The mechanism has no physical end-stops. Coasting or dropping STBY lets gearbox
inertia carry the moving part past reed 1 or reed 5, which twists the attached
harness. Every stop in every state uses `motor_brake()`. `motor_disable()`
appears exactly once outside init, in the transition into `ST_FAULT`, where the
motor has already been braked first.

---

## 5. `encoder` — Sensing, Filtering, Classification

Owns GP26/ADC0. Called once per tick from IRQ context.

### `void encoder_init(void)`
Initialises the ADC, selects `ADC_CHANNEL`, disables the pin's digital
functions. Primes the filter by taking `FILTER_DEPTH` samples so the first
classification is valid rather than a ramp from zero.

### `void encoder_tick(void)`
Called at 1 kHz. Performs one conversion, inserts it into the rolling buffer,
recomputes the average, classifies it, and advances the debounce timer.
*Must be the first call in the tick*, so `controller_tick()` sees fresh data.

Internal sequence:
1. `raw = adc_read()`
2. Insert into circular buffer of depth `FILTER_DEPTH`, update running sum
3. `avg = sum / FILTER_DEPTH`
4. `instant = classify(avg)`
5. If `instant` differs from the previous instant, restart the debounce timer;
   otherwise increment it
6. If the debounce timer reaches `DEBOUNCE_MS` and `instant` differs from the
   stored confirmed value, update confirmed and set the changed flag

### `static position_t classify(uint16_t v)`
Maps an averaged reading to a position using the band constants. Returns
`POS_UNKNOWN` for anything above `BAND_P5_MAX`.

```c
if (v <= BAND_P1_MAX) return 1;
if (v <= BAND_P2_MAX) return 2;
if (v <= BAND_P3_MAX) return 3;
if (v <= BAND_P4_MAX) return 4;
if (v <= BAND_P5_MAX) return 5;
return POS_UNKNOWN;
```

### `uint16_t encoder_raw(void)` / `uint16_t encoder_average(void)`
Most recent raw sample and current filter output. For diagnostics and for the
bench measurements in `hardware.md` §10.

### `position_t encoder_instant(void)`
Band classification of the current average, with **no debounce**. Used for two
things only: `PASS:N` transit detection, and braking on first detection when
targeting a limit. Everywhere else, use the confirmed value.

### `position_t encoder_confirmed(void)`
The classification that has held steady for `DEBOUNCE_MS`. Returns
`POS_UNKNOWN` if the open band is what has held steady. This is what `pos`
reports and what arrival is judged by.

### `bool encoder_take_change(position_t *out)`
Tests and clears the changed flag, writing the new confirmed position to `out`.
Single-consumer — only `controller_tick()` may call it.

### 5.1 Two Detection Rules, and Why

`encoder_instant()` and `encoder_confirmed()` exist because two requirements
pull in opposite directions.

Arrival must be *certain*, so it waits 12 ms. But at `DUTY_NORMAL` a reed may
close for less than 12 ms as the magnet sweeps past, so transit detection
cannot wait — `PASS:N` would simply never fire. Transit therefore uses the
instant value, and arrival uses the confirmed one.

The limit exception (§6.4) is the third case: when the target is 1 or 5, the
brake fires on the instant value and the `ARR:N` message waits for
confirmation. Braking early costs nothing if the reading was noise; braking
late risks the harness.

---

## 6. `controller` — State Machine

Owns all system state. Runs entirely in IRQ context except for
`controller_request()` and the read-only accessors.

### State Variables

```c
static sys_state_t   state;
static position_t    position;         // last confirmed
static position_t    target;
static direction_t   last_direction;   // initialised DIR_REV
static uint32_t      deadline_ms;      // absolute, 0 = disarmed
static uint32_t      brake_until_ms;
```

`position` and `state` are `volatile` — the main loop reads them for `pos` and
`status`.

### `void controller_init(void)`
Sets `state = ST_BOOT`, `position = POS_UNKNOWN`, `last_direction = DIR_REV`,
clears the mailbox. Does not touch hardware; `motor_init()` and
`encoder_init()` are called separately from `main()`.

### `move_result_t controller_request(request_kind_t kind, position_t arg)`
**Called from the main loop.** Validates the request against a snapshot of the
current state and, if acceptable, posts it to the mailbox for the next tick to
consume. Returns immediately — it never moves the motor itself.

Validation order for `REQ_MOVE`, first match wins:

| Check | Result |
|-------|--------|
| `state == ST_FAULT` | `MOVE_FAULT` |
| `arg < POS_MIN \|\| arg > POS_MAX` | `MOVE_INVALID` |
| `position == POS_UNKNOWN` | `MOVE_POS_UNKNOWN` |
| `state` is `ST_MOVING`, `ST_APPROACH`, `ST_HOMING` or `ST_RECOVER` | `MOVE_BUSY` |
| `arg == position` | `MOVE_ALREADY` |
| `position == POS_MIN && arg < position` | `MOVE_ENDSTOP` |
| `position == POS_MAX && arg > position` | `MOVE_ENDSTOP` |
| otherwise | `MOVE_OK`, request posted |

The two end-stop rows are unreachable given the range check above them — an
out-of-range target is rejected as `MOVE_INVALID` first. They are retained
because `agent.md` §8 specifies `ERR: at end-stop` for `move 0` at position 1,
and because they are the correct guard if the position range is ever widened.
**Implementation note:** to match the specified protocol exactly, check the
end-stop condition against the *raw parsed integer* before the range check.

`REQ_STOP` is always accepted. `REQ_HOME` is accepted in any state including
`ST_FAULT` — it is the documented recovery path.

### `void controller_tick(void)`
Called at 1 kHz immediately after `encoder_tick()`. Structure:

1. **Consume the mailbox.** If a request is pending, apply the state transition
   and clear it.
2. **Refresh position.** If `encoder_take_change()` reports a new confirmed
   position, update `position`; if it is a valid position, remember it.
3. **Check the deadline.** If `deadline_ms` is armed and now past, run the
   timeout path for the current state.
4. **Run the state handler.** One `switch` on `state`.

Handlers:

| State | Behaviour |
|-------|-----------|
| `ST_BOOT` | Wait for the filter to prime, then classify. Valid → brake, `ST_IDLE`, emit `EV_ARRIVE`. Unknown → begin homing. |
| `ST_IDLE` | Motor braked. Nothing to do. |
| `ST_MOVING` | Emit `EV_PASS` on each new instant position. When one step from `target`, switch to `ST_APPROACH`. |
| `ST_APPROACH` | Drive at approach speed (see §6.4). Watch for arrival. On arrival: brake, arm `brake_until_ms`, `ST_IDLE`, emit `EV_ARRIVE`. |
| `ST_HOMING` | Creep reverse. On confirmed position 1: brake, `ST_IDLE`, emit `EV_ARRIVE`. |
| `ST_RECOVER` | Creep in the direction chosen by §6.5. On any confirmed valid position: brake, `ST_IDLE`, emit `EV_ARRIVE`. |
| `ST_FAULT` | Motor disabled. Only `REQ_HOME` or reset escapes. |

Any state other than `ST_IDLE` and `ST_FAULT` that observes
`position == POS_UNKNOWN` unexpectedly enters `ST_RECOVER`.

### `void controller_begin_move(position_t tgt)` *(internal)*
Sets `target`, computes direction (`tgt > position` → `DIR_FWD`, else
`DIR_REV`), stores it in `last_direction`, arms
`deadline_ms = now + steps * TIMEOUT_STEP_MS` where `steps = abs(tgt - position)`,
and enters `ST_MOVING` — or `ST_APPROACH` directly if `steps == 1`.

### `void controller_begin_home(void)` *(internal)*
Brakes, then `motor_enable()` if needed, sets `target = POS_MIN`,
`last_direction = DIR_REV`, arms `deadline_ms = now + TIMEOUT_HOME_MS`, drives
reverse at `DUTY_CREEP`, enters `ST_HOMING`, emits `EV_HOMING`.

### `sys_state_t controller_state(void)` / `position_t controller_position(void)` / `position_t controller_target(void)`
Read-only accessors for the console. Safe from the main loop; each reads a
single volatile word.

### 6.1 State Transition Table

| From | Trigger | To | Action |
|------|---------|----|--------|
| `ST_BOOT` | valid position | `ST_IDLE` | brake, `EV_ARRIVE` |
| `ST_BOOT` | unknown position | `ST_HOMING` | creep reverse |
| `ST_IDLE` | `REQ_MOVE` accepted | `ST_MOVING` / `ST_APPROACH` | drive |
| `ST_IDLE` | `REQ_HOME` | `ST_HOMING` | creep reverse |
| `ST_MOVING` | one step from target | `ST_APPROACH` | reduce speed |
| `ST_MOVING` / `ST_APPROACH` | `REQ_STOP` | `ST_IDLE` | brake |
| `ST_APPROACH` | target detected | `ST_IDLE` | brake, `EV_ARRIVE` |
| any moving state | position becomes unknown | `ST_RECOVER` | creep per §6.5 |
| `ST_RECOVER` | valid position confirmed | `ST_IDLE` | brake, `EV_ARRIVE` |
| `ST_MOVING` / `ST_APPROACH` | deadline expired | `ST_HOMING` | brake, `EV_TIMEOUT`, then home |
| `ST_HOMING` | position 1 confirmed | `ST_IDLE` | brake, `EV_ARRIVE` |
| `ST_HOMING` | deadline expired | `ST_FAULT` | brake, disable, `EV_FAULT_HOME` |
| `ST_RECOVER` | deadline expired | `ST_FAULT` | brake, disable, `EV_FAULT_RECOVER` |
| `ST_FAULT` | `REQ_HOME` | `ST_HOMING` | enable, creep reverse |

### 6.2 Timeout Policy

A move gets `steps × TIMEOUT_STEP_MS`. On expiry the controller brakes, emits
`EV_TIMEOUT`, and **automatically begins homing** rather than faulting
immediately — a missed reed is more likely than a jam, and homing recovers a
known position.

If homing *also* times out, the mechanism is genuinely stuck or the encoder is
dead, and the controller faults hard: brake, `motor_disable()`, `ST_FAULT`.
Two stages, never a loop.

### 6.3 Speed Selection

```c
static uint8_t speed_for(position_t tgt, uint8_t steps_remaining) {
    if (steps_remaining > 1)            return DUTY_NORMAL;
    if (tgt == POS_MIN || tgt == POS_MAX) return DUTY_CREEP;
    return DUTY_APPROACH;
}
```

The limits get creep speed for the entire final step, not just the last few
millimetres.

### 6.4 Arrival Detection

```c
bool arrived(position_t tgt) {
    if (tgt == POS_MIN || tgt == POS_MAX)
        return encoder_instant() == tgt;   // brake immediately
    return encoder_confirmed() == tgt;     // confirm first
}
```

When the limit branch fires, brake at once but withhold `EV_ARRIVE` until
`encoder_confirmed()` agrees. If confirmation never comes, the deadline catches
it.

### 6.5 Recovery Direction

```c
static direction_t recover_direction(position_t last_valid) {
    if (last_valid == POS_MIN) return DIR_FWD;   // never further out
    if (last_valid == POS_MAX) return DIR_REV;   // never further out
    return last_direction;                        // safe in the middle
}
```

This overrides direction history at the limits. Creeping in `last_direction`
after leaving reed 5 forward would drive further past the upper limit and tear
the harness. **This is the function most likely to be implemented wrongly.**
Test it explicitly — `hardware.md` §10 and `agent.md` §13 stage 6.

---

## 7. `console` — UART Command Interface

Runs entirely in the main loop. Never called from IRQ context.

### `void console_init(void)`
Initialises UART0 at `UART_BAUD`, 8N1, no flow control, on GP0/GP1. Clears the
line buffer and event queue. Prints the banner.

### `void console_poll(void)`
Non-blocking. Drains available characters from the UART FIFO into the line
buffer. On `\n` or `\r\n`, terminates the buffer and calls
`console_handle_line()`. On overflow past `CONSOLE_LINE_MAX`, emits
`ERR: line too long` and discards to the next newline. Empty lines produce no
output.

### `static void console_handle_line(char *line)`
Lowercases the verb, splits on the first space, dispatches:

| Verb | Handler |
|------|---------|
| `pos` | Format `POS:N` or `POS:?` from `controller_position()` |
| `move` | Parse argument, `controller_request(REQ_MOVE, n)`, map result to text |
| `stop` | `controller_request(REQ_STOP, 0)`, print `OK: stopped` |
| `status` | Assemble the full state dump |
| `home` | `controller_request(REQ_HOME, 0)`, print `OK: homing` |
| anything else | `ERR: unknown command` |

Argument parsing uses `strtol` with end-pointer checking so `move 3x` is
rejected as `ERR: invalid target` rather than silently accepted as 3. A missing
argument is also `ERR: invalid target`.

### `static const char *result_text(move_result_t r)`

| Result | Text |
|--------|------|
| `MOVE_OK` | `OK: moving to N` |
| `MOVE_ALREADY` | `OK: already at N` |
| `MOVE_INVALID` | `ERR: invalid target` |
| `MOVE_ENDSTOP` | `ERR: at end-stop` |
| `MOVE_BUSY` | `ERR: busy` |
| `MOVE_POS_UNKNOWN` | `ERR: position unknown` |
| `MOVE_FAULT` | `ERR: fault` |

### `void console_push_event(event_kind_t kind, uint8_t arg)`
**The only console function callable from IRQ context.** Writes one two-byte
entry into the ring buffer and returns. It does not format and does not touch
the UART. If the queue is full the event is dropped — dropping a `PASS:N`
report is always preferable to stalling the control loop.

### `void console_drain_events(void)`
Called from the main loop. Pops queued events and prints them:

| Event | Output |
|-------|--------|
| `EV_PASS` | `PASS:N` |
| `EV_ARRIVE` | `ARR:N` |
| `EV_TIMEOUT` | `ERR: timeout` |
| `EV_FAULT_HOME` | `ERR: fault home timeout` |
| `EV_FAULT_RECOVER` | `ERR: fault recover timeout` |
| `EV_HOMING` | `OK: homing` |

### `static void console_status(void)`
Emits `POS:N DIR:FWD|REV|STP SPD:0-255 STATE:<name>` using
`controller_position()`, `motor_direction()`, `motor_duty()` and
`controller_state()`. `POS:?` when the position is unknown.

### 7.1 Why Events Are Queued

`controller_tick()` runs in an interrupt at 1 kHz. Calling `printf` from there
would block on the UART FIFO for hundreds of microseconds and could overrun the
next tick — meaning a missed ADC sample precisely when the motor is moving and
sampling matters most. The ring buffer costs 16 bytes and removes the problem
entirely.

---

## 8. `main.c`

### `int main(void)`

```c
// No stdio_init_all() - console is a raw UART0 driver. See agent.md §15.3.
if (watchdog_caused_reboot()) { /* flag for banner */ }

console_init();            // uart_init(), banner
motor_init();              // STBY low, PWM idle
encoder_init();            // ADC, prime filter
controller_init();

motor_enable();            // STBY high, direction cleared first

watchdog_enable(100, true);   // agent.md §15.8
add_repeating_timer_us(-1000, on_tick, NULL, &timer);

for (;;) {
    console_poll();
    console_drain_events();
    tight_loop_contents();
}
```

### `static bool on_tick(repeating_timer_t *t)`

```c
encoder_tick();
controller_tick();     // kicks the watchdog internally
return true;
```

Order is fixed and not negotiable: the controller must act on the sample taken
this tick, not the previous one.

The negative delay in `add_repeating_timer_us` requests a fixed 1000 µs period
measured from the start of each callback rather than its end, which keeps the
sampling interval uniform.

---

## 9. Invariants

These must hold at every tick boundary. They are worth asserting in a debug
build.

1. `state == ST_IDLE` implies the motor is braked and `duty == 0`.
2. `state == ST_FAULT` implies STBY is LOW.
3. The motor is never driven `DIR_REV` while `position == POS_MIN`.
4. The motor is never driven `DIR_FWD` while `position == POS_MAX`.
5. `position` is either `POS_UNKNOWN` or within `POS_MIN..POS_MAX`.
6. `deadline_ms != 0` whenever the motor is energised.
7. No `motor_*` call originates outside `controller_tick()` except
   `motor_init()` and the single `motor_enable()` in `main()`.
8. No UART write originates from IRQ context.

Invariants 3 and 4 are the harness-protection guarantees. If a code change
makes either hard to verify by inspection, that change is wrong.

---

## 10. Test Hooks

Optional, behind `#ifdef LUFTFUGL_DEBUG`, useful during bring-up and harmless
to omit from the shipped build.

| Command | Purpose |
|---------|---------|
| `adc` | Print raw and averaged readings — fills the `hardware.md` §5 measured column |
| `raw N` | Drive at duty N for 200 ms then brake, bypassing the state machine. **Motor uncoupled only.** |
| `band` | Dump the active band table, to confirm `config.h` matches the build |
| `state` | Print state, target, deadline remaining, last direction |

`raw` bypasses limit enforcement by design, which is exactly why it must never
be used with the mechanism coupled.
