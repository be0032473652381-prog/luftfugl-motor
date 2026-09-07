---
name: luftfugl-workflow
description: Apply when modifying or reviewing luftfugl-motor firmware.
---

# skill.md — how to work on luftfugl-motor with Codex CLI

This file exists because the same mistakes keep recurring across
sessions — most concretely, new debug commands getting added without
being wired into the page-6 index or given a help entry. Read this
before starting any task on this project. It doesn't repeat facts
already in `hardware.md`, `agent.md`, or `AGENTS.md` — it captures the
*workflow* patterns and standing requirements those documents don't.

---

## The problem this file exists to solve

A new debug command has multiple places it needs to be registered, not
one. Adding the dispatch logic alone is not "done" — this has been
forgotten repeatedly. See the checklist below; treat it as mandatory,
not optional, for every new command.

## New debug command checklist — every item, every time

When adding any new `debug.c` console command:

1. **Dispatch logic** — the actual `else if (!strcmp(command, "..."))`
   branch in `submit()`.
2. **A `debug_help_document_t` for the command** — confirmed as the
   current architecture (`src/debug_help.h`): help is now data-driven,
   not bespoke per-command functions. Define the document (`purpose`,
   `syntax[]`, `parameters[]` as `debug_help_parameter_t` — name,
   meaning, default, example — `interactions[]`, `notes[]` as needed),
   then confirm it's actually reachable through `debug_help_find(name,
   page)`. **The exact registration mechanism lives in `debug_help.c`,
   not the header — quote that file before assuming how a new document
   gets wired in**, rather than guessing at the pattern.
3. **Page-6 command index** — the `command_rows[][4]` table in
   `frame_continue()`. If it's not here, it doesn't show up in the
   command list a user browsing page 6 actually sees.
4. **Single-letter alias**, if warranted — the `aliases[]` arrays (both
   upper and lower case) in `submit()`. Not every command needs one, but
   decide deliberately, don't just skip it by default.
5. **Report explicitly, every time**: "added to dispatch, help document
   registered and confirmed reachable via debug_help_find, page-6 index,
   alias: yes/no." Don't just say "command added" — confirm each item by
   name, and confirm the help document is actually *found*, not just
   defined.

If a prompt asks for a new command and doesn't explicitly repeat this
checklist, apply it anyway. This is a standing requirement, not
something that needs re-stating in every individual prompt.

---

## Standing requirements for every task, stated once here

Don't make me repeat these in every prompt — apply them by default:

- **Never `sleep_ms()` in any control or command path.** Non-blocking
  only. Use the existing deadline idiom exactly:
  `if (deadline_ms && (int32_t)(now - deadline_ms) >= 0) { /* fires */ }`
  — same pattern as `controller.c`'s `reached()`. A deadline of `0` means
  "never expires" — reuse this sentinel, don't invent a different one.
  **Known existing exception, not a precedent**: `co2.c`'s single-shot
  CO₂ command currently uses `sleep_ms(5000)`. This is acknowledged
  technical debt, not a case that weakens the rule — don't point to it
  to justify a new blocking call elsewhere. If touching this specific
  code path, converting it to the deadline idiom is a genuine
  improvement; leaving it alone is fine too, but don't extend the pattern.
- **Every tunable constant belongs in `config.h`**, never inline. This is
  an explicit house rule, not a style preference.
- **Reuse existing internal mechanisms before writing a parallel one.**
  If something similar already exists (a frequency-setting helper, a
  parsing pattern, a percentage-based brightness scheme), find and reuse
  it. Building a second, separate way to do the same underlying thing is
  a defect, not a valid alternative — this has been the actual root
  cause of confusion more than once.
- **Quote real code before assuming its structure.** If uncertain what
  an existing function, struct, or interface actually looks like, quote
  it directly from the source before building on top of an assumption.
  Guessing and being wrong costs more time than one extra read.
- **Build both configurations** (debug and production, or whatever the
  project's actual target names are) with `-Wall -Wextra`, confirm
  clean, report the result explicitly.
- **Build AND flash, then report the result.** This was previously
  "never flash" — that default is now reversed, deliberately, as of this
  update. After a clean build, flash using the confirmed-working command
  for this hardware:
  `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program build/luftfugl.elf verify reset exit"`
  Report the actual flash output (Programming/Verify result), not just
  that the build succeeded. If a specific task explicitly says not to
  flash, that overrides this default for that task only.
- **Report with evidence, not assertion.** "Confirmed via grep: ..." or
  a quoted code block beats "this is now handled correctly." If a claim
  can be shown, show it.

---

## GPIO allocation — check before adding, prefer software over hardware

