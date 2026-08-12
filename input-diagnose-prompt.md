# Task — Find Why Debug Console Input Is Lost

**Do not change any behaviour until step 1 is reported.** Instrument, measure,
report. Then fix what the measurement shows.

The console renders correctly. Typed characters produce no echo and no result.
UART0 has passed a hardware loopback test, and the production console on the
same wiring accepts commands normally — so the RX path to the chip works.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## The leading hypothesis, with numbers

At 115200 8N1 each character takes 86.8 µs. The RP2040 UART has a **32-byte RX
FIFO**, so it overflows **2.78 ms** after the last read.

`console_poll()` — the only thing that reads RX — runs in the main loop. If the
main loop is blocked writing output, RX is not read and characters are silently
discarded by the hardware.

| Output | Bytes | Blocking time |
|--------|-------|---------------|
| Full frame draw | ~2000 | **174 ms** = 62 RX FIFO depths |
| Field refresh at 5 Hz | 264 | **22.9 ms** = 8 RX FIFO depths |

`write_text()` calls `uart_putc_raw()`, which **spins while the TX FIFO is
full**. So every frame draw is a 174 ms window in which every typed character
is lost, and every refresh a 22.9 ms window.

This also explains the earlier symptom where the interface received its own
output: with RX overrunning and the terminal echoing, the parser saw fragments
like `[16;1H` rather than what was typed.

---

## Step 1 — Measure, do not guess

Add temporary counters, reported by a new `diag` command:

```c
static volatile uint32_t rx_chars;        /* characters read from the UART */
static volatile uint32_t rx_overruns;     /* UARTRSR/UARTDR overrun bit set */
static volatile uint32_t poll_calls;      /* console_poll() entries */
static volatile uint32_t poll_max_gap_us; /* longest gap between poll calls */
static volatile uint32_t tx_spin_us;      /* total time spent inside uart_putc_raw */
```

- Increment `rx_chars` on every character read.
- Read the overrun flag: on RP2040, `uart_get_hw(uart0)->rsr & UART_UARTRSR_OE_BITS`.
  Count and clear it.
- Record the gap between `console_poll()` entries with `time_us_32()` and keep
  the maximum.
- Bracket `uart_putc_raw()` and accumulate the spin time.

Then type `diag` and report the five numbers. Also type `abc` slowly, one
character per second, and report `rx_chars` before and after.

**This distinguishes the possibilities definitively:**

| Result | Meaning |
|--------|---------|
| `rx_chars` does not increase when typing | Characters never reach the UART — wiring or routing, not blocking |
| `rx_chars` increases, `rx_overruns` > 0 | The FIFO is overflowing — blocking confirmed |
| `rx_chars` increases, no overruns, still no echo | Characters arrive and are read but the handler discards them — a dispatch bug |
| `poll_max_gap_us` > 2780 | The main loop stalls longer than the RX FIFO can hold |

Report all of it before writing a fix.

---

## Step 2 — The fix the measurement will most likely require

If blocking is confirmed, neither a bigger buffer nor a faster drain is
sufficient. The structural fix is to stop depending on the main loop for RX.

**Move RX to an interrupt.**

```c
static volatile char rx_ring[128];
static volatile uint8_t rx_head, rx_tail;

static void on_uart_rx(void) {
    while (uart_is_readable(uart0)) {
        uint8_t next = (rx_head + 1u) % sizeof rx_ring;
        char c = uart_getc(uart0);
        if (next != rx_tail) { rx_ring[rx_head] = c; rx_head = next; }
    }
}

/* in console_init(): */
irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
irq_set_enabled(UART0_IRQ, true);
uart_set_irq_enables(uart0, true, false);   /* RX only, not TX */
```

`console_poll()` then drains `rx_ring` instead of the hardware FIFO.

With this, characters are captured within microseconds of arriving no matter
what the main loop is doing. A 174 ms frame draw becomes harmless.

**And make output non-blocking.** Every debug write goes through a ring drained
only while `uart_is_writable(uart0)` is true, returning immediately when the TX
FIFO is full. No path may call `uart_putc_raw()` in a spin.

Both changes are needed. The interrupt stops characters being lost; the
non-blocking output stops the main loop stalling at all.

---

## Step 3 — If the measurement shows something else

If `rx_chars` does not increase when typing, blocking is not the cause. In that
case report which of these it is:

- `console_poll()` is not being called at all — check the main loop still
  reaches it when the interface is active
- characters are read but `dbg_active()` is false, so they go to the production
  parser whose output is overwritten by the frame
- the character is consumed by a branch in `dbg_handle_key()` before reaching
  the line buffer — dump the byte value and the branch taken

---

## Report

1. The five counter values, at rest and after typing `abc`
2. Which row of the table above matches
3. What you changed, and why that follows from the measurement
4. Console output showing `adc` typed character by character, each echoing,
   with the result appearing on Enter

Item 4 is the acceptance test. Everything else is diagnosis.

Do not run the motor.
