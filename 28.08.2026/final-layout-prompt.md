# Task — Final Console Layout

The current screen has no column grid: `STATE IDLE       ADC 251` is one
string, so `ADC 1251` shifts everything after it. Station angles do not sit
under their values. Results are truncated mid-word.

This specification has been laid out and verified at 79 columns. Implement the
format strings literally.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## The screen

```
 luftfugl 2.0                                                    up 00:00:30
───────────────────────────────────────────────────────────────────────────────
  STATE    IDLE          TARGET   --            DIR      stopped
  ADC      251           ANGLE    22.1 deg      DUTY     0
  ERROR    --            STEP     100           SELECTED none

  1:  200        2:  525        3:  850        4: 1175        5: 1500
  17.6 deg       46.2 deg       74.7 deg       103.3 deg      131.9 deg

  ▸ type "sel 1" to start setting up station 1
───────────────────────────────────────────────────────────────────────────────
  adc       angle     status    stations  limits    cfg       diag
  jog       step      pos       move      goto      home      stop
  sel       save      export    reset     bootsel   plain     exit
  selftest  pins      pwm       tick      trace     findmin   help
  arm       disarm    drive     sim

  jog +100                 pos 3                    sel 1
  goto 1260                cfg DUTY_NORMAL 30       save 3
  drive fwd 25 200         help jog                 export
───────────────────────────────────────────────────────────────────────────────
 ▶ diag
───────────────────────────────────────────────────────────────────────────────
  00:00:15  diag       frame=2547 B  field=85 B  draws=1
  00:00:12  pos 3      arrived  ADC 848  70.2 deg  err -2
  00:00:00  event      ARR:1
```

## Row allocation — 24 rows exactly

| Row | Contents |
|-----|----------|
| 1 | Title, uptime |
| 2 | Rule |
| 3–5 | Status, three rows of three label/value pairs |
| 6 | Blank |
| 7 | Station values |
| 8 | Station angles |
| 9 | Blank |
| 10 | Guidance |
| 11 | Rule |
| 12–16 | Command names, seven per row |
| 17 | Blank |
| 18–20 | Worked examples, three per row |
| 21 | Rule |
| 22 | Command line, alone |
| 23 | Rule |
| 24 | Results — see below |

The two blank rows at 6, 9 and 17 are deliberate breathing space. Do not fill
them.

## Format strings — verified at 79 columns

```c
/* row 1 */
" luftfugl 2.0%52sup %02u:%02u:%02u"

/* rows 2, 11, 21, 23 */
"───────────────────────────────────────────────────────────────────────────────"

/* rows 3-5  — label 9, value 14, three pairs */
"  %-9s%-14s%-9s%-14s%-9s%-14s"

/* rows 7-8  — five columns of 15 */
"  %-15s%-15s%-15s%-15s%-15s"

/* row 10 */
"  ▸ %s"

/* rows 12-16 — seven columns of 10 */
"  %-10s%-10s%-10s%-10s%-10s%-10s%-10s"

/* rows 18-20 — three columns of 25 */
"  %-25s%-25s%-25s"

/* row 22 */
" ▶ %s"

/* results */
"  %-8s  %-11s%s"
```

**The label is a separate field from its value.** That is the whole point —
`ADC` in a 9-wide field, `251` or `1251` in a 14-wide field, so a longer value
cannot move the field beside it.

Station values right-aligned to 4 digits (`1:  200`, `4: 1175`) so the digits
line up. Angles left-aligned directly beneath.

**Clear to end of line (`ESC[K`) after writing every row.**

## Results region

Row 24 is the newest result. When a second arrives, the region scrolls upward —
use a scrolling region of rows 24 to the bottom of the terminal, so results
scroll below the fixed screen rather than pushing it up.

If the terminal is taller than 24 rows the extra space becomes result history,
which is a bonus rather than a requirement.

**Two blank lines are emitted before the first result after a reset**, so the
fixed screen and the scrolling log are visually separate.

## After a move completes

Every motion command reports position and reading on completion:

```
  00:00:12  pos 3      arrived  ADC 848  70.2 deg  err -2
  00:00:19  jog +100   done     ADC 951  83.6 deg
  00:00:24  goto 1260  done     ADC 1262  110.9 deg  err +2
```

Format: outcome, then `ADC <value>`, then the angle to one decimal, then the
signed error against the target where there was one. Fixed columns so the
values line up down the log.

## Guidance line, row 10

| State | Text |
|---|---|
| Nothing selected | `type "sel 1" to start setting up station 1` |
| Selected, away | `jog toward station 3 — 400 counts to go` |
| Selected, in window | `at station 3 — type "save" to store it` |
| Moving | `moving to station 3 — 180 counts to go` |
| All five saved | `type "export" and copy the lines into config.h` |

## Static versus live

| Region | Redrawn |
|---|---|
| 1–2, 11, 12–20, 21, 23 | Once on entry |
| 3–5 | Per field, on change only |
| 7–8 | When a station value or the current reading changes |
| 10 | When the guidance changes |
| 22 | Every keystroke |
| 24+ | On each result |

---

## Verification

Report the literal screen after reset, then:

1. The same with ADC at four digits — every column unmoved
2. The same with `SELECTED station 3` — nothing shifted
3. A completed `pos 3`, showing the result line with ADC, angle and error
4. Row 22 with a partly typed command, alone between its rules
5. Command count in the parser versus displayed — both numbers

Items 1 and 2 are the current failures.
