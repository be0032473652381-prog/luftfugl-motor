# Task — Preset the Five Station Values

Replace the station values in `config.h`. The current ones were inherited from
the v1 reed-switch resistor ladder and have no relationship to this mechanism.

Do not use `sed -i` by line number or `perl -0pi -e`. Use `apply_patch`.

---

## The values

Evenly spaced 30° apart, with a 10° offset so station 1 has margin below it
rather than sitting at ADC 0.

```c
#define POS_1_ADC  114   /*  10 deg */
#define POS_2_ADC  455   /*  40 deg */
#define POS_3_ADC  796   /*  70 deg */
#define POS_4_ADC 1138   /* 100 deg */
#define POS_5_ADC 1479   /* 130 deg */
```

Derived as `degrees * 4095 / 360` for a 360° single-turn potentiometer wired
0 V to 3.3 V.

Replace the old values: 372, 738, 1309, 2047, 2815.

## Why the offset

Station 1 at 0° would sit at ADC 0, the very bottom of the range. Any
undershoot wraps to 4095, which the firmware would read as the opposite end of
travel. The 10° offset gives 114 counts of margin below station 1 and removes
that failure mode.

## Check that still holds

- Spacing is 341 counts throughout, evenly.
- Quarter of the smallest gap is 85 counts, so `POS_WINDOW` at 80 remains
  valid — but only just. If `POS_WINDOW` is ever raised above 85 the validation
  will reject the table.
- Span from station 1 to station 5 is 1365 counts, 120°.

Report whether any other constant now conflicts. `APPROACH_COUNTS` at 200 is
more than half a station gap, which would mean the controller is in approach
mode for most of any single-station move — say whether that matters and what
you would set it to.

## Also update

- Any comment or table in `config.h` quoting the old values
- The `stations` command output, if it hardcodes anything
- The default `cfg` values, so `reset stations` restores these rather than the
  old ones

Do not edit `v2-sensing.md`; report which of its tables are now stale and I
will amend them.

## Verification

Report actual console output for:

1. `stations` — all five values with angles 10, 40, 70, 100, 130
2. `limits` — the range and window figures consistent with the new spacing
3. `pos 3` — moves to 796 and reports the achieved reading
4. `pos 1` then `pos 5` — the full span, both completing

Report the build size.
