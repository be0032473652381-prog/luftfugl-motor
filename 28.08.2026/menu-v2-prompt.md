# Task — Operator-Friendly Debug Console

The current screen shows status and a prompt but nothing about what to type.
Anyone who does not already know the command names is stuck.

Rebuild it so a non-technical operator can sit down, read the screen, and work
the mechanism without a manual. This supersedes the layout in the previous
redesign and in `debug-functions.md` §15.5.

Do not use `sed -i` by line number or `perl -0pi -e` on source files. Use
`apply_patch` with context.

---

## The rule this hangs on

**Every command visible on screen carries a worked example.** Not a syntax
summary — an actual line the operator could type as-is. `jog +100` is useful;
`jog <±counts>` is not.

If a command cannot be shown with an example in the space available, it belongs
under `help`, not on the main screen.

---

## Layout, 80×24

```
 luftfugl 1.0.0                                              up 00:00:12
───────────────────────────────────────────────────────────────────────────────
  STATE   IDLE          POSITION  between 3 and 4      ADC  1891
  TARGET  --            ERROR     --                   STEP 100 counts
  FAULTS  0             DUTY      0                    DIR  stopped

  STATIONS   1: 372    2: 738    3: 1309  ▶ 4: 2047    5: 2815
             selected: 4     stored 2047     now 1891     off by -156
             jog forward to reach it
───────────────────────────────────────────────────────────────────────────────
 COMMANDS                                    type "help" for the full list
   jog +100        move forward 100 counts     jog -100    move back
   step 250        change jog size             sel 3       select station 3
   save            store this spot as st. 4    export      print all stations
   move 2          go to station 2             stop        stop now (or ".")
───────────────────────────────────────────────────────────────────────────────
 COMMAND
 > jog +100_
───────────────────────────────────────────────────────────────────────────────
 00:04:07  jog +100     done, now at 1991
 00:04:05  jog +100     moving...
 00:04:01  sel 4        station 4 selected, stored value 2047
```

### The guidance line is the point

Row 8 — `jog forward to reach it` — is what makes this operable by someone who
does not know the system. It is computed from the selected station and the
current reading:

| Condition | Line |
|-----------|------|
| More than `POS_WINDOW` below the station | `jog forward to reach it` |
| More than `POS_WINDOW` above | `jog back to reach it` |
| Within `POS_WINDOW` | `you are at station 4 — type "save" to store it` |
| No station selected | `type "sel 1" to start setting up station 1` |
| In fault | `type "clearfault" then "home"` |
| Outside safe range | `mechanism is past its limit — jog inward slowly` |

Always exactly one line. It tells the operator the next action, in plain words.

### Plain-language status

No internal identifiers on screen:

- `POSITION` shows `station 4`, `between 3 and 4`, or `unknown` — never `?` or
  `6`
- `DIR` shows `stopped`, `forward`, `back` — never `STP`
- `STEP` shows `100 counts`
- `ERROR` shows `-156` with the units implied by the neighbouring guidance line
- `▶` marks the selected station; `selected: none` when there is none

The command region shows the eight commands used during calibration, each with
a real example. Everything else lives in `help`.

---

## Commands

Typed in full, Enter to submit. Case insensitive. Unambiguous prefixes
accepted.

### Setting up positions — the primary workflow

| Command | Example | Effect |
|---------|---------|--------|
| `sel <n>` | `sel 3` | Select station n for setup |
| `jog +n` / `jog -n` | `jog +100` | Move by n counts, 10–500 |
| `step <n>` | `step 250` | Change the default jog size |
| `save` | `save` | Store the current reading as the selected station |
| `save <n>` | `save 3` | Store it as station n |
| `stations` | `stations` | Table with stored, current and difference |
| `export` | `export` | Paste-ready `#define POS_n_ADC` lines |
| `reset stations` | `reset stations` | Restore compiled defaults |

### Moving

| Command | Example | Effect |
|---------|---------|--------|
| `move <n>` | `move 2` | Closed-loop move to a station |
| `home` | `home` | Return to station 1 |
| `stop` | `stop` | Brake immediately |
| `.` | `.` | Immediate stop, no Enter needed |

