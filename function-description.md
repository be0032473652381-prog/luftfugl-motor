# Function Description: Aura luftfugl Motor & Position Control

Module and function-level specification. `agent.md` defines *what* the
firmware must do and `hardware.md` defines what is physically built;
this document defines *how the code is organised* — every module, every
public function, its contract, and the execution context it runs in.

**A note on confidence, stated plainly rather than hidden**: this is a
full rewrite of a document that had drifted very far from the actual
codebase — its previous version described reed switches, a `RECOVER`
state, and pin assignments that never matched any confirmed hardware
revision. For `main.c`, `config.h`, `motor.c`, `encoder.c`,
`controller.c`, `console.c`, `debug.c`, and `led.c`, this rewrite is
based on direct, function-level source review — the same standard of
confidence as the original document aimed for. For `power_monitor.c`,
`co2.c`, `buzzer.c`, `event_timer.c`, and `debug_help.c`, confidence is
real but shallower — confirmed architecture, confirmed key behaviors,
confirmed persistence pattern, but not an exhaustive per-function
audit. Each of those sections says so explicitly rather than presenting
uniform confidence that doesn't exist yet.

---

## 1. Execution Model

Two contexts, plus a main loop that is not a busy-wait.

| Context | Runs | Contains |
|---------|------|----------|
| **Timer IRQ**, 1 kHz | `encoder_tick()`, `controller_tick()`, `power_monitor_tick()` | All sampling, all state transitions, all motor writes |
| **Main loop**, interrupt-idle | `console_poll()`, `console_drain_events()`, `led_update()`, `co2_tick()`, `buzzer_tick()`, `event_timer_poll()`, ambient-light polling, `battery_alert_poll()` | All UART I/O, all text formatting, all peripheral polling that doesn't need 1 ms resolution |

**Confirmed**: the main loop calls `__wfi()` between iterations, not a
busy-wait — the CPU halts until the next interrupt, with the 1 kHz
safety tick remaining active throughout, so this is not a deep-sleep
wait. `AGENTS.md` reserves further sleep/power-down work for the
future, without a mechanism yet selected.

The motor is never written from the main loop. The UART is never
written from IRQ context. This remains the single most important
structural rule in the codebase.

```
  ┌──────────────── Main loop (__wfi() idle) ─────────────┐
  │  console_poll() / console_drain_events()              │
  │  led_update()  co2_tick()  buzzer_tick()               │
  │  event_timer_poll()  ambient-light poll  battery_alert_poll() │
  └───────────────────────▲────────────────────────────────┘
                          │ event ring / cached sensor state
  ┌───────────────────────┴────────────────────────────────┐
  │  1 kHz timer IRQ                                        │
  │    encoder_tick()   controller_tick()   power_monitor_tick() │
  └──────────────────────────────────────────────────────────┘
```

### 1.1 Tick Budget

The 1 kHz tick must complete well under 1 ms. `controller_tick()`'s
first substantive line calls `watchdog_update()` — the 100 ms watchdog
is serviced from this independent timer context, not the main loop,
which is why a slow main-loop operation doesn't by itself risk a
watchdog reset (confirmed reasoning, see §6 below).

### 1.2 Known, Acknowledged Exception to "No Blocking Calls"

`co2.c`'s single-shot CO₂ measurement command uses `sleep_ms(5000)`.
This is existing technical debt, confirmed present, not fixed as of
this writing — not a precedent for adding blocking calls elsewhere.

---

## 2. Shared Types

Declared in `config.h`.

