#include "debug_help.h"

#include "config.h"
#include <stdio.h>
#include <string.h>

#define COUNT(items) (sizeof(items) / sizeof((items)[0]))
#define SYNTAX(items) .syntax = items, .syntax_count = COUNT(items)
#define PARAMETERS(items) .parameters = items, .parameter_count = COUNT(items)
#define INTERACTIONS(items) .interactions = items, .interaction_count = COUNT(items)
#define NOTES(items) .notes = items, .note_count = COUNT(items)

static const char *const status_syntax[] = {"status"};
static const char *const status_notes[] = {
    "Read-only.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated."};

static const char *const brightness_syntax[] = {
    "led brightness <parameter> <0..100>",
    "led brightness -> lists all six current RAM values",
    "led brightness reset -> restores all compiled defaults"};
static const debug_help_parameter_t brightness_parameters[] = {
    {"station", "all five CO2 station colours", "3%", "led brightness station 5"},
    {"warning", "battery-warning flash", "30%", "led brightness warning 40"},
    {"critical", "battery-critical flash", "30%", "led brightness critical 40"},
    {"error", "CO2 sensor-error red flash", "30%", "led brightness error 25"},
    {"sample", "startup accepted-sample flash", "10%", "led brightness sample 15"},
    {"breathe", "SCD41 warm-up maximum", "10%", "led brightness breathe 7"}};
static const char *const brightness_interactions[] = {
    "The selected base is multiplied by the confirmed VEML7700 zone multiplier. The final channel value is rounded and saturated at 100%; alert pulses may saturate.",
    "Station brightness remains below the alert base through the station ceiling.",
    "LED is still forced off while moving, between stations, or in forced-off mode.",
    "led raw <hex> is a diagnostic wire word and bypasses this percentage control."};
static const char *const brightness_notes[] = {
    "Values are RAM-only and return to defaults after reset; no flash is written.",
    "Warning uses an orange double flash; critical uses an orange hazard flash; the startup accepted-sample flash is warm-white."};

static const char *const chirp_syntax[] = {
    "batt chirp [<frequency> kHz] [/s]",
    "batt chirp -> report the active chirp frequency and default source"};
static const char *const chirp_interactions[] = {
    "Sounds only while a valid battery reading is below the critical threshold.",
    "Stops immediately when voltage is no longer critical; no boundary chirp.",
    "Audible alert remains active regardless of LED auto/on/off/raw mode.",
    "Use help batt chirp time to configure sequence interval, repeat, pause, and duration."};
static const char *const chirp_notes[] = {
    "Frequency range: 0.10 to 10.00 kHz (100 to 10000 Hz).",
    "Compiled default is 2.700 kHz when no valid flash record exists.",
    "batt chirp 3.50 kHz - use 3.5 kHz until reboot.",
    "batt chirp 3.50 kHz /s - save it as the power-on default.",
    "/s saves range, warning, critical, and chirp defaults together.",
    "Examples: batt chirp | batt chirp 0.10 kHz | batt chirp 10.00 kHz /s"};

static const char *const crips3_syntax[] = {"buzzer crips-3 <1..200>"};
static const char *const crips3_interactions[] = {"buzzer off stops playback."};
static const char *const crips3_notes[] = {
    "Count is 1..200 complete calls. Example: buzzer crips-3 10.",
    "Listening test only: three-part clean-sine sequence; 8/8/12 bursts, 50 ms breaks; 444 ms total, 100 ms repeat gap.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated."};

static const char *const serial_syntax[] = {"serial"};
static const char *const serial_interactions[] = {
    "Stops periodic measurement briefly and reads the SCD41 48-bit serial number."};
static const char *const serial_notes[] = {
    "No arguments.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated."};
static const char *const serial_page5_notes[] = {"SDC41 Page-5 command."};

/* Approved review documents share the renderer with the complete catalog. */
static const debug_help_document_t documents[] = {
    {.name = "STATUS", .purpose = "Shows the full controller state.",
     SYNTAX(status_syntax), NOTES(status_notes)},
    {.name = "LED BRIGHTNESS", .purpose = "base % for one automatic indication",
     SYNTAX(brightness_syntax), PARAMETERS(brightness_parameters),
     INTERACTIONS(brightness_interactions), NOTES(brightness_notes)},
    {.name = "BATT CHIRP", .purpose = "tone for the repeating critical-battery audible alert",
     SYNTAX(chirp_syntax), INTERACTIONS(chirp_interactions), NOTES(chirp_notes)},
    {.name = "BUZZER CRIPS-3", .purpose = "Listening test only: three-part clean-sine sequence.",
     SYNTAX(crips3_syntax), INTERACTIONS(crips3_interactions), NOTES(crips3_notes)},
    {.name = "SERIAL", .purpose = "read the 48-bit SCD41 serial number",
     SYNTAX(serial_syntax), INTERACTIONS(serial_interactions), NOTES(serial_notes)}};
