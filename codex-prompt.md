# Task

Implement the luftfugl motor and position control firmware for the RP2040,
build it, and flash it to the target.

The complete specification already exists in this repository. Your job is to
implement it faithfully — not to redesign it, and not to fill gaps with
assumptions.

---

## Specification Documents

Read all five before writing any code. They are the source of truth.

| File | Role |
|------|------|
| `README.md` | Project overview and orientation |
| `agent.md` | **Authoritative behavioural specification.** Pin assignments, ADC bands, state machine, timing, protocol, build configuration |
| `hardware.md` | Physical build: wiring, BOM, power tree, grounding |
| `function-description.md` | Module structure, every public function signature and contract, execution model, invariants |
| `debug-functions.md` | Interactive debug monitor: menus, functions, safety interlocks |

**Precedence when documents disagree:** `agent.md` wins, then
`function-description.md`, then the others. If you find a genuine
contradiction, stop and report it rather than picking one.

**If a value or behaviour is not specified anywhere, stop and ask.** Do not
invent constants, thresholds, timings or error strings. Every number this
firmware needs is already in the documents. `agent.md` §15 resolves the points
that were previously ambiguous — check there before concluding something is
missing.

**Do not modify, "correct" or reformat any of the five specification
documents.** They are inputs, not deliverables. If you believe one is wrong,
stop and tell me. Editing a spec to match your implementation is the one
failure mode I cannot detect by reading your code.

**Implement exactly the specified scope — no more.** Do not add commands that
are not in `agent.md` §7 or `debug-functions.md`. Do not add USB stdio, a REPL,
telemetry formats, persistence, or convenience features. Do not add
dependencies beyond the Pico SDK. Do not install anything.

---

## Target

- MCU: RP2040 on a YD-RP2040 board, 16 MB flash
- SDK: Raspberry Pi Pico SDK, C (not C++, not MicroPython)
- Driver: TB6612FNG, channel A
- Toolchain: `arm-none-eabi-gcc`, CMake ≥ 3.13
- Flash and debug: Raspberry Pi Debug Probe over SWD; console on UART0

---

## Deliverables

```
CMakeLists.txt
pico_sdk_import.cmake
boards/luftfugl_rp2040.h
src/main.c
src/config.h
src/motor.c      src/motor.h
src/encoder.c    src/encoder.h
src/controller.c src/controller.h
src/console.c    src/console.h
src/debug.c      src/debug.h
.vscode/launch.json
.gitignore
```

Commit after each phase with a message naming the phase. Do not force-push, do
not rewrite history, do not commit `build/`.

Build configuration is specified in `agent.md` §11. The module split and every
function signature are specified in `function-description.md` §3–§8 and
`debug-functions.md`.

---

## Hard Requirements

These are not stylistic preferences. Violating any of them produces firmware
that can physically damage the mechanism.

1. **The mechanism has no physical end-stops.** Positions 1 and 5 are enforced
   in firmware only. The moving part carries a wire harness that tears if
   driven past either limit.

2. **All eight invariants in `function-description.md` §9 must hold.** In
   particular: never drive reverse at position 1, never drive forward at
   position 5, outside `ST_DEBUG`.

3. **Stopping is always a short brake** (`AIN1 = AIN2 = 1`). Never coast to a
   stop. Never use STBY as a stop — it releases the motor into a free spin.

4. **Two execution contexts, strictly separated.** All motor writes and state
   transitions happen in the 1 kHz timer IRQ. All UART I/O and text formatting
   happen in the main loop. They communicate only through the command mailbox
   and the event ring buffer. No `printf` in the IRQ. No `motor_*` call in the
   main loop.

5. **No blocking calls in the control path.** No `sleep_ms()`, no busy-waits,
   no floating-point division inside `controller_tick()` or `encoder_tick()`.

6. **Recovery direction is limit-aware, not history-aware**
   (`function-description.md` §6.5). This is the single most error-prone
   function in the system. Implement it exactly as written.

7. **Every tunable value lives in `config.h`.** No magic numbers anywhere else.

8. **Use the ADC band table from `agent.md` §2.7 verbatim.** Do not recompute
   it. An earlier revision of the spec had an arithmetic error here and the
   corrected table is the one in the document.

---

## Implementation Order

Build after each phase. Do not proceed with a failing build.

**Phase 1 — Skeleton.** `CMakeLists.txt` exactly as given in `agent.md` §11,
`pico_sdk_import.cmake` **copied verbatim from
`$PICO_SDK_PATH/external/pico_sdk_import.cmake`** (never hand-written), board
header, `config.h` with every constant from the specs, and stub module files
whose functions have empty bodies so the link succeeds. Verify: configures,
compiles and links.

**Phase 2 — Console.** UART0 init, line buffer, parser, all commands returning
stub responses. Verify: builds; every command string and error string from
`agent.md` §7 and §8 is present in the source.

