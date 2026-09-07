# Verify agent.md and function-description.md against current source — report only, do not edit

## Why this review, and why now

Both files are significantly older than the current codebase state:
agent.md last modified August 28, function-description.md last
modified August 10. Since then, confirmed real changes have happened
across the codebase: the VEML7700 ambient light sensor was added, the
buzzer's DDS engine was substantially reworked (crips-2/crips-3), the
endstop flash-write sizing bug was fixed, the console UART pin
assignment was corrected (GP8/GP9, not GP20/GP21), event_timer.c and
debug_help.h were introduced, and Station 6 (EVENT_POSITION) was
implemented. Two other documents in this project (hardware.md,
skill.md) were reviewed this same way recently and both had genuine,
substantive drift corrected across multiple rounds -- there's no reason
to assume these two are any different.

## Critical constraint -- report, do not edit

Per this project's own AGENTS.md: these are specification documents,
not code. Do not modify agent.md or function-description.md directly,
even if something looks clearly wrong. Report every discrepancy found,
with exact file and line citations on both sides (the claim in the
doc, and the actual current code that contradicts or confirms it) --
the same rigor already applied across the recent skill.md review
rounds. What happens with each finding is a human decision, not
something to resolve by editing the spec to match the code silently.

## What to check specifically

Go through both documents section by section against current source.
Prioritize areas most likely to have drifted given what's changed
since these were last touched:

1. Pin assignments -- cross-check every GPIO claim against config.h
   directly. The UART pin correction (GP8/GP9) is a known recent
   change worth specifically verifying is reflected correctly, or
   flagged if not.
2. Peripheral/sensor inventory -- does either document mention the
   VEML7700 at all? If not, that's a real gap, not just an omission --
   confirm whether either document's peripheral list needs updating.
3. Buzzer architecture -- does either document describe the buzzer's
   actual current DDS-based tone generation, or an earlier, simpler
   PWM-only description that predates that rework? Check specifically
   for outdated command names if play-2/play-3 or earlier names appear
   anywhere, given the confirmed canonical names are crips-2/crips-3.
4. Persistence claims -- does either document's description of what
   gets saved to flash match the actual current four-subsystem pattern
   (endstops, battery settings, CO2 profiles, event-timer interval)?
5. State machine / station count -- does either document reflect
   Station 6 (EVENT_POSITION) existing, or only describe five
   stations?
6. Function inventory in function-description.md specifically -- does
   it list event_timer.c's and debug_help.c's actual current
   functions? These are confirmed-existing files; if the document
   predates them, its module list is incomplete, not just slightly
   dated.
7. Anything else you find -- don't limit yourself to the six items
   above; these are known likely trouble spots, not an exhaustive list.

## Output format

For each finding: quote the specific claim in the document (with line
number), quote the actual current code that confirms or contradicts
it (with file and line number), and state plainly whether it's a real
discrepancy, confirmed-still-accurate, or something you're not fully
certain about either way.

No files should be changed as a result of this review. This is a
report only.
