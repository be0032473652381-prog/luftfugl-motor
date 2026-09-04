# Task — Design the Console Properly

The screen has grown by accretion: fields drift out of alignment, the command
line sits in the middle of the text, and most commands have no example. Lay it
out deliberately.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## The layout

Exactly 79 columns wide, 24 rows. Every column position below is deliberate —
implement it as written.

```
┌─ luftfugl 2.0 ──────────────────────────────────────────── up 00:03:16 ─┐
│                                                                          │
│   STATE    IDLE              ADC     1253         ANGLE    110.2 deg     │
│   TARGET   --                ERROR   --           DUTY     0             │
│   STATION  between 4 and 5   SELECTED none        DIR       stopped      │
│                                                                          │
│   1: 200        2: 525        3: 850        4: 1175       5: 1500        │
│   17.6 deg      46.2 deg      74.7 deg      103.3 deg     131.9 deg      │
│                                                                          │
│   ▸ type "sel 1" to start setting up station 1                           │
├──────────────────────────────────────────────────────────────────────────┤
│   LOOK              MOVE                    SET UP                       │
│   adc               jog +100                sel 1                        │
│   angle             jog -100                save                         │
│   status            step 250                export                       │
│   stations          pos 3                   cfg                          │
│   limits            goto 1260               cfg DUTY_NORMAL 30           │
│   faults            angle 60                                             │
│                     home                    CHECK                        │
│   HELP              stop                    selftest                     │
│   help              reset                   pins    pwm    tick          │
│   help jog                                  arm     drive  findmin       │
├──────────────────────────────────────────────────────────────────────────┤
│ ▶ pos 3_                                                                 │
├──────────────────────────────────────────────────────────────────────────┤
│   00:03:11  pos 3       arrived, adc 848, 2 counts from target           │
│   00:03:05  cfg         DUTY_NORMAL 30  DUTY_APPROACH 20                 │
│   00:02:58  sel 3       station 3 selected, stored 850                   │
└──────────────────────────────────────────────────────────────────────────┘
```

### Alignment rules, not suggestions

**Status block.** Three columns at fixed positions: labels at column 4, 22 and
39; values at column 13, 31 and 48. Every label left-aligned, every value
left-aligned in its own column. Nothing shifts as values change length — pad
to a fixed field width and clear the remainder.

**Stations.** Five columns at columns 4, 17, 30, 43 and 56. Value on one row,
angle directly beneath it, so each pair reads as a unit. The selected station
is marked, and if the mechanism is within `POS_WINDOW` of one, that value is
shown in reverse video.

**Commands.** Three columns at columns 4, 22 and 44, under headings `LOOK`,
`MOVE`, `SET UP`, `CHECK`, `HELP`. Grouped by what the operator is doing, not
alphabetically. **Every entry is a typeable example** — `jog +100`, `pos 3`,
`cfg DUTY_NORMAL 30`, `help jog`. Never `<argument>` syntax anywhere.

**The command line is on its own row**, between two rules, prefixed `▶`. It is
never adjacent to descriptive text.

**Results** are three lines, newest at the top, with time at column 4, command
at column 14, outcome at column 27.

### Characters

Use box-drawing characters for the frame and rules: `┌ ┐ └ ┘ ─ │ ├ ┤`. If the
terminal cannot render them the display degrades but stays readable — that is
acceptable, and `plain` mode remains for terminals that cannot.

`▸` marks the guidance line, `▶` the command prompt. Both single characters.

---

## The guidance line

One line, at row 10, computed from the current state. It is the only thing on
screen that tells the operator what to do next, so it has to be right.

| Condition | Line |
|---|---|
| Nothing selected | `type "sel 1" to start setting up station 1` |
| Selected, mechanism away | `jog toward station 3 — 400 counts to go` |
| Selected, within the window | `at station 3 — type "save" to store it` |
| Moving | `moving to station 3 — 180 counts to go` |
| All five saved | `type "export" and copy the lines into config.h` |
| Motor not responding | `no movement — try "cfg DUTY_NORMAL 40"` |

---

## Every command listed

Audit the command table. **Every command that exists appears in the block
above.** If one does not fit, use a fourth column or a second `CHECK` row
rather than omitting it.

Report the count of commands that exist and the count displayed. They must
match.

---

## Rendering

- The frame, headings and command block are **static** — drawn once, never
  redrawn.
- Status values, station markers, the guidance line and the results are the
  only live regions.
- Each live field is written at its fixed address, padded to a fixed width, and
  only when its value changes.
- All output through the existing non-blocking path.

Report the byte cost of a full draw and of a typical field update.

---

## Verification

Report the actual screen after reset, as text, plus:

1. Column alignment holding when `ADC` goes from 4 digits to 3
2. The guidance line in three different states
3. The command count matching the number that exist
4. `help jog` still working
5. Bytes per full draw and per field update

Item 1 is where the current version fails — values of different lengths shift
the fields beside them.
