# Task — Show Every Command on Screen, Two Columns

The command region shows six commands out of about thirty. The operator cannot
see what exists without typing `help`, and `help` covers the screen.

Show them all, permanently, in two columns.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## Layout

Compress the status region to make room. It currently uses four rows for
twelve fields; three rows is enough. The stations line and the guidance line
stay — the guidance line is the most useful thing on the screen.

```
 luftfugl 1.0.0                                              up 00:03:16
───────────────────────────────────────────────────────────────────────────────
 STATE  IDLE      POS  between 4 and 5   ADC 1253   ANGLE 110.2   DIR stopped
 TARGET --        ERR  --                DUTY 0     STEP 100      SEL none
 STATIONS  1:200  2:525  3:850  4:1175  5:1500        LIMITS none
           type "sel 1" to start setting up station 1
───────────────────────────────────────────────────────────────────────────────
 adc          reading now        stop         stop now
 angle        angle now          sel 1        choose a station to set
 status       everything         save         store reading as that station
 stations     the five values    export       print lines for config.h
 limits       range and window   cfg          list or change settings
 jog +100     move forward       pins         pin states
 jog -100     move back          pwm          pwm frequency
 step 250     set jog size       tick         loop timing
 pos 3        go to a station    selftest     run checks
 goto 1260    go to a reading    faults       fault history
 angle 60     go to an angle     sim on       simulation
 home         go to station 1    arm          allow manual drive
 reset        restart the board  help jog     detail on one command
───────────────────────────────────────────────────────────────────────────────
 Command: _
───────────────────────────────────────────────────────────────────────────────
 00:03:11  pos 3        arrived, adc 848, 2 counts from target
 00:03:05  cfg          DUTY_NORMAL 30  DUTY_APPROACH 20  APPROACH 300
```

Left column: seeing where you are, and moving. Right column: setting up,
diagnostics, and everything else. Roughly the order an operator needs them.

**Every entry is a working example**, not syntax. `jog +100`, not
`jog <±counts>`. `pos 3`, not `pos <n>`. `help jog`, not `help <cmd>`.

## Command count

There are around thirty commands. **Audit the command table and list every one
of them.** If a command exists that does not appear, add it. If one is listed
that does not exist, remove it and say which.

Report the count and confirm the two numbers match.

If they genuinely do not fit in two columns, use three narrower ones rather
than omitting any. The requirement is that everything is visible.

## Static

This region no longer changes with state — it is drawn once with the frame and
never redrawn. That removes the state-dependent command list entirely, and with
it the redraw traffic it generated.

The guidance line under STATIONS still adapts, and it now carries the whole
burden of telling the operator what to do next. Make sure it is good:

| Condition | Line |
|---|---|
| No station selected | `type "sel 1" to start setting up station 1` |
| Selected, mechanism away | `jog toward station 3, 400 counts to go` |
| Selected, mechanism at it | `at station 3 — type "save" to store it` |
| All five saved this session | `type "export" and copy the lines into config.h` |
| Moving | `moving to station 3, 180 counts to go` |

## `help` unchanged

`help` and `help <name>` still give the detail — examples, limits, notes. The
on-screen list is for discovery; `help` is for depth.

---

## Verification

Report actual console output for:

1. The full screen after reset, with every command visible
2. The command count, and the count of commands that exist — matching
3. `help` still working
4. The guidance line changing as a station is selected and the mechanism moves
5. Frame bytes per full redraw, and confirmation the command region is not
   redrawn on state changes

Item 5 matters: the region is static, so it should cost nothing after the
initial draw.