static const debug_help_document_t serial_page5 = {
    .name = "SERIAL", .purpose = "read the 48-bit SCD41 serial number",
    SYNTAX(serial_syntax), NOTES(serial_page5_notes)};

#include "debug_help_catalog.inc"

bool debug_help_page5_topic(const char *name) {
  if (!strcmp(name, "serial")) return true;
  for (size_t i = 0; i < COUNT(catalog); ++i)
    if (catalog[i].page == 5u && !strcmp(catalog[i].key, name))
      return true;
  return false;
}

const debug_help_document_t *debug_help_find(const char *name, uint8_t page) {
  static const char *const keys[] = {
      "status", "led brightness", "batt chirp", "buzzer crips-3", "serial"};
  if (page == 5u && !strcmp(name, "serial"))
    return &serial_page5;
  for (size_t i = 0; i < COUNT(catalog); ++i)
    if ((!catalog[i].page || catalog[i].page == page) &&
        !strcmp(catalog[i].key, name))
      return catalog[i].document;
  for (size_t i = 0; i < COUNT(keys); ++i)
    if (!strcmp(keys[i], name))
      return &documents[i];
  return NULL;
}

static void parameter_line(const debug_help_document_t *d, size_t row,
                           char *line, size_t size) {
  size_t name_width = strlen("PARAMETER"), meaning_width = strlen("MEANING");
  size_t default_width = strlen("DEFAULT");
  for (size_t i = 0; i < d->parameter_count; ++i) {
    const debug_help_parameter_t *p = &d->parameters[i];
    if (strlen(p->name) > name_width) name_width = strlen(p->name);
    if (strlen(p->meaning) > meaning_width) meaning_width = strlen(p->meaning);
    if (strlen(p->default_value) > default_width) default_width = strlen(p->default_value);
  }
  const debug_help_parameter_t header = {"PARAMETER", "MEANING", "DEFAULT", "EXAMPLE"};
  const debug_help_parameter_t *p = row ? &d->parameters[row - 1u] : &header;
  snprintf(line, size, "%*s  %-*s  %-*s  %s", -(int)name_width, p->name,
           (int)meaning_width, p->meaning, (int)default_width, p->default_value,
           p->example);
}

/* Enumerate logical lines in one order. The terminal's existing top-insert
 * result window needs reverse emission; host previews use forward emission.
 * This keeps section order and blank lines identical without a page buffer. */
static void document_line(const debug_help_document_t *d, size_t index,
                          char *line, size_t size) {
  line[0] = '\0';
  /* Separate the submitted command from its help in both output modes. */
  if (index == 0u) return;
  --index;
  if (index == 0u) {
    snprintf(line, size, "%s — %s", d->name, d->purpose);
    return;
  }
  if (index == 1u) return;
  index -= 2u;
  if (index < d->syntax_count) {
    snprintf(line, size, "%s%s", index ? "          " : "SYNTAX    ", d->syntax[index]);
    return;
  }
  index -= d->syntax_count;
  if (d->parameter_count > 1u) {
    if (index == 0u) return;
    if (index <= d->parameter_count + 1u) {
      parameter_line(d, index - 1u, line, size);
      return;
    }
    index -= d->parameter_count + 2u;
  }
  if (d->interaction_count) {
    if (index == 0u) return;
    if (index == 1u) {
      snprintf(line, size, "INTERACTIONS, most consequential first");
      return;
    }
    if (index < d->interaction_count + 2u) {
      snprintf(line, size, "- %s", d->interactions[index - 2u]);
      return;
    }
    index -= d->interaction_count + 2u;
  }
  if (d->note_count + d->live_note_count) {
    if (index == 0u) return;
    if (index == 1u) snprintf(line, size, "NOTE");
    else {
      size_t note = index - 2u;
      snprintf(line, size, "%s", note < d->note_count ? d->notes[note]
               : d->live_notes[note - d->note_count]);
    }
  }
}

void debug_help_render(const debug_help_document_t *d, bool reverse,
                       debug_help_emit_t emit, void *context) {
  size_t count = 3u + d->syntax_count;
  if (d->parameter_count > 1u) count += d->parameter_count + 2u;
  if (d->interaction_count) count += d->interaction_count + 2u;
  if (d->note_count + d->live_note_count) count += d->note_count + d->live_note_count + 2u;
  for (size_t i = 0; i < count; ++i) {
    char line[DEBUG_HEADER_BUFFER_SIZE];
    document_line(d, reverse ? count - 1u - i : i, line, sizeof line);
    emit(line, context);
  }
}
