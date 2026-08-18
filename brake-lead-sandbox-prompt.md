# Task — Sandbox: Brake-Lead Compensation, Reversible

Test whether decoupling the brake trigger from the arrival tolerance removes
the systematic ~25-count undershoot, without touching the values currently in
use on hardware.

Do not use `sed -i` by line number or `perl -0pi -e` on source files. Use
`apply_patch`.

---

## What was found

Four arrivals, two stations, one direction each:

| Move | Direction | Target | Rest | Undershoot |
|---|---|---|---|---|
| → 5 | up | 1844 | 1819 | 25 |
| → 3 | down | 850 | 875 | 25 |
| → 5 | up | 1844 | 1819 | 25 |
| → 3 | down | 850 | 876 | 26 |

Mean 25.25 counts, stdev 0.43. This is a fixed coast between the brake
command and the rotor actually stopping — not noise, and consistent in both
directions once read as "distance short of completed travel" rather than raw
signed error.

`POS_WINDOW` currently triggers both the brake and the arrival confirmation.
Narrowing it alone does not reduce the coast — it moves the confirmation band
closer to target while the mechanism still coasts the same ~25 counts past
the trigger point, missing the band entirely below roughly window=25. This is
the likely mechanism behind the oscillation problems fought earlier in this
project.

---

## Requirement: reversible via a sandbox build

**Do not modify `main` or the flashed configuration in place.** This must be
testable and then fully discardable.

1. Create a new git branch, `sandbox/brake-lead`, from the current committed
   state. All work happens on this branch.
2. Build to a separate directory, `build-sandbox`, not `build`. The existing
   `build` output and whatever is currently flashed must remain untouched
   until I explicitly decide to adopt the result.
3. Do not flash automatically. Report when the sandbox build is ready; I will
   flash it myself and can reflash the current `build/luftfugl.elf` at any
   time to return to today's known-good state.
4. Everything in this task is on the branch. If I decide not to adopt it,
   `git checkout main` and delete the branch reverts everything with nothing
   left behind — no stray commits on `main`, no changed defaults in the
   version currently running.

---

## Change 1 — separate the brake trigger from the arrival tolerance

Add a new constant, independent of `POS_WINDOW`:

```
BRAKE_LEAD_COUNTS
```

The brake must fire when `magnitude <= POS_WINDOW + BRAKE_LEAD_COUNTS`, not
at `magnitude <= POS_WINDOW`. Arrival confirmation continues to require the
debounced reading to be within `POS_WINDOW` of target, unchanged.

Starting value: `BRAKE_LEAD_COUNTS = 25`. This is a first estimate from four
samples on two of five stations — expect to retune it from the data gathered
below, not treat it as final.

Add both to the runtime `cfg` set so they can be adjusted without a rebuild,
same as the existing duty and window constants.

## Change 2 — arrival logging for characterisation

Extend whatever arrival logging already exists (the console currently reports
`arrived ADC <n> <angle> deg err <n>` on each `pos` completion) so that it
also reports, for each arrival:

- target station
- approach direction (which way the error was closing)
- rest ADC
- signed error against target

This is already close to what the console prints today — confirm it, and add
a `history` or extend `stations` output to show the last N arrivals in a
table so a run across all five stations can be reviewed without manually
transcribing console lines.

## Change 3 — do not touch anything else

`POS_WINDOW`, all duty constants, `APPROACH_COUNTS`, and every other value
stay exactly as currently configured. This task adds one new constant and
changes one comparison (`arrived()`/brake-trigger logic) to use it. Nothing
else in the motion or arrival path should change.

---

## Verification, on the sandbox build only

1. Confirm the branch, build directory, and build separation are as
   specified — report the branch name and confirm `build/` was not touched.
2. With `BRAKE_LEAD_COUNTS = 25` and `POS_WINDOW` unchanged at its current
   value, report the arrival error for `pos 3` and `pos 5` from both
   directions — this checks whether the compensated brake now lands closer to
   target than the uncompensated 25-count undershoot.
3. Do not narrow `POS_WINDOW` in this task. Compensation is verified first,
   narrowing is a separate follow-up once the compensated bias is confirmed
   small and stable across more than two stations.
4. Report the actual console output for each test arrival, not a summary.

I will drive the moves myself and read the results before deciding whether to
merge, retune `BRAKE_LEAD_COUNTS`, or discard the branch.
