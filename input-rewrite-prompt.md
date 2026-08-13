# Task — Rewrite the Debug Console Input Routine From Scratch

The input path has been patched repeatedly and still does not work. Stop
patching it. Delete it and write a new one.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## What is already established

Four sandboxes have eliminated everything outside this routine. Do not
re-investigate any of it.

| Established | Evidence |
|---|---|
| UART hardware, wiring, probe and ground are sound | Sandbox 1: every character echoed |
| Blocking output does not starve RX | Sandbox 2: `rx 186973` vs `echoed 186972`, 24 overruns in 1181 blocks — 0.4% |
| Characters reach the handler and lines submit correctly | Sandbox 3: `consumed_by=CMDLINE`, then `SUBMIT line="adc"` |
| Command lookup and handlers work | `DISPATCH matched=YES handler=cmd_adc`, `RESULT "raw 1841 avg 1841 pos ?"` |
| The renderer's own output couples into RX | `ESC[16;1H` arrives as input; a terminal reply would end in `R`, not `H` |

So the machinery works in isolation and fails when assembled. The routine that
assembles it is what to replace.

---

## Delete

Remove entirely from `src/debug.c`:

- `dbg_handle_key()` and every branch in it
- the `prompt` enum, `handle_prompt_char()`, `finish_prompt()`,
  `prompt_swallow_lf`, and every `PROMPT_*` constant
- `input[]`, `input_len`, and every place they are touched
- the escape-filter and renderer-active checks added most recently
- `jog_mode`, `menu_focused`, `dbg_menu_focus()`, `action`-driven key handling

If anything becomes unreferenced, delete it rather than leaving it.

---

## The new routine, complete

One function, one buffer, no modes, no prompts, no state beyond the line
itself.

```c
#define CMD_MAX 48

static char     cmd[CMD_MAX + 1];
static uint8_t  cmd_len;
static bool     in_escape;        /* discarding a CSI sequence */

void dbg_input_char(char c)
{
    /* 1. Swallow escape sequences. The console has no use for them as input,
          and the renderer's own output couples into RX on this bench. */
    if (in_escape) {
        if (c >= 0x40 && c <= 0x7e) in_escape = false;   /* CSI final byte */
        return;
    }
    if (c == 0x1b) { in_escape = true; return; }

    /* 2. Immediate stop. The only key that acts without Enter. */
    if (c == '.') { cmd_stop(); return; }

    /* 3. Submit on CR or LF. Empty line does nothing. */
    if (c == '\r' || c == '\n') {
        if (cmd_len) {
            cmd[cmd_len] = '\0';
            dbg_execute(cmd);
            cmd_len = 0;
        }
        cmd_redraw();
        return;
    }

    /* 4. Backspace. */
    if (c == '\b' || c == 0x7f) {
        if (cmd_len) { cmd_len--; cmd_redraw(); }
        return;
    }

    /* 5. Printable characters only. Silently ignore everything else. */
    if (c < 0x20 || c > 0x7e) return;
    if (cmd_len < CMD_MAX) { cmd[cmd_len++] = c; cmd_redraw(); }
}
```

Five branches, in that order, and nothing else. No branch above step 3 may
command motion except step 2.

**Both CR and LF submit**, and an empty line does nothing — so a CRLF pair
submits once and the LF falls through harmlessly. That replaces the
`prompt_swallow_lf` machinery entirely.

### Echo

`cmd_redraw()` rewrites the whole command line in place on every keystroke:

```c
static void cmd_redraw(void)
{
    cmd[cmd_len] = '\0';
    dbg_out_printf("\033[s\033[%u;1H Command: %s\033[K\033[u", CMD_ROW, cmd);
}
```

That is the echo — the operator sees the line because it is redrawn, not
because individual characters are echoed. About 60 bytes per keystroke, which
is nothing.

---

## Layout

`Command:` at the bottom, information at the top, commands listed in between.

```
 luftfugl 1.0.0                                              up 00:04:12
───────────────────────────────────────────────────────────────────────────────
  STATE    IDLE              POSITION  between 3 and 4       ADC   1824
  TARGET   --                ERROR     --                    ANGLE 160.3°
  FAULTS   0                 DUTY      0                     DIR   stopped
  STEP     100 counts        SELECTED  none                  SIM   off

  STATIONS    1: 372      2: 738      3: 1309     4: 2047     5: 2815
              type "sel 1" to start setting up station 1
───────────────────────────────────────────────────────────────────────────────
  adc            show reading        jog +100       move forward
  status         show all state      jog -100       move back
  angle          show angle          step 250       set jog size
  angle 60       move to 60 deg      goto 1260      move to reading 1260
  sel 3          select station 3    save           store selected station
  stations       show table          export         print config lines
  move 2         go to station 2     home           go to station 1
  stop           stop now (or ".")   clearfault     clear a fault
  selftest       run checks          pins           show pin states
  sim on         simulation on       arm            allow manual drive
  cfg            list settings       help adc       detail for one command
───────────────────────────────────────────────────────────────────────────────
 Command: sel 1_
───────────────────────────────────────────────────────────────────────────────
 00:04:07  sel 1        station 1 selected, stored 372
 00:04:01  adc          raw 1824 avg 1824 pos between 3 and 4
```

**Every command on screen carries a working example**, in two columns. Nothing
is shown as `<argument>` syntax — `angle 60`, not `angle <degrees>`.

The guidance line under STATIONS is computed from the selected station and the
current reading, and tells the operator the next action in plain words.

Results appear newest-first below the command line.

---

## Command list

All of these must exist. Any that do not, add.

`adc` `status` `angle` `angle <n>` `goto <n>` `jog +n` `jog -n` `step <n>`
`sel <n>` `save` `save <n>` `stations` `export` `move <n>` `home` `stop`
`clearfault` `faults` `selftest` `tick` `pins` `pwm` `sim on` `sim off`
`sim adc <n>` `arm` `disarm` `drive <dir> <duty> <ms>` `findmin` `cfg`
`cfg <key> <val>` `plain` `help` `help <cmd>` `exit`

`help` lists all of them with examples. `help <cmd>` gives Examples, Limits and
Notes in plain language.

Exact matches always win over prefix matches — `sel` resolves to `sel`, never
ambiguous with `selftest`. That fix is already in; keep it.

---

## Verification

Report actual console output, captured with `picocom --logfile`:

1. Typing `adc` — each character appears after `Command:` as typed
2. Enter — the result line appears, the command line clears
3. `sel 1` — accepted, marker moves, guidance line updates
4. `jog +100` — `moving`, then the resulting reading
5. `xyz` — `rejected: no command called "xyz", try "help"`
6. Backspace mid-line — the character disappears from the display
7. `.` — immediate stop
8. The command line stays clean for two minutes with the renderer running —
   no `[16;1H` or other sequence accumulating

Item 8 is the one that has failed repeatedly. Item 1 is the one that has never
worked.

Do not run the motor beyond what item 4 and 7 require, and report rather than
run if unsure.
