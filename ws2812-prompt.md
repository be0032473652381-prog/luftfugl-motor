# Task — WS2812B Status LED on GP18

A single WS2812B addressable LED is fitted, data in on **GP18**.

Light it dusty rose (RGB 235, 160, 160) when the mechanism is at station 5.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## Why PIO, not bit-banging

The WS2812B needs a 800 kHz waveform with 0.4 µs and 0.8 µs pulse widths, held
to about ±150 ns. Bit-banging it from the main loop would be perturbed by any
UART write, and from the tick it would blow the 1 ms budget.

Use the RP2040's PIO. `pico-examples` ships `ws2812.pio`; the SDK provides
`pio_sm_put_blocking()`. One state machine, one instruction memory slot, and
the CPU cost per update is a single 32-bit write.

Add `hardware_pio` to `target_link_libraries` and the `.pio` file to the build
with `pico_generate_pio_header()`.

## Pin

GP18. Note `agent.md` §9 reserves GP16–GP21 for SPI0 and I2S expansion — this
is a deliberate allocation of one of them. Record that in the commit message; do
not edit `agent.md`.

Add to `config.h`:

```c
#define PIN_LED_DATA   18
#define LED_COUNT      1
```

## Colour

```c
#define LED_STATION5_R  235
#define LED_STATION5_G  160
#define LED_STATION5_B  160
```

**WS2812B expects GRB order, not RGB.** The 24-bit word is
`(G << 16) | (R << 8) | B`, shifted left 8 for the PIO's 32-bit output. Getting
this wrong gives a pale green instead of dusty rose, which is the usual first
symptom.

## Behaviour

| Condition | LED |
|-----------|-----|
| Confirmed at station 5 | Dusty rose, 235/160/160 |
| Anything else | Off |

"At station 5" means `encoder_confirmed() == 5` — the debounced value, the same
test that reports `ARR:5`. Not the instantaneous reading, or the LED will
flicker as the mechanism passes through.

## Where it is driven

The LED is a display, so it belongs with the display code, not the control
loop.

- `led_init()` called from `main()` after `encoder_init()`.
- `led_update()` called from the **main loop**, not the tick. It reads
  `encoder_confirmed()` and writes the PIO only when the colour changes.
- Never called from `controller_tick()`. Invariant 8 — UART and display work
  in the main loop only — extends to this.

Writing the same colour repeatedly is harmless but pointless; keep a shadow of
the last value and skip if unchanged.

## Console

Add an `led` command so the wiring can be tested without moving the mechanism:

```
> led
 led          station 5: off    (currently at station 3)

> led on
 led on       forced dusty rose 235,160,160

> led off
 led off      forced off

> led auto
 led auto     following station 5
```

`led on` and `led off` override until `led auto` restores normal behaviour.
That makes it possible to prove the LED and its wiring work before trusting the
position logic.

Add to the command block and to `help`, with `help led` giving the colour, the
pin and the GRB note.

## Also

Add `LED` to the status region if a field is free — showing `on`, `off` or
`forced` — so the operator can see the state without looking at the hardware.
Only if it fits the existing grid without shifting a column.

---

## Verification

1. `led on` — the LED lights dusty rose. Confirm the colour looks correct and
   is not green, which would mean the GRB order is wrong.
2. `led off` — dark.
3. `led auto` then `pos 5` — lights on arrival at station 5.
4. `pos 3` — goes out.
5. Passing through station 5 during a longer move — confirm it does not
   flicker, since the confirmed value is used rather than the instantaneous
   one.
6. Report the PIO state machine and instruction memory used, and confirm the
   tick timing is unchanged — `tick` should still show no overruns.

Item 1 is worth doing first. If the colour is wrong, everything after it is
wasted effort.