- **Check source before documents, and check the most current source
  first.** `hardware.md` contains acknowledged stale wiring — it is not
  the first place to check. Priority order: `AGENTS.md`'s amendments
  first, then `src/config.h` (the actual compiled pin assignments), then
  the schematic itself, and only then `hardware.md` as a last, lower-
  confidence resort. This board doesn't even break out `GP24` — a fact
  only discovered when it was needed — a reminder that any single
  document here can be wrong, and the real source is the actual pin
  definitions in code plus current amendments, not an older summary.
- **`GP8`–`GP13` is not a preservable, fully-reserved SPI1 cluster.**
  Corrected: `GP8`/`GP9` already serve UART, and `GP10` already selects
  the CO₂ room profile (`CO2-LIMIT_AB`). Don't treat this range as a
  block to protect — check `config.h` directly for what's actually free
  within it, if anything.
- **Prefer a software-controlled solution over a new GPIO** when the
  power tradeoff allows it. A chip with a genuinely low shutdown current
  (single-digit µA or less) usually doesn't justify spending a GPIO on
  hardware power-gating — do the math against what's already an
  unavoidable floor (the DS3231's own continuous draw) before assuming a
  new switch is needed. The potentiometer's GPIO-gated supply was
  justified by a real, much larger current cost (hundreds of µA); most
  small I²C sensors won't clear that bar.
- **Don't spend an ADC-capable pin** (`GP26`–`GP29`) on a purely digital
  signal if a plain GPIO would do — these are scarce for a different
  reason (limited ADC-capable pins on this chip), independent of
  whatever else is or isn't allocated nearby.

---

## Persistence — a real, repeated pattern now, not a rare exception

**Correction**: this is no longer "only the endstops." Confirmed flash
persistence now exists for at least four subsystems, all using the
*identical* structure — magic number, version, payload fields, checksum,
written via `flash_range_erase()` + `flash_range_program()` with
interrupts disabled, each to its own dedicated flash offset constant:

- Endstops (`endstop_persist()`/`endstop_restore()`)
- Battery settings (`power_monitor_settings_save()`, `power_monitor.c`)
  — includes warning/critical thresholds and the full buzzer chirp
  timing (interval, repeat count, pause, duration, frequency)
- CO₂ profile settings (`co2.c`'s `settings_save()`/`settings_restore()`)
  — includes the active room profile and the actual ppm limit values
  per level, not just fixed compile-time zone boundaries
- Event-timer interval (`event_timer.c`'s `settings_save()`/
  `settings_restore()`) — the wake-cycle interval itself

**If adding new persistence, follow this exact established pattern** —
magic/version/checksum record, dedicated flash offset, erase-then-program
with interrupts disabled — rather than inventing a new mechanism.

**Open question, not yet confirmed**: with four independent subsystems
each computing its own flash offset, is there central coordination
ensuring none collide on the same physical sector? Confirm the actual
offset values (not just the symbolic constant names) don't overlap
before adding a fifth.

Anything *not* using this pattern remains RAM-only by default — that
part of the original guidance still holds; it's just no longer true that
only two values are persisted.

---

## Safety-critical, non-negotiable

**There are no physical end-stops on this mechanism.** Firmware is the
only thing preventing damage from over-travel. Never remove, weaken, or
bypass limit-checking logic without an explicit, direct instruction to
do so — and even then, flag it back clearly rather than applying it
silently. This applies regardless of how the request is framed (a
"quick test," a "bench-only" change, a refactor that incidentally
touches this logic).

---

## Documentation stays separate from code — don't edit specs to match code

`hardware.md` and `agent.md` describe what should be true. If the code
and a specification genuinely disagree, say so plainly — don't silently
edit the spec to match whatever the code currently does. That's a human
decision, not something to resolve by assumption.

When a change is genuinely new capability (a new sensor, a new command
category, a new architectural pattern), flag that it may be worth adding
to `hardware.md`/`agent.md` — don't edit those files directly unless
specifically asked to.

---

## Debug vs. production — keep them separate unless told otherwise

New experimental or test-only features default to debug-console-only
(`LUFTFUGL_MONITOR`-gated), not wired into production trigger paths,
unless a prompt explicitly says otherwise. When asked to add something
"for testing" or "to listen to," treat "don't touch the production path"
as implied even if not stated every time.

---

## Where the actual facts live — this file doesn't duplicate them

- **`hardware.md`** — BOM, pin allocation, schematic-confirmed facts,
  open hardware questions.
- **`agent.md`** — behavioral specification, state machine, protocol,
  timing constants, the `BENCH_TEST` profile.
- **`AGENTS.md`** — standing rules for how Codex should behave on this
  repo (precedence order between spec documents, the "don't modify specs"
  rule in full).

This file is about *workflow discipline* — the checklist and defaults
above — not a fourth copy of technical facts that live elsewhere and
would just drift out of sync if duplicated here.

---

## RP2040 architectural principles — filtered from a team brainstorm

A broader "RP2040 best practices" document was reviewed for inclusion
here. Some of it matches this project exactly and is worth codifying.
Some of it is genuinely good advice **for a different kind of project**
and would be actively wrong to apply here — included below with the
reasoning, not silently dropped, so nobody re-proposes it later without
understanding this was a deliberate call.

### Keep — already the house pattern, worth stating explicitly

- **Non-blocking, timer-driven control.** The existing single 1 kHz
  `repeating_timer` calling `on_tick()` (which dispatches
  `encoder_tick()`/`controller_tick()`/`power_monitor_tick()`) is the
  house pattern. Extend it for new periodic work — don't introduce a
  second, separate timing mechanism. The deadline idiom
  (`(int32_t)(now - deadline_ms) >= 0`, `0` meaning "never expires") is
  the standard, already used throughout `controller.c`.
- **IRQ + ring buffer for UART RX** — already exactly how `console.c`
  works. Use this as the template for anything with a similar shape.
- **PIO for strict-timing serial protocols** — already exactly how the
  WS2812 LED output works (`ws2812.pio`). The right tool when it's
  actually needed; nothing else on this board currently needs it.
- **Configuration structs over long parameter lists** — matches the
  existing `cfg_t` pattern already in `config.h`.
- **Defensive parameter validation, fixed-size buffers, no dynamic
  memory, check return codes** — already this codebase's practice
  throughout; keep doing it.
- **Documentation discipline** — the HBS-style structure just applied to
  the debug-menu help text (header, syntax, parameter table, ranked
  interactions, note) extends naturally to source-code file- and
  function-level doc comments too. Same principle, same rigor.

### Explicitly excluded — genuinely wrong fit, not just unnecessary

- **Dual-core architecture** (`multicore_fifo`, spinlocks, Core 0/Core 1
  separation). This is a single-core application — one tick dispatcher,
  one main loop, no evidence anywhere of `multicore_launch_core1()`. This
  workload (motor control, a handful of I²C sensors, LED/buzzer output)
  has no need to split across cores. This would add real complexity
  (memory barriers, FIFO messaging, spinlock discipline) for no actual
  gain. Do not introduce this without an explicit, specific reason tied
  to a real performance problem — not as a general "best practice"
  upgrade.

  **Open architectural question, not resolved here** — confirmed from
  `main.c`: the main loop currently calls `__wfi()` between iterations,
  with the 1 kHz safety tick remaining active in IRQ context throughout.
  This means the CPU wakes roughly 1000 times per second for the
  device's entire operating life — a fundamentally lighter sleep mode
  than true RP2040 dormant sleep (all clocks stopped), which an earlier
  design phase for the DS3231 wake signal assumed would be the actual
  sleep strategy. Whether this is a deliberate, considered choice (true
  dormant sleep has real complications — clock-restart sequencing,
  possible debug-probe/USB interaction issues) or an interim state
  before dormant sleep is properly implemented is **not yet confirmed**.
  This matters beyond wording: the earlier battery-life estimates for
  the DS3231's own contribution assumed the RP2040 itself would also be
  deeply asleep most of the time. If it's actually running continuously
  via repeated `__wfi()` cycles, total system power draw is likely
  higher than those estimates accounted for — worth revisiting the power
  budget once this is settled, not just this document's wording.
- **DMA for I²C or ADC sampling.** DMA earns its place for continuous,
  high-throughput data. **Correction**: the LED (`led.c`) is not a DMA
  example — it sends individual frames through the PIO FIFO directly and
  disables the PIO state machine afterward, confirmed from source. **The
  buzzer's DDS engine (`play-2`/`play-3`) is the actual existing DMA
  example in this codebase** — it allocates DMA resources to feed the
  PIO/PWM hardware continuously while a composition plays. This project's
  I²C traffic is small, infrequent transactions (once per wake cycle);
  ADC sampling is one reading per 1 ms tick. Neither is a firehose. DMA
  setup overhead here would likely exceed any benefit — this would be
  over-engineering, not an improvement.
- **"Prefer interrupts over polling," applied indiscriminately.** True
  for discrete events — the DS3231 wake signal correctly uses a GPIO
  interrupt. **Not true for continuous analog sampling** — the
  potentiometer's ADC read is correctly polled at a fixed rate; that's
  what sampling a continuously-varying analog signal looks like, not a
  design gap to "fix" by converting to interrupts.
- **`hardware_alarm` as the timer API.** The codebase uses the SDK's
  higher-level `repeating_timer` API throughout. The *principle*
  (hardware-timer-driven, not busy-wait) is already followed; switching
  the specific API without a real reason would just be disruptive
  churn on an already-working, central piece of the architecture.