**Phase 3 — Encoder.** ADC init, rolling average, band classifier, debounce,
the instant/confirmed split. Verify: builds; classifier boundaries match the
band table exactly.

**Phase 4 — Motor.** TB6612FNG abstraction, PWM setup at the specified wrap and
divider, drive/brake/coast/enable/disable. Verify: builds; truth table matches
`agent.md` §2.3.

**Phase 5 — Controller.** State machine, mailbox, event queue, limit
enforcement, timeouts, speed selection, arrival detection, recovery direction.
Verify: builds; every transition in `agent.md` §6.1 and
`function-description.md` §6.1 is implemented.

**Phase 6 — Debug monitor.** All seven menus behind `LUFTFUGL_DEBUG`, the
arming interlock, `ST_DEBUG`. Verify: builds both with and without
`-DLUFTFUGL_DEBUG=ON`.

**Phase 7 — Build and flash.** See below.

---

## Coding Standards

- C11, compiled with `-Wall -Wextra`. Fix warnings; do not suppress them.
- Static-qualify everything not in a header.
- Integer arithmetic only in the tick path.
- `volatile` on every variable crossing the IRQ/main-loop boundary.
- Header guards, not `#pragma once`.
- Comment *why*, not *what*. The specs explain what; the code should explain
  the non-obvious reasoning — particularly in `recover_direction()` and the
  arrival-detection split.
- No dynamic allocation anywhere.

---

## Build

```sh
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j4
```

Verify before flashing:

- Zero warnings from files under `src/` with `-Wall -Wextra`. Warnings
  originating inside the Pico SDK are not yours to fix; report them, do not
  patch the SDK.
- Build both ways: `-DLUFTFUGL_DEBUG=ON` and `-DLUFTFUGL_DEBUG=OFF`. Both must
  be clean.
- `build/luftfugl.elf` and `build/luftfugl.uf2` produced
- Report flash and RAM usage from the size output

If `PICO_SDK_PATH` is unset or the toolchain is missing, stop and report what
is needed rather than attempting to install anything.

---

## Flash

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 5000" \
        -c "program build/luftfugl.elf verify reset exit"
```

**Before flashing, confirm with me that the Debug Probe is connected and the
motor supply (VM) is disconnected.** Do not flash unprompted.

After flashing:

```sh
picotool info
```

Report the flash size. It must read 16 MB. If it does not, stop —
`PICO_FLASH_SIZE_BYTES` is wrong and further work is unsafe.

**Do not attempt to run the motor.** Do not send `move`, `home`, or any debug
menu motion command. Do not automate a smoke test that energises the driver.
The firmware leaves STBY low until initialisation completes, and physical
bring-up follows the staged procedure in `agent.md` §13 with the motor
uncoupled — that is a human, hands-on process.

---

## Acceptance Checklist

Report against each item, **with evidence** — a command and its output, a file
and line reference, or a quoted code fragment. An unsupported "done" is not an
acceptance.

- [ ] All five specification documents read
- [ ] Every file in Deliverables created
- [ ] Every function in `function-description.md` §3–§8 implemented with the
      specified signature
- [ ] Every menu and function in `debug-functions.md` implemented
- [ ] All eight invariants verifiable by inspection; state where each is
      enforced
- [ ] ADC band constants match `agent.md` §2.7 exactly
- [ ] All command and error strings match `agent.md` §7–§8 exactly
- [ ] `TIMEOUT_STEP_MS` = 1500, PWM wrap 255, clkdiv 97.6875
- [ ] Builds clean with `-Wall -Wextra`, both debug-on and debug-off
- [ ] Flashed, `picotool info` reports 16 MB
- [ ] Watchdog enabled at 100 ms with `pause_on_debug = true`, kicked from
      `controller_tick()` only (`agent.md` §15.8)
- [ ] No `stdio_init_all()` anywhere; console is raw UART0 (`agent.md` §15.3)
- [ ] Deadline comparisons are wrap-safe signed subtraction (`agent.md` §15.9)
- [ ] No command or feature exists that is not in the specification
- [ ] No specification document was modified — confirm with `git diff --stat`
- [ ] No motor motion attempted

---

## Report Format

When done, give me:

1. What was built, file by file, with line counts
2. Flash and RAM usage
3. The acceptance checklist with each item marked
4. Any place the specification was ambiguous, and whether you stopped to ask
   or resolved it yourself — see the rule below
5. Anything you could not implement and why

**Ambiguity rule.** If an ambiguity could change behaviour — a threshold, a
state transition, a response string, a safety check — stop and ask before
writing the code. Only resolve something yourself if it is purely cosmetic
(variable naming, comment wording, file ordering), and list every such
resolution in the report.

Do not summarise the specification back to me. I wrote it. Tell me what you
built and where it diverges.
