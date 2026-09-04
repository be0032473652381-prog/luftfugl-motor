# Task — Version 2: Potentiometer Position Sensing

Migrate the luftfugl firmware from reed-switch resistor-ladder sensing to a
single potentiometer. Build, verify both configurations, and flash.

The firmware currently on `main` is version 1 and works. This is a migration,
not a rewrite: motor control, console protocol, watchdog, execution model and
the debug monitor structure are all unchanged.

---

## Authoritative Document

**`v2-sensing.md`** specifies this change completely and supersedes named
sections of the other documents. Its §0 table lists exactly what it replaces.

Precedence for version 2: `v2-sensing.md` first, then `agent.md`, then
`function-description.md`, then the rest. Where `v2-sensing.md` is silent, the
existing specification stands unchanged.

Read `v2-sensing.md` in full before any edit. Re-read `agent.md` §15 and
`debug-functions.md` §13 and §14 — those resolutions still apply.

---

## What This Change Actually Is

A potentiometer is an absolute continuous sensor. The reed ladder was discrete
with an invalid region. That difference is the whole migration:

- Discrete bands become position windows around five nominal values.
- "Between reeds is unknown" becomes "between stations is a known angle".
- Boot homing is deleted — position is available at power-up.
- Over-travel becomes detectable rather than merely avoided.
- `ST_RECOVER` and `recover_direction()` are deleted, not fixed.
- Stall and wrong-direction detection become possible and are required.

If you find yourself preserving version 1 logic that treats a high reading as
invalid, stop — that is the central thing this change removes.

---

## Deliverables

Modify: `src/config.h`, `src/config.c`, `src/encoder.c`, `src/encoder.h`,
`src/controller.c`, `src/controller.h`, `src/console.c`, `src/debug.c`.

No new modules. No new files.

---

## Hard Requirements

1. **The mechanism still has no physical end-stops.** Positions 1 and 5 are
   still firmware-enforced and the harness still tears if exceeded. Continuous
   sensing makes over-travel detectable; it does not make it safe.

2. **Over-travel faults immediately** — brake, disable, `ERR: overtravel`,
   `ST_FAULT`, in the same tick. No creep, no recovery, no auto-home.

3. **Stall and direction checks are required**, not optional. They are the only
   fault detection available given the TB6612FNG has no fault output.

4. **Invariants 1, 2, 5, 6, 7 and 8 from `function-description.md` §9 are
   unchanged and must hold.** Invariants 3 and 4 — never reverse at position 1,
   never forward at position 5 — are now enforced against the continuous
   reading rather than the last confirmed band; state where.

5. **The two-context execution model is unchanged.** All sensing, state
   transitions and motor writes in the 1 kHz IRQ; all UART I/O in the main
   loop.

6. **Stopping is still always a short brake.** Never coast. Never use STBY as
   a stop.

7. **Every new constant goes in `config.h`** and is overridable through the
   existing `cfg_t` mechanism, with the validation specified in
   `v2-sensing.md` §9.

8. **The console protocol is unchanged** except for the three new error strings
   in `v2-sensing.md` §12. Do not alter any existing response string.

---

## Phases

Build after each. Commit after each, naming the phase. Use `apply_patch`; do
not overwrite whole files.

**Phase V2.1 — Constants.** Remove `BAND_*_MAX` and `TIMEOUT_RECOVER_MS`. Add
the constants from §9 with `cfg_t` entries and validation. Update
`dbg_cfg_list`, `dbg_cfg_set`, `dbg_cfg_export`.

**Phase V2.2 — Encoder.** Replace `classify()` with `position_at()`. Add
`POS_BETWEEN`, `encoder_in_safe_range()`, `encoder_error_to()`,
`encoder_nominal()`. Retain the rolling average and debounce unchanged.

**Phase V2.3 — Controller.** Error-driven movement and speed selection per §7.1
and §7.2. Delete `ST_RECOVER` and `recover_direction()`. Add over-travel, stall
and direction faults. Update boot per §6 — no homing.

**Phase V2.4 — Console.** Three new error strings. `POS:?` for both
`POS_UNKNOWN` and `POS_BETWEEN`.

**Phase V2.5 — Debug monitor.** Menu changes per §10: position table, position
error, window sweep, over-travel test, revised `dbg_cal_positions`, new stall
and direction status in menu 8.

**Phase V2.6 — Build and flash.** Both configurations clean. Confirm
`build/luftfugl.elf` exists and its timestamp is newer than the last source
change before flashing.

---

## Verification Before Flashing

- Zero warnings from `src/` with `-Wall -Wextra`, both `LUFTFUGL_DEBUG=ON` and
  `OFF`
- No reference to `BAND_`, `ST_RECOVER`, `recover_direction` or
  `TIMEOUT_RECOVER` remains anywhere in `src/` — show the grep
- `git diff --stat -- '*.md'` is empty
- Report flash and RAM usage for both builds

Flash with:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 5000" \
        -c "program build/luftfugl.elf verify reset exit"
```

The Debug Probe is permanently connected on this bench and VM state is
irrelevant to flashing. Flash automatically after every successful build;
do not request confirmation.

**Do not attempt to move the motor.** Bring-up follows `v2-sensing.md` §11 and
is a human, hands-on process.

---

## Known Defects to Fix in Passing

Two faults found on hardware in version 1. Fix both:

1. **`margin 23%%`** — doubled percent sign in the debug header. A format
   escape applied twice.
2. **Lowercase `dbg` returns `ERR: unknown command` while `DBG` works.**
   `agent.md` §7 requires case-insensitive commands. The case-folding is being
   missed on the path that reaches the `dbg` branch.

Also commit any uncommitted §14 work still in the working tree before starting
Phase V2.1, as a separate commit.

---

## Report

1. Files changed, with line counts
2. Flash and RAM usage, both configurations
3. Grep evidence that version 1 band and recovery code is fully removed
4. Where invariants 3 and 4 are now enforced, quoted
5. Any ambiguity — stop and ask before writing code if it could change
   behaviour; resolve only cosmetic points, and list them

Do not summarise the specification back. Tell me what changed and where it
diverges.
