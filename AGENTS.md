# AGENTS.md — luftfugl

RP2040 firmware, Pico SDK, C. Drives an N20 gearmotor to five reed-sensed
positions via a TB6612FNG. This file holds standing rules. The task itself
arrives in the prompt.

## Specification documents — read before writing code

| File | Role |
|------|------|
| `agent.md` | **Authoritative spec.** Pins, ADC bands, state machine, protocol, build config. §15 resolves all previously ambiguous points |
| `hardware.md` | Wiring, BOM, power tree, grounding |
| `function-description.md` | Module structure, every function signature and contract, invariants |
| `debug-functions.md` | Debug monitor menus and safety interlocks |

Precedence on conflict: `agent.md`, then `function-description.md`, then the
rest. On a genuine contradiction, stop and ask.

## Never do these

1. **Do not modify any specification document.** They are inputs. If one looks
   wrong, say so — do not edit it to match the code.
2. **Do not invent values.** Every constant is already specified. If something
   is genuinely absent, stop and ask.
3. **Do not add scope.** No commands beyond `agent.md` §7 and
   `debug-functions.md`. No USB stdio, no persistence, no extra dependencies,
   no package installs.
4. **Do not run the motor.** No `move`, no `home`, no debug motion command, no
   automated smoke test that energises the driver. Bring-up is a staged manual
   procedure.
5. **Do not call `stdio_init_all()`.** The console is a raw UART0 driver
   (`agent.md` §15.3).

## Safety context

**The mechanism has no physical end-stops.** Positions 1 and 5 are enforced in
firmware only, and the moving part carries a wire harness that tears if driven
past either limit. This is why the code brakes rather than coasts, why limit
approaches use creep speed, and why recovery direction ignores travel history.

Consequences that must survive every edit:

- Never drive reverse at position 1 or forward at position 5, outside
  `ST_DEBUG`.
- Stopping is always a short brake (`AIN1 = AIN2 = 1`). Never coast. Never use
  STBY as a stop.
- The watchdog is mandatory: 100 ms, `pause_on_debug = true`, kicked from
  `controller_tick()` only.
- All eight invariants in `function-description.md` §9 must hold.

## Architecture rule

Two execution contexts, strictly separated:

- **1 kHz timer IRQ** — all sampling, all state transitions, all motor writes
- **Main loop** — all UART I/O and text formatting

They communicate only through the command mailbox and the event ring buffer.
No `printf` in the IRQ. No `motor_*` call from the main loop. No blocking calls
or floating-point division in the tick path.

## Code style

- C11, `-Wall -Wextra`, warnings fixed not suppressed. SDK-internal warnings
  are not yours to patch — report them.
- Static-qualify anything not in a header. Header guards, not `#pragma once`.
- `volatile` on every variable crossing the IRQ boundary.
- No dynamic allocation anywhere.
- Every tunable value lives in `src/config.h` and nowhere else.
- Comment *why*, not *what* — especially in `recover_direction()` and the
  arrival-detection split, where the reasoning is not obvious from the code.

## Build

```sh
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j4
```

`pico_sdk_import.cmake` is copied verbatim from
`$PICO_SDK_PATH/external/`, never hand-written. The full `CMakeLists.txt` is
given in `agent.md` §11 — the ordering in it is load-bearing.

Both configurations must build clean: `-DLUFTFUGL_DEBUG=ON` and `=OFF`.

## Flash

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 5000" \
        -c "program build/luftfugl.elf verify reset exit"
```

Confirm with me first that the Debug Probe is connected and motor supply VM is
disconnected. Then `picotool info` must report 16 MB.

## Reporting

State what you built and where it diverges. Do not summarise the specification
back — it was written here. Support acceptance claims with evidence: a command
and its output, or a file and line reference.
