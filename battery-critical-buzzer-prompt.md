# Task — audible chirp alert when battery reaches critical

## Requirement

When `power_monitor_snapshot()` reports `valid && bus_mv < CFG_BATTERY_CRITICAL_MV`
(the same threshold the LED critical-blink already uses — reuse it, do not
add a separate constant), sound a single buzzer chirp at **2.7 kHz**, then
repeat every **48 seconds** for as long as the condition remains true.
Stop the moment the battery recovers above the threshold — don't let one
final chirp fire right at the recovery boundary.

This is **independent of the LED's current mode**. `led.c` has several
forced-mode early-returns (`led on`/`led off`/raw override) that bypass
its own battery check — the chirp must not inherit that behavior. A
critically low battery should audibly warn even if someone's actively
testing LED modes on the bench.

## `buzzer.h` currently has no single-tone primitive — add one

**Confirmed**: the buzzer module only implements the fixed bird-call
pattern (`buzzer_play()`), no arbitrary-frequency single-tone function
exists. This needs adding, not discovering.

**Add a new public function** — e.g. `buzzer_tone(uint32_t frequency_hz,
uint32_t duration_ms)` or whatever naming fits the module's existing
conventions — that plays one fixed-frequency tone for a fixed duration,
independent of the bird-call melody logic.

**Reuse the underlying PWM/frequency-setting mechanism, don't duplicate
it.** The bird-call pattern already varies its tone across
200–12,000 Hz to simulate bird-like calls (confirmed from `debug.c`'s
help text) — this means some internal, likely `static`, helper for
setting an arbitrary PWM frequency almost certainly already exists inside
`buzzer.c`, just not exposed publicly. **Find and reuse that internal
mechanism** for the new single-tone function rather than writing a
second, separate way to drive the same PWM hardware (BIN1/BIN2 GP6/GP7,
PWMB GP16, confirmed differential BO1/BO2 drive).

**Duration**: not specified by the requirement — propose ~150–200 ms per
chirp as a starting point, matching the existing bird-call pattern's own
200 ms gap convention, and say so explicitly in your report rather than
silently picking a number. Adjust if the existing melody's individual
call duration suggests a better match once you're actually looking at
the code.

## Timing — non-blocking, same pattern as everywhere else in this project

No `sleep_ms()`. Use the same deadline-comparison idiom already
established throughout `controller.c`:

```c
if (deadline_ms && (int32_t)(now - deadline_ms) >= 0) { /* fires */ }
```

Track "next chirp due" as a millisecond deadline, checked once per main
loop pass (same execution context as `led_update()` — this belongs in
the main `for(;;)` loop in `main.c`, not the 1 kHz tick, and not inside
`led_update()` itself).

## Where this logic should live

Proposed: a small new function, called from `main.c`'s main loop
alongside `led_update()` — e.g. `battery_alert_poll()` or similar, in
whatever file makes sense given `buzzer.h`'s actual location (possibly
`buzzer.c` itself, since it's the module that already owns tone
generation — your call once you've seen the real file structure).
**Not inside `led.c`** — see the independence requirement above for why.

State machine needed:
- Not critical → no chirp, no timer running.
- Just entered critical → chirp immediately, set next-chirp deadline to
  now + 48000 ms.
- Still critical, deadline reached → chirp, reschedule +48000 ms.
- Recovered above threshold → clear any pending deadline, no more chirps
  until it goes critical again (and when it does, chirp immediately
  again, not wait a stale 48s from before).

## Verification

Report, don't just claim:

1. Quote the new `buzzer_tone()` (or equivalent) function in full, and
   confirm whether it reuses an existing internal frequency-setting
   helper or had to build one — either is fine, just report which.
2. Confirm which file the new polling function lives in, and why.
3. Confirm the 2.7 kHz tone is generated correctly given whatever the
   existing PWM/frequency-setting mechanism actually is — quote the
   relevant code, don't just assert it works.
4. Confirm via `grep` that `led.c` was **not** modified by this change —
   the independence requirement means this shouldn't touch that file at
   all.
5. Confirm build is clean, `-Wall -Wextra`, both targets if applicable.

Do not flash — build and report only, I load onto hardware myself.
