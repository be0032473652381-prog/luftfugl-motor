#ifndef LUFTFUGL_DEBUG_HELP_H
#define LUFTFUGL_DEBUG_HELP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *name;
  const char *meaning;
  const char *default_value;
  const char *example;
} debug_help_parameter_t;

typedef struct {
  const char *name;
  const char *purpose;
  const char *const *syntax;
  size_t syntax_count;
  const debug_help_parameter_t *parameters;
  size_t parameter_count;
  const char *const *interactions;
  size_t interaction_count;
  const char *const *notes;
  size_t note_count;
  /* Optional main-context snapshots appended to NOTE for this rendering. */
  const char *const *live_notes;
  size_t live_note_count;
} debug_help_document_t;

typedef void (*debug_help_emit_t)(const char *line, void *context);

/* Presentation lookup only: command recognition stays in the command table.
 * Page-5 sensor documents are selected before the general documents. */
const debug_help_document_t *debug_help_find(const char *name, uint8_t page);
bool debug_help_page5_topic(const char *name);
void debug_help_render(const debug_help_document_t *document, bool reverse,
                       debug_help_emit_t emit, void *context);

#endif
