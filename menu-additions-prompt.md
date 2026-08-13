# Task — Add Position Commands and Two Info Listings

The console now accepts input. Add the commands needed to drive to a position
and to see the configured values.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## 1. `pos <1-5>` — go to a station

Closed-loop move to a station's stored ADC value, through the existing
controller path with limit enforcement.

```
> pos 3
 pos 3        moving to station 3, adc 1309
 pos 3        arrived, adc 1311, 2 counts from target
```

- Rejects `pos 0`, `pos 6` and non-numeric with
  `rejected: station must be 1 to 5`
- Rejects while another motion is running with `rejected: already moving`
- Rejects in fault with `rejected: faulted, use clearfault`
- On completion, report the achieved reading and the error in counts

If a `move <n>` command already exists with this behaviour, make `pos` an alias
rather than duplicating the logic, and say so.

## 2. `goto <adc>` — go to a raw reading

```
> goto 1260
 goto 1260    moving, 586 counts back in 2 steps
 goto 1260    step 1 of 2, adc 1391
 goto 1260    step 2 of 2, adc 1262
 goto 1260    done, adc 1262, 2 counts from target
```

Decomposed into bounded jogs of at most `JOG_MAX_COUNTS`, each validated
against the safe range before it starts, issued one at a time from the main
loop with the next only after the previous completes. `stop` cancels the
remainder.

- Rejects outside the safe range:
  `rejected: 3200 is outside the safe range 272 to 2915`
- Rejects a delta below `JOG_MIN_COUNTS`:
  `rejected: already within 10 counts of 1260`

**Do not add a controller request that bypasses the safe-range check.** Each
step is an ordinary `controller_request_jog()` call.

---

## 3. `limits` — the ADC range

```
> limits
 ADC RANGE
   hardware        0 .. 4095      12-bit, 0 to 3.3 V
   safe range      272 .. 2915    2643 counts, 232.4 deg
   margin below    100 counts below station 1
   margin above    100 counts above station 5
   current         1746           153.5 deg
   position window 80 counts      7.0 deg either side of a station
   jog range       10 .. 500 counts
```

Every figure read from the live constants, not hardcoded. Degrees computed as
`adc * 360 / 4095` for a 360-degree potentiometer.

## 4. `stations` — the five values

```
> stations
 STATIONS                 stored      angle      from here
   1                        372       32.7 deg    -1374
   2                        738       64.9 deg    -1008
   3                       1309      115.1 deg     -437
   4                       2047      179.9 deg     +301
   5                       2815      247.4 deg    +1069
   current                 1746      153.5 deg
   spacing              366  571  738  768 counts
   selected                none
```

The `from here` column is the signed delta from the current reading — so the
operator can see at a glance which way and how far to each station. The
`spacing` row shows the gaps, which makes an uneven or mis-measured table
obvious.

If a station is within `POS_WINDOW`, mark that row `<-- here`.

---

## Screen

Add to the command list on screen, keeping the two-column layout with working
examples:

```
  pos 3          go to station 3     goto 1260      go to reading 1260
  limits         show adc range      stations       show station table
```

Update `help` and `help <cmd>` for all four, with Examples, Limits and Notes.
Notes in plain language — "one count is about 0.09 degrees", not a ratio.

---

## Verification

Report actual console output for:

1. `stations` — the full table, with `from here` correct against the current
   reading
2. `limits` — every value matching the constants in `config.h`
3. `pos 9` — `rejected: station must be 1 to 5`
4. `goto 3200` — `rejected: 3200 is outside the safe range 272 to 2915`
5. `goto 1260` from more than 500 counts away — the step plan and each step
6. `stop` during a multi-step `goto` — remaining steps not issued

Item 6 is the safety property. Demonstrate it.

Report rather than run anything that moves the mechanism if you are unsure.