### Checking

| Command | Example | Effect |
|---------|---------|--------|
| `status` | `status` | Full state |
| `adc` | `adc` | Raw and filtered reading |
| `faults` | `faults` | Last fault and counters |
| `clearfault` | `clearfault` | Clear a fault |
| `selftest` | `selftest` | Static checks |
| `tick` | `tick` | Loop timing and watchdog |
| `pins` | `pins` | Live pin states |
| `pwm` | `pwm` | PWM configuration and measured frequency |

### Advanced

| Command | Example | Effect |
|---------|---------|--------|
| `cfg` | `cfg` | List tunable constants |
| `cfg <key> <val>` | `cfg DUTY_CREEP 30` | Change one, in RAM |
| `sim on` / `sim off` | `sim on` | Simulation mode |
| `sim adc <n>` | `sim adc 2047` | Inject a reading |
| `sim travel <a> <b> <ms>` | `sim travel 1 5 300` | Simulated travel |
| `arm` / `disarm` | `arm` | Manual drive interlock |
| `drive <dir> <duty> <ms>` | `drive fwd 60 200` | Manual pulse, needs arm |
| `findmin` | `findmin` | Lowest duty that moves the motor |
| `plain` | `plain` | Switch to line-oriented mode |
| `exit` | `exit` | Leave the debug console |

---

## help

`help` with no argument lists every command in one column with its example.

`help <command>` gives detail:

```
> help jog
  jog — move the mechanism by a small amount

  Examples
    jog +100     move forward 100 counts
    jog -50      move back 50 counts

  Limits
    10 to 500 counts per command
    speed is fixed at creep, the slowest safe speed
    refuses to move past the safe range at either end

  Notes
    100 counts is roughly 7 degrees of travel.
    Use "step 250" then "jog +" to repeat a larger step.
```

Every `help` entry has Examples, Limits and Notes. Notes must be in plain
language — "roughly 7 degrees", not "13.6 counts per degree".

---

## Results

```
 00:04:07  jog +100     done, now at 1991
 00:04:05  jog +100     moving...
 00:04:01  sel 4        station 4 selected, stored value 2047
 00:03:58  jog +900     rejected: 900 is too far, the most is 500
 00:03:51  jgo +100     rejected: no command called "jgo", try "help"
```

Newest at the top so the operator does not chase a scrolling list.

**Every rejection says what was wrong and what to do**, in plain words. Not
`ERR: invalid jog` but `rejected: 900 is too far, the most is 500`.

Motion produces `moving...` then `done, now at <adc>`.

---

## First-run guidance

On entry, the results region starts with:

```
 Welcome. To set up the five stations:
   1. Type "sel 1" to choose station 1
   2. Use "jog +100" or "jog -100" until the mechanism is where you want it
   3. Type "save" to store it
   4. Repeat for stations 2 to 5
   5. Type "export" and copy the lines into config.h
 Type "help" at any time.
```

Cleared by the first command.

---

## Unchanged and still binding

- All output through the non-blocking ring. No debug path calls
  `uart_putc_raw()` directly.
- Static frame drawn once; live fields updated individually against a shadow
  copy, only on change.
- Frame draw emits `ESC[2J ESC[H ESC[?25l` first, scrolling region last.
- `.` is the only key acting without Enter, besides Escape clearing the line.
- Every motion goes through the existing bounded, safe-range-checked path.
- `plain` mode carries the same commands with no escape sequences.

---

## Verification

Report actual console output for:

1. Entry shows the welcome text and the command region with examples
2. `sel 3` updates the marker, the selected line and the guidance line
3. `jog +100` gives `moving...` then `done, now at <adc>`
4. `jog +900` gives `rejected: 900 is too far, the most is 500`
5. `jgo +100` gives `rejected: no command called "jgo", try "help"`
6. `help jog` shows Examples, Limits and Notes
7. The guidance line changes correctly as the reading crosses into and out of
   the selected station's window
8. Two minutes in the console with no watchdog reset

Do not run the motor.

---

## Note

`debug-functions.md` §14.2 and §15.5 are superseded. Do not edit those
documents; report that they need amending and I will do it.
