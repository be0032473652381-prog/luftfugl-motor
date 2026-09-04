# Task — Console Layout, Specified by Row and Column

Previous layout attempts drifted because they were described as pictures.
This one is specified as positions and format strings. Implement it literally.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## What is wrong now

```
 adc reading now        stop stop now          sel 1 choose station
 angle angle now       status everything      save 3 store station
```

No column structure — names and descriptions run together and every row starts
in a different place. And the welcome text has overwritten the command line:

```
   1. Type "sel 1" to choose station 1
> v
   3. Type "save" to store it
```

The command line must never share rows with anything else.

---

## Row allocation — exactly 24 rows, no more

| Row | Contents |
|-----|----------|
| 1 | Title and uptime |
| 2 | Rule |
| 3 | Status, line 1 |
| 4 | Status, line 2 |
| 5 | Station values |
| 6 | Station angles |
| 7 | Guidance line |
| 8 | Rule |
| 9–18 | Commands, 10 rows of 3 columns |
| 19 | Rule |
| 20 | Command line |
| 21 | Rule |
| 22–24 | Results, newest first |

Nothing may occupy a row not listed. **The welcome text is deleted** — the
guidance line on row 7 replaces it, and `help` covers the rest.

## Format strings — use these literally

Every row is produced by `snprintf` with explicit field widths. That is what
makes the columns hold when values change length.

```c
/* row 1 */
"%-56s up %02u:%02u:%02u"

/* row 2, 8, 19, 21 — rule */
"───────────────────────────────────────────────────────────────────────────────"

/* row 3 */
"  STATE %-10s ADC %-6u ANGLE %-7s DIR %-8s"

/* row 4 */
"  TARGET %-9s ERR %-6s DUTY %-6u STEP %-5u SEL %-5s"

/* row 5 */
"  1:%-9u 2:%-9u 3:%-9u 4:%-9u 5:%-9u"

/* row 6 */
"    %-9s   %-9s   %-9s   %-9s   %-9s"        /* angles, under each value */

/* row 7 */
"  > %-74s"                                    /* guidance */

/* rows 9-18, three columns */
"  %-12s %-13s %-12s %-13s %-12s %-13s"

/* row 20 */
"  %s%-70s"                                    /* prompt then the typed line */

/* rows 22-24 */
"  %02u:%02u:%02u  %-12s %-48s"
```

Every `%-Ns` pads to a fixed width, so a shorter value cannot pull the next
field left. **Clear each row to end of line after writing it** (`ESC[K`) so a
longer previous value cannot leave residue.

## The command block

Thirty commands, three columns, ten rows. Name then a short description, both
in fixed-width fields.

```
  adc          reading now   jog +100     move forward  sel 1        choose station
  angle        angle now     jog -100     move back     save         store station
  status       everything    step 250     set jog size  export       config.h lines
  stations     five values   pos 3        go to station cfg          list settings
  limits       range/window  goto 1260    go to reading cfg DUTY_NORMAL 30
  faults       fault history angle 60     go to angle   pins         pin states
  tick         loop timing   home         station 1     pwm          pwm frequency
  trace        last move     stop         stop now      selftest     run checks
  diag         uart counters reset        restart       sim on       simulation
  help         all commands  help jog     one command   arm          manual drive
```

Left column: information. Middle: motion. Right: setup and diagnostics.

**Every entry is typeable exactly as shown.** `jog +100`, not `jog <±counts>`.

If a command exists that is not in this block, add a row or a fourth column —
do not omit it. Report the count of commands that exist and the count shown;
they must match.

## Static versus live

| Region | Redrawn |
|--------|---------|
| Rows 1–2, 8, 9–18, 19, 21 | Once, on entry. Never again. |
| Rows 3–6 | Per field, only when that field's value changes |
| Row 7 | When the guidance changes |
| Row 20 | On every keystroke |
| Rows 22–24 | When a result arrives |

The command block is static, so it costs nothing after the first draw.

## Guidance line, row 7

| State | Text |
|-------|------|
| Nothing selected | `type "sel 1" to start setting up station 1` |
| Selected, away | `jog toward station 3 — 400 counts to go` |
| Selected, within window | `at station 3 — type "save" to store it` |
| Moving | `moving to station 3 — 180 counts to go` |
| All five saved | `type "export" and copy the lines into config.h` |

---

## Verification

Report the screen as literal text after reset, then:

1. The same screen with ADC at a 3-digit value — every column in the same place
2. The same with a long station name in SEL — no field shifted
3. Row 20 with a partly typed command, showing it alone on its row
4. Command count existing versus displayed
5. Bytes for the initial draw, and for one field update

Item 1 and 2 are the failures in the current version. Show them fixed.
