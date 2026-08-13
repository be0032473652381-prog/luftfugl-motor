# Task — Help Inside the Console, Not Over It

`help` currently takes over the whole screen and hides the status. The operator
loses sight of the mechanism at exactly the moment they are deciding what to do
with it.

Show help **within** the console instead.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## Layout

Status stays. The command region grows to fill the space the results normally
occupy, and the results region shrinks to three lines. Nothing scrolls away,
nothing is hidden.

```
 luftfugl 1.0.0                                              up 00:05:08
───────────────────────────────────────────────────────────────────────────────
  STATE  IDLE          POSITION  station 3        ADC   1344   ANGLE 118.1
  TARGET --            ERROR     +35              STEP  100    DIR   stopped
  FAULTS 0             SELECTED  none             SIM   off

  STATIONS  1:372  2:738  3:1309  4:2047  5:2815
            type "sel 1" to start setting up station 1
───────────────────────────────────────────────────────────────────────────────
 COMMANDS   1/3   "help 2" next page   "help jog" detail

   adc            show the reading            adc
   angle          show the angle              angle
   stations       the five stored positions   stations
   limits         the range allowed           limits
   jog +100       move forward                jog +100
   jog -250       move back                   jog -250
   step 250       change the jog size         step 250
   pos 3          go to a station             pos 3
───────────────────────────────────────────────────────────────────────────────
 Command: _
───────────────────────────────────────────────────────────────────────────────
 00:05:05  adc          raw 1344 avg 1344 pos station 3
 00:05:01  sel 1        station 1 selected
```

The command region is the **only** thing that changes. Status, command line and
results all stay exactly where they are.

## Paging

`help` shows page 1. `help 2`, `help 3` show the rest. The header says
`COMMANDS 1/3` so the operator knows there is more.

Three pages, grouped by intent:

| Page | Contents |
|------|----------|
| 1 | Seeing where you are, and moving |
| 2 | Setting up the five stations, faults |
| 3 | Diagnostics, simulation, manual drive, settings |

## Detail, also inline

`help jog` replaces the same region:

```
───────────────────────────────────────────────────────────────────────────────
 HELP: jog                                      "help" back to the list

   Moves the mechanism a small amount, without going to a station.

   jog +100     forward 100 counts, about 9 degrees
   jog -250     back 250 counts, about 22 degrees

   size    10 to 500 counts        speed   creep, the slowest safe speed
   range   stays inside 272..2915  time    gives up after 3 seconds

   One count is about 0.09 degrees. "step 250" sets the default size.
   See also: step, goto, pos, stations
───────────────────────────────────────────────────────────────────────────────
```

Fits the region without paging. Limits **read from the live constants**, so
changing `JOG_MAX_COUNTS` with `cfg` changes the help text too.

## Returning

`help` alone returns to page 1 of the list. Any other command runs normally and
leaves the region showing whatever it was showing — help does not need
dismissing, because it never covered anything.

---

## The default view

When the operator has not asked for help, the command region shows the eight
commands relevant to the current state, chosen the same way as before:

| State | Show |
|-------|------|
| No station selected | `sel 1` `adc` `angle` `stations` `limits` `pos 3` `goto 1260` `help` |
| Station selected, away from it | `jog +100` `jog -100` `step 250` `save` `stations` `adc` `sel 2` `help` |
| Station selected, at it | `save` `sel 2` `export` `stations` `jog +100` `adc` `pos 3` `help` |
| In fault | `clearfault` `status` `faults` `adc` `stations` `limits` `home` `help` |

Always eight, always ending with `help`.

---

## Constraints

- The status region and the command line never move or disappear.
- Redraw only the command region when help changes, not the whole frame.
  A full repaint is 2 KB and 174 ms at 115200; the region alone is about 700
  bytes.
- All output through the existing non-blocking path.
- Every command that exists appears somewhere in the three pages, and has a
  `help <name>` entry.

## Verification

Report actual console output for:

1. `help` — page 1 with status still visible above it
2. `help 2` and `help 3`
3. `help jog` — detail inline, status still visible
4. `help` after that — back to page 1
5. `adc` while help is showing — the result appears in the results region and
   help stays put
6. The default view changing when a station is selected
7. `cfg JOG_MAX_COUNTS 300` then `help jog` — the limit shown updates

Item 7 proves the help is reading live values rather than fixed text.

Do not run the motor.
