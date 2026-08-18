# Task — Sandbox: Find the Cause of the Window-Scaling Undershoot

The earlier "fixed 25-count coast" theory is wrong. Measure what is actually
happening during arrival, on a disposable branch, before proposing any fix.

Branch from **`main`**, not from any earlier sandbox branch. A previous
attempt introduced `HIGH_ENDSTOP_ADC`/`LOW_ENDSTOP_ADC` and a
"position unknown" regression that was never explained — do not build on
that branch or reintroduce whatever caused it.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## What the data actually shows

Seven arrivals at `POS_WINDOW=60`: undershoot 54, 55, 55, 56, 54 (mean 54.8,
stdev 0.75). Four arrivals at `POS_WINDOW=30`: undershoot 25, 25, 25, 26
(mean 25.25, stdev 0.43).

Ratio of undershoot: 2.18×. Ratio of window: 2.00×. The undershoot scales
with the window — roughly 91–92% of the window radius in both cases — not a
fixed mechanical coast distance as originally assumed.

**Leading hypothesis:** the 12 ms confirmation debounce starts counting the
moment `|error| <= POS_WINDOW` first becomes true, but the mechanism may
still be moving at that instant. If it continues drifting at a roughly
constant speed during those 12 ms, a wider window is entered earlier (further
from target) and more drift accumulates before debounce completes — which
would produce settling near the window's inner edge, scaling with window
size, exactly as observed.

This is a hypothesis, not a conclusion. The task is to measure it directly,
not to assume it and patch around it.

---

## Requirement: reversible, as before

1. New branch, `sandbox/debounce-drift`, from `main`.
2. Build to `build-sandbox`, not `build`. Do not touch `build/luftfugl.elf`.
3. Do not flash automatically. Report when ready; I flash and test by hand.
4. Discardable with `git checkout main` and deleting the branch, no trace
   left on `main`.

---

## Instrumentation — measure, do not fix yet

Add a temporary trace, gated behind a build flag (default off, matching the
pattern used for earlier diagnostics in this project), that captures the
filtered ADC on every tick from the moment the brake condition first becomes
true through to confirmed arrival:

```
entry_tick, entry_adc          -- first tick where |error| <= POS_WINDOW
tick, adc                      -- every following tick, for at least 30 ticks
                                   or until confirmed, whichever is longer
confirm_tick, confirm_adc       -- the tick arrival was confirmed on
```

Report this as a table via a console command (`trace` or similar, following
this project's existing command conventions), not as raw serial spam.

This directly answers the real question: is the mechanism still moving
substantially between entry and confirmation, and does the distance it moves
in that window match the observed undershoot?

---

## Test matrix

Run with the trace enabled, `POS_WINDOW` at its current default (60):

1. `pos 3` from station 5's direction (approaching from above)
2. `pos 5` from station 3's direction (approaching from below)

For each, report the full entry-to-confirm trace, not just the final
arrival line.

Then repeat with `POS_WINDOW` set to 30 via `cfg`, same two moves, same full
traces.

---

## What to look for, and report explicitly

- **Is `entry_adc` already close to the final confirmed value**, or does the
  trace show meaningful continued movement after entry? This is the
  determining observation.
- **If it is still moving**: roughly what speed (counts per tick) during
  that window, and does entry-to-confirm distance approximate the observed
  undershoot at each window size?
- **If it is NOT still moving** — i.e. `entry_adc` is already near the final
  rest point — then the debounce-drift hypothesis is wrong, and the actual
  cause is something else. Say so plainly rather than forcing the data to
  fit the hypothesis, and suggest what to look at next (candidates: the
  brake command itself not firing promptly, the arrival comparison logic
  using a stale reading, or the window/debounce interacting with something
  in the state machine not yet considered).

---

## Do not implement a fix in this task

This task is measurement only. Once the actual mechanism is identified from
the trace data, the fix — whatever form it takes — is a separate, later task
informed by what was actually measured here.

Report the traces verbatim. I will read them before deciding what happens
next.