```c
typedef enum {
    DIR_STOP = 0,
    DIR_FWD,
    DIR_REV
} direction_t;

typedef enum {
    ST_BOOT = 0,
    ST_IDLE,
    ST_MOVING,
    ST_APPROACH,
    ST_HOMING,
    ST_FAULT,
    ST_DEBUG          // gated behind #ifdef LUFTFUGL_DEBUG
} sys_state_t;

// Confirmed: no ST_RECOVER exists. A move timeout falls back to
// begin_home() -- always re-seeking position 1 -- not a direction-aware
// recovery state. This is a genuine, still-open NEEDS DECISION item in
// agent.md, not an oversight in this document.

#define POS_BETWEEN   0    // unconfirmed / between stations
#define POS_MIN       1
#define POS_MAX       5    // the five CO2-severity stations
// Station 6 (EVENT_POSITION) confirmed implemented (cfg.pos_6_adc
// exists) -- structurally different from 1-5, reserved for
// calibration/warning/error/wait states. Its interaction with the
// POS_MIN/POS_MAX limit checks below is not confirmed.
typedef uint8_t position_t;

typedef enum {
    REQ_NONE = 0,
    REQ_MOVE,
    REQ_STOP,
    REQ_HOME
} request_kind_t;

typedef enum {
    MOVE_OK = 0,
    MOVE_ALREADY,
    MOVE_BUSY,
    MOVE_POS_UNKNOWN,
    MOVE_FAULT
} move_result_t;

// Jog uses a separate result type -- confirmed distinct from move_result_t,
// since jog validates against +/-ADC_MAX_VALUE and the configured
// endstops, not the 1..5 station range.
typedef enum {
    JOG_OK = 0,
    JOG_ENDSTOP
} jog_result_t;
```

---

## 3. `config.h`

Constants only. Every tunable value lives here — this remains an
explicit house rule, confirmed applied consistently across every
feature added tonight (battery thresholds, buzzer alert timing,
brightness zones).

