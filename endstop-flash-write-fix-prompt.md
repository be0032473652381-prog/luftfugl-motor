# Bug fix — endstop persistence writes an undersized flash record

## Confirmed defect

`endstop_persist()` (`src/debug.c`) passes a 16-byte record directly to
`flash_range_program()`. The RP2040 SDK requires flash writes in
multiples of `FLASH_PAGE_SIZE` (256 bytes). This is a real, existing
firmware defect, not a style concern — confirmed via direct review of
`hardware/flash.h`.

## Fix using the pattern already correct elsewhere in this codebase

Three other persistence implementations in this same codebase already
handle this correctly — confirmed via direct review:

- `power_monitor.c` (battery settings record), line ~37
- `co2.c` (CO₂ profile settings record), line ~29
- `event_timer.c` (event-timer interval record), line ~36

Each of these pads its record to fill exactly one flash page and
enforces this at compile time with a static assertion against
`FLASH_PAGE_SIZE`.

**Quote the exact padding and static-assertion pattern from one of these
three files first**, then apply the *same* pattern to the endstop
record and `endstop_persist()`/`endstop_restore()` — don't invent a
different fix. This is exactly the kind of "reuse an existing correct
pattern rather than build a parallel one" situation this project's own
`skill.md` calls out as a standing principle.

## What needs to change

1. Pad the endstop record structure to exactly `FLASH_PAGE_SIZE` bytes,
   matching whichever of the three reference implementations' approach
   is cleanest to replicate (explicit padding field, or a wrapping
   buffer — your call, based on what you find already in use).
2. Add the same compile-time static assertion confirming the padded
   size actually equals `FLASH_PAGE_SIZE`, matching the existing pattern
   exactly.
3. Update `endstop_persist()` to write the full padded buffer, not the
   raw 16-byte record.
4. Update `endstop_restore()` if its read-back logic needs adjusting to
   match the new padded layout — confirm whether it does or doesn't
   before assuming either way.
5. **Note the endstop record has no version field**, unlike the other
   three (confirmed in an earlier review). Don't add one as part of this
   fix unless it's needed to reuse the padding pattern cleanly — this fix
   is about the sizing defect specifically, not about making the record
   structurally identical to the other three in every respect.

## Verification

Report, don't just claim:

1. Quote the padding/static-assertion pattern from the reference
   implementation you copied from.
2. Quote the corrected endstop record definition and the new static
   assertion.
3. Confirm `endstop_persist()` now writes a properly-sized buffer —
   quote the actual `flash_range_program()` call showing this.
4. Confirm `endstop_restore()` still correctly reads back real endstop
   values after this change — test this logically if you can't test on
   real hardware, and say which you did.
5. Confirm this doesn't change the actual flash offset used for
   endstops (`0x3FF000`, confirmed in an earlier review) — this fix
   should change the write size, not relocate where it writes.
6. Confirm build is clean, `-Wall -Wextra`, both targets if applicable.

Follow this project's `AGENTS.md` flash-and-console-reset procedure to
flash and verify on real hardware, per the current skill.md default —
report the actual flash and reset output, and confirm the endstop
values survive a reset correctly after this fix.
