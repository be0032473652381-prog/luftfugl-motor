# Answer — Command Block Resolved

Your objections are all correct. My sample block had duplicates (`jog`, `cfg`,
`help` twice), listed two commands that do not exist (`faults`, `angle 60` as a
separate entry), and 30 cells cannot hold 32 commands. Here is the resolution.

---

## Rows 9–18 are re-allocated

Instead of ten rows of three name-plus-description cells, use:

**Rows 9–13: the command names, seven per row.** Names only, no descriptions —
32 commands fit in 35 cells with room to spare.

```c
"  %-9s %-9s %-9s %-9s %-9s %-9s %-9s"
```

**Rows 14–18: five example lines**, showing the commands that take arguments,
since those are the ones a name alone does not explain.

```c
"  %-24s %-24s %-24s"
```

Total still rows 9–18. Row allocation unchanged.

## Rows 9–13 — every command, grouped by row

Group by purpose so the rows mean something. Order within a row does not
matter; completeness does.

```
  adc       angle     status    stations  limits    cfg       diag
  jog       step      pos       move      goto      home      stop
  sel       save      export    reset     bootsel   plain     exit
  selftest  pins      pwm       tick      trace     findmin   help
  arm       disarm    drive     sim
```

That is 32 cells for 32 commands. **Reconcile this against the actual parser
table.** If a command exists that is not listed, add it to the last row. If one
is listed that does not exist, remove it and tell me which. Report both counts.

## Rows 14–18 — worked examples

Only the commands whose usage is not obvious from the name:

```
  jog +100                 pos 3                    goto 1260
  step 250                 sel 1                    cfg DUTY_NORMAL 30
  angle 60                 move 2                   drive fwd 25 200
  help jog                 sim on                   save 3
  export                   selftest                 reset
```

Three columns of 24 characters. Every one typeable exactly as printed.

## Header rows

Row 8 is the rule. Put a one-line heading immediately below the rule inside the
block if it fits without stealing a row — otherwise omit headings entirely.
Completeness beats decoration.

---

## Everything else stands

Row allocation, format strings for rows 1–7 and 19–24, the static-versus-live
table, the guidance line, and the `ESC[K` after every row are all unchanged.

The command line stays alone on row 20. The welcome text is still deleted.

## Verification, unchanged plus one

1. ADC at three digits — every column in the same place
2. A long value in SEL — no field shifted
3. Row 20 with a partly typed command, alone on its row
4. **Command count in the parser versus count displayed — state both numbers**
5. Bytes for the initial draw and for one field update

Item 4 is what caught this. Report it explicitly rather than asserting they
match.