| Group | Confirmed values |
|-------|-------|
| Motor pins | `PIN_AIN1` 2, `PIN_AIN2` 3, `PIN_PWMA` 14, `PIN_STBY` 15 |
| Sense | `PIN_SENSE` (ADC0) 26 |
| I²C0 | `PIN_I2C_SDA` 4, `PIN_I2C_SCL` 5 |
| Buzzer | `PIN_BIN1` 6, `PIN_BIN2` 7, `PIN_PWMB` 16 |
| Console UART | `PIN_UART_TX` 8, `PIN_UART_RX` 9 — **confirmed by direct ohmmeter measurement on physical hardware**; two earlier, different pin claims (GP0/GP1, then GP20/GP21) were both wrong |
| Other pins | LED power-enable `GP0`, SDC41 enable `GP1`, CO2-LIMIT_AB `GP10`, DS3231 wake `GP17`, LED data `GP18`, motor-pot power switch `GP22`, onboard status LED `GP25` |
| PWM | `PWM_WRAP` 255, `PWM_CLKDIV` 97.6875f |
| Duty | `DUTY_NORMAL` 50, `DUTY_APPROACH` 25, `DUTY_CREEP` 25, `DUTY_MIN` 25 |
| Position sensing | Station ADC targets + `POS_WINDOW` (window comparison against a continuous potentiometer) — **not** a resistor-ladder band table |
| Timing | `TICK_HZ` 1000, `FILTER_DEPTH` 5, `DEBOUNCE_MS` 12, `BRAKE_HOLD_MS` |
| Timeouts | `TIMEOUT_STEP_MS` 30000, `TIMEOUT_HOME_MS` 6000, `JOG_TIMEOUT_MS` 3000 — no `TIMEOUT_RECOVER_MS` exists |
| Console | `CONSOLE_LINE_MAX` 32, `UART_BAUD` 115200 |
| Battery | `BATTERY_WARN_MV` 3600, `BATTERY_CRITICAL_MV` 3000 (corrected from earlier 4400/4000, reasoned from the buck-boost's real input range and alkaline end-of-life voltage, both now `cfg`-settable) |
| Buzzer alert | `BATTERY_ALERT_FREQUENCY_HZ` 2700, `BATTERY_ALERT_DURATION_MS` 180, `BATTERY_ALERT_PERIOD_MS` 48000 |
| Flash | `PICO_FLASH_SIZE_BYTES` = `4 * 1024 * 1024` — confirmed directly via SFDP during a real flash operation; a wrong earlier board-header value (16 MB) was found and corrected, since several persistence mechanisms compute physical flash addresses from this constant |

---

## 4. `motor.c`

The only module that touches the motor driver's GPIOs. Confirmed
consistent with the original design intent — this module's contract
hasn't meaningfully changed even though the encoder and console around
it have.

### `void motor_init(void)`
Configures AIN1/AIN2/STBY as outputs driven LOW, PWMA as PWM at
`PWM_WRAP`/`PWM_CLKDIV`, level 0. STBY stays LOW — motor cannot drive
until `motor_enable()` is explicitly called.

### `void motor_enable(void)` / `void motor_disable(void)`
Sets STBY HIGH/LOW. `motor_enable()` clears AIN1/AIN2 first, then raises
STBY, avoiding a glitch from a stale latched direction.

### `void motor_drive(direction_t dir, uint8_t duty)`
Sets AIN1/AIN2 per direction, PWMA to `duty`. A `duty` of 0 delegates to
`motor_brake()`.

### `void motor_brake(void)`
AIN1 = AIN2 = HIGH (short brake), PWMA at full. This is the normal way
to stop — confirmed used everywhere stopping happens.

### `void motor_coast(void)`
AIN1 = AIN2 = LOW. Bench/diagnostic use.

### `direction_t motor_direction(void)` / `uint8_t motor_duty(void)`
Cached-value accessors for `status` reporting.

### 4.1 Why Stopping Is Always a Brake

Unchanged principle, still true regardless of encoder type: there are
no physical end-stops. Coasting or dropping STBY lets gearbox inertia
carry the moving part past a limit, twisting the harness. Every stop
uses `motor_brake()`.

---

## 5. `encoder.c`

**Rewritten entirely** — the original version of this document described
a 5-reed-switch resistor-ladder design with band-comparison
classification. That was debunked by direct source review: this is a
single continuous potentiometer, and classification is a window
comparison, not a band lookup.

### `void encoder_init(void)`
Takes 5 raw ADC samples immediately at boot, computes their average,
classifies it, and sets `confirmed_position` directly with the debounce
timer pre-set as already-satisfied — the very first reading is trusted
instantly, since there is nothing yet to compare it against. This is a
deliberate, confirmed exception to the normal debounce rule below.

### `void encoder_tick(void)`
Called every 1 ms from `on_tick()`, before `controller_tick()`. Reads
raw ADC, updates a 5-sample rolling average, classifies against station
targets within `CFG_POS_WINDOW`, and confirms a new position once the
classification has held stable for `CFG_DEBOUNCE_MS`.

### `static position_t position_at(uint16_t adc_value)`
`delta = |adc_value - nominal|`, compared against `CFG_POS_WINDOW` for
each station's nominal ADC target. Returns the matching station, or
`POS_BETWEEN` if none match. **Confirmed window comparison — not the
band-ladder classifier the original version of this document
described.**

### `position_t encoder_confirmed(void)`
Returns the current confirmed position, or `POS_BETWEEN`.

**Not confirmed for the current design**: the original document
described a separate `encoder_instant()` (no debounce, for `PASS:N`
transit detection) alongside `encoder_confirmed()`, justified by a
reed switch's brief closure time. Whether this same two-tier split
still exists for a continuous potentiometer — which doesn't have that
specific timing problem — has not been directly confirmed. Don't assume
either way without checking current source.

---

## 6. `controller.c`

The best-confirmed module in this rewrite, backed by direct review of
`controller.h`'s full function signatures and extensive cross-referencing
against `debug.c`.

### `void controller_init(void)`
Sets initial state. Does not explicitly reset `station1_lock` — as a
file-scope `static volatile bool` with no explicit initializer, it is
zero-initialized by standard C runtime startup on every real reset,
regardless of whether this function touches it.

### `void controller_tick(void)`
Called every 1 ms from `on_tick()`, after `encoder_tick()`. First
substantive line: `watchdog_update()`. Handles `ST_BOOT` classification
on the first tick, move/jog execution, anti-stiction duty boost (see
§6.2), and the timeout-triggered homing fallback (§6.1).

### `move_result_t controller_request(request_kind_t kind, position_t arg)`
Entry point for move/home requests. Returns `MOVE_OK`, `MOVE_BUSY`,
`MOVE_ALREADY`, `MOVE_FAULT`, or `MOVE_POS_UNKNOWN`.

### `jog_result_t controller_request_jog(int16_t delta, uint16_t *from_adc)`
Bounded by `±ADC_MAX_VALUE` (~4095) and the configured endstops — a
substantially wider bound than the original document's five-station
range implied jog would use. Returns `JOG_OK` or `JOG_ENDSTOP`.

### `move_result_t controller_request_setpos(position_t position, uint16_t adc)`
Sets one station's nominal ADC value in RAM.

### `void controller_set_station1_lock(bool locked)`
When `locked`, rejects everything except `REQ_MOVE` to `POS_MIN` and
debug-exit. **Trigger source is unconfirmed** — nothing in
`controller.c` or `debug.c` calls this with `true`; it likely lives in
an updated `console.c` or `main.c` not yet reviewed.

### `static void begin_move(position_t target)`
Pre-marks the starting station as already-passed in `passed_mask`
(`1u << (position - POS_MIN)`, not `0u`) — a confirmed fix preventing a
spurious `PASS` report for the station a move starts at.

### `static void begin_home(uint32_t now)`
Always seeks position 1. **This is the sole timeout fallback — there is
no direction-aware recovery.** A previous version of this document (and
an earlier `agent.md` draft) specified a `ST_RECOVER` state with
direction-selection logic keyed to the last valid position; that state
does not exist in the current enum. This remains a genuine, unresolved
NEEDS DECISION item.

### `static bool reached(uint32_t now, uint32_t deadline_ms)`
`return deadline_ms && (int32_t)(now - deadline_ms) >= 0;` — confirmed
exact idiom, used throughout this file. `deadline_ms == 0` means "never
expires."

### 6.1 Timeout Policy

Confirmed: a non-jog move timeout brakes, reports timeout, then calls
`begin_home()`. If homing also times out, the controller faults: brake,
`motor_disable()`, `ST_FAULT`. Two stages, not a loop — matching the
original design's intent even though the specific `RECOVER`-state
mechanism it assumed doesn't exist.

### 6.2 Anti-Stiction Motion Tracking

**Newly confirmed, not in any earlier version of this document.**
`motion_progress_adc`/`motion_progress_ms` track whether the filtered
ADC has moved at least `CFG_ARRIVAL_WINDOW` counts within
`CFG_BRAKE_HOLD_MS`. If not, a `stiction_boost` flag forces full
`CFG_DUTY_NORMAL` duty for the rest of that move, rather than the
reduced approach/creep duty that may not overcome static friction on
the loaded mechanism. The final settle check (`target_correcting`
phase) uses the tighter `CFG_ARRIVAL_WINDOW`, not the wider
`CFG_POS_WINDOW`.

---

## 7. `console.c`

### `void console_init(void)`
Configures UART1 on **GP8 (TX) / GP9 (RX)** — confirmed by direct
ohmmeter measurement on physical hardware. Sets up an IRQ-driven RX
ring buffer.

### `static void console_uart_rx_irq(void)`
UART1 RX interrupt handler — pushes bytes into a ring buffer. RX cannot
lose characters to a busy main loop.

### `void console_poll(void)`
Drains the RX ring buffer, assembles a line, dispatches on newline. TX
is a blocking `uart_putc_raw()` loop, with its own elapsed time measured
into `tx_spin_us` for diagnostics — an acknowledged, monitored blocking
point, not an oversight.

### `void console_drain_events(void)`
Emits queued `PASS:N`/`ARR:N` style unsolicited messages.

### `static const char *state_name(sys_state_t s)`
Returns `"BOOT"`, `"IDLE"`, `"MOVING"`, `"APPROACH"`, `"HOMING"`,
`"FAULT"`, conditionally `"DEBUG"`. **Known bug, confirmed, not yet
fixed**: this array gates `"DEBUG"` behind `#ifdef LUFTFUGL_MONITOR`,
while `config.h`'s `sys_state_t` enum gates `ST_DEBUG` behind `#ifdef
LUFTFUGL_DEBUG` — a different flag. If a build defines
`LUFTFUGL_MONITOR` without `LUFTFUGL_DEBUG`, the array and the enum
disagree in size.

### `static bool resolve(const char *word)`
Confirmed: searches `help_entries[]` for exact and prefix matches to
recognize a typed command exists. **Not typo-correction or fuzzy
matching** — an earlier description of this function overstated its
capability.

### 7.1 Why Events Are Queued

Unchanged reasoning, still correct: `controller_tick()` runs in a 1 kHz
interrupt; a blocking UART write from there risks missing the next
tick's ADC sample. The ring buffer avoids this entirely.

---

## 8. `led.c`

**Not covered at all in any earlier version of this document** — full
addition.

### LED indication logic
Station colors are a base percentage (`LED_STATION_BRIGHTNESS_PERCENT`,
confirmed 3%) multiplied by a confirmed ambient-light zone multiplier
(§12) before transmission — replacing what was originally a single
fixed percentage. Alert indications (battery warning/critical, CO₂
sensor error) use a separately-configured base (confirmed 30%),
scaled by the same zone multiplier, with hysteresis on zone transitions
to avoid flicker.

### Power sequencing
GP0 (LED power enable, TPS22918) goes HIGH only when a non-dark color
must be shown; GP18 carries SK6812RGBWW data. GRBW wire order, white
channel held at zero to preserve hue saturation. Station 5 keeps GP0
continuously HIGH for its hazard-blink pattern rather than power-cycling
per pulse. The LED is completely unpowered during motion, between
stations, and at `EVENT_POSITION`.

### Caching
Confirmed: color output is cached, and retransmission is skipped when
the computed value hasn't changed — this is why the ambient-brightness
multiplier must be computed once per wake cycle and cached, not
recomputed inside the frequently-called color-generation path; doing
the latter caused a real, confirmed flicker bug (fixed).

---

## 9. `power_monitor.c` — confirmed architecture, not exhaustively audited

INA219-backed. `power_monitor_tick()` runs in the 1 kHz IRQ context
(confirmed) — whether its own I²C read there is genuinely non-blocking
under all conditions has been flagged as worth checking directly, not
confirmed either way.

Persists battery settings — warning/critical thresholds and the full
buzzer alert timing (frequency, duration, period) — via the confirmed
flash pattern (§13), with a compile-time static assertion enforcing the
record fills one `FLASH_PAGE_SIZE`.

---

## 10. `co2.c` — confirmed architecture, not exhaustively audited

SCD41 driver, reusing protocol work (CRC, retry-based `write_command`)
originally proven on the standalone `sdc41` project. Persists the
active room profile and the actual ppm limit values per severity level
— not fixed compile-time boundaries — via the same flash pattern.

**Confirmed, unfixed technical debt**: the single-shot measurement
command uses `sleep_ms(5000)`, violating the project's own non-blocking
principle. Acknowledged, not yet resolved.

---

## 11. `buzzer.c` — confirmed architecture, not exhaustively audited

Uses Direct Digital Synthesis: a phase accumulator advances per target
frequency, a 256-entry sine lookup table supplies waveform amplitude,
and a 100 kHz PWM carrier represents it, with DMA feeding the **PWM
registers directly** (not PIO — an earlier draft of `agent.md`
incorrectly described this as PIO-fed).

Confirmed piezo resonance: 2.0 kHz and 4.3 kHz. Practical compositions
stay within ~2.3–3.1 kHz for reliable loudness. RC filter on the drive
lines: `R4 = R5 = 1.5 Ω`, `C8 = 3.3 µF`, ~16 kHz cutoff — sized to
suppress the 100 kHz carrier's ripple without attenuating the
synthesized tone, not to shape audio-band harmonics.

Canonical debug commands: `buzzer play-2 <count>`, `buzzer play-3
<count>`. `play-3` is a three-part escalating sequence, repeated as one
unit per `<count>`. Both are test/evaluation commands, not wired into
the production per-station chirp trigger.

---

## 12. Ambient light sensing — confirmed behavior, module name/internals not directly reviewed

VEML7700, I²C address `0x10`, on the shared I²C0 bus — no dedicated
GPIO. Software shutdown between reads (confirmed ~0.5 µA typical in
shutdown vs. ~45 µA typical while measuring) rather than hardware
power-gating, since the current cost doesn't justify spending a GPIO.

Drives LED brightness via a four-zone lux-based multiplier (night/dim/
normal/bright), sampled once per wake cycle and cached — not recomputed
per `led_update()` call — with hysteresis on zone-boundary crossings to
prevent flicker.

---

## 13. Persistence — a real, repeated pattern across four subsystems

Confirmed: endstops, battery settings, CO₂ profile settings, and the
event-timer interval each persist via an equivalent pattern — magic
number, payload fields, checksum, `flash_range_erase()` +
`flash_range_program()` with interrupts disabled, each to its own
dedicated flash offset (confirmed non-overlapping: `0x3FF000`,
`0x3FE000`, `0x3FD000`, `0x3FC000`).

**A real sizing bug existed in the endstop implementation and has been
fixed.** `endstop_persist()` originally wrote a 16-byte record directly
to `flash_range_program()`, which requires multiples of
`FLASH_PAGE_SIZE` (256 bytes). The fix pads the record to exactly one
page with a compile-time `_Static_assert`, matching the pattern the
other three implementations already used correctly. A regression test
(`test/test_endstop_persistence.py`) confirms the fix, confirms the
flash offset is unchanged, and confirms pre-fix legacy records remain
readable (the padding field was appended after all named fields, so
byte offsets didn't shift).

Anything not using this pattern remains RAM-only by default.

---

## 14. `event_timer.c` — confirmed behavior, not exhaustively audited

DS3231 interface. Persists `event_interval_seconds` via the pattern
above. **Controls the manually armed, one-shot DS3231 event — it does
not set the sensor-sampling cadence**, which follows separate
scheduling. This distinction was a real, confirmed correction during
this rewrite; don't conflate the two.

---

## 15. `debug.c` / `debug_help.c` — help architecture

Command recognition (`help_entries[]`, searched by `resolve()`) is
separate from detailed help content. Detailed help is data-driven:
a `debug_help_document_t` per command (purpose, syntax lines, a
parameter table, ranked interactions, notes), rendered by one shared
`debug_help_render()` function, located via `debug_help_find(name,
page)`. A separate index (`debug_help_catalog.inc`) serves the
no-argument `help` listing. **The exact registration mechanism inside
`debug_help.c` — how a new document becomes findable — has not been
directly reviewed.**

---

## 16. Invariants

Confirmed to still apply, updated where the underlying mechanism
changed:

1. `state == ST_IDLE` implies the motor is braked, `duty == 0`.
2. `state == ST_FAULT` implies STBY is LOW.
3. The motor is never driven `DIR_REV` at `position == POS_MIN`.
4. The motor is never driven `DIR_FWD` at `position == POS_MAX`.
5. `position` is `POS_BETWEEN` or within `POS_MIN..POS_MAX` — Station 6's
   interaction with this invariant is not confirmed (§2).
6. No UART write originates from IRQ context.
7. The ambient-brightness multiplier is computed once per wake cycle
   and cached — never recomputed inside `led_update()`'s per-call path
   (confirmed necessary by a real, fixed flicker bug).

Invariants 3 and 4 remain the harness-protection guarantees — there are
still no physical end-stops.
