# Task — A Real Help System

The screen shows six commands. There are around thirty. Anyone who does not
already know the command names cannot discover them.

Build a help system that makes every command findable, with a worked example
and its limits.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## `help` — the full list

Takes over the whole screen, replacing the frame until dismissed. Paged if it
does not fit 24 rows: `-- more, press space --` at the bottom, space for the
next page, `q` to return to the console.

Grouped by what the operator is trying to do, not alphabetically:

```
 HELP  page 1 of 2                                    space = more, q = back
───────────────────────────────────────────────────────────────────────────────
 SEEING WHERE YOU ARE
   adc                    show the reading now              adc
   angle                  show the angle now                angle
   status                 show everything                   status
   stations               the five stored positions         stations
   limits                 the range the motor may move in   limits

 MOVING
   jog +n                 move forward n counts             jog +100
   jog -n                 move back n counts                jog -250
   step n                 change the default jog size       step 250
   pos n                  go to a stored station            pos 3
   goto n                 go to an exact reading            goto 1260
   angle n                go to an exact angle              angle 60
   home                   go to station 1                   home
   stop                   stop now                          stop

 SETTING UP THE FIVE STATIONS
   sel n                  choose which station to set       sel 1
   save                   store the reading as that station save
   export                 print lines for config.h          export
───────────────────────────────────────────────────────────────────────────────
```

Page 2 covers faults, diagnostics, simulation, manual drive and settings, in
the same shape.

**Four columns: name, what it does in plain words, and a working example.**
Never `<argument>` syntax on this screen — `jog +100`, not `jog <±counts>`.

## `help <command>` — the detail

```
 HELP: jog                                                        q = back
───────────────────────────────────────────────────────────────────────────────
 Moves the mechanism by a small amount, without going to a station.

 EXAMPLES
   jog +100        move forward 100 counts, about 9 degrees
   jog -250        move back 250 counts, about 22 degrees
   jog +10         the smallest move allowed

 LIMITS
   size            10 to 500 counts, either direction
   speed           creep only, the slowest safe speed
   range           will not move outside 272 to 2915
   time            gives up after 3 seconds

 NOTES
   One count is about 0.09 degrees.
   The default size is set with "step", so "step 250" then "jog +" repeats it.
   Type "stop" or "." at any time.

 SEE ALSO
   step, goto, pos, stations
───────────────────────────────────────────────────────────────────────────────
```

Every entry has Examples, Limits, Notes and See Also. Notes in plain language —
"about 9 degrees", never "13.6 counts per degree".

**Limits are read from the live constants**, not hardcoded text. If
`JOG_MAX_COUNTS` changes, the help changes with it. That is the difference
between help that stays true and help that rots.

## Unknown command

```
> jgo +100
 rejected: no command called "jgo"
           did you mean: jog ?
           type "help" for the full list
```

Suggest the closest match by simple prefix or single-character difference. If
nothing is close, just point at `help`.

## `help` with a partial name

```
> help j
 commands starting with "j":  jog
 type "help jog" for detail
```

---

## The on-screen command list

The main screen keeps six commands, but they should be the six that matter
right now, chosen by state:

| State | Show |
|-------|------|
| No station selected | `sel 1`, `adc`, `stations`, `limits`, `pos 3`, `help` |
| Station selected, mechanism away from it | `jog +100`, `jog -100`, `step 250`, `save`, `stations`, `help` |
| Station selected, mechanism at it | `save`, `sel 2`, `jog +100`, `export`, `stations`, `help` |
| In fault | `clearfault`, `status`, `faults`, `adc`, `limits`, `help` |

Always ending with `help` so the way out is never hidden.

---

## Coverage

Every command that exists must appear in `help` and have a `help <command>`
entry. Include at minimum:

`adc` `angle` `status` `stations` `limits` `jog` `step` `pos` `goto` `home`
`stop` `sel` `save` `export` `clearfault` `faults` `selftest` `tick` `pins`
`pwm` `sim` `arm` `disarm` `drive` `findmin` `cfg` `plain` `help` `exit`

If a command exists that is not in that list, add it too. If one is listed but
does not exist, say so rather than inventing it.

---

## Verification

Report actual console output for:

1. `help` — page 1, then space, then page 2
2. `help jog` — the full detail
3. `help sel` — resolves to `sel`, not ambiguous with `selftest`
4. `help j` — the partial-name list
5. `jgo +100` — the suggestion
6. `help` for a command whose limits come from `config.h`, showing the live
   value
7. The on-screen list changing when a station is selected

Item 6 matters: change `JOG_MAX_COUNTS` with `cfg` and show the help text
changing with it.

Do not run the motor.
