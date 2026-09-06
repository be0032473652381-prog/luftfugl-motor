#ifndef LUFTFUGL_AMBIENT_LIGHT_H
#define LUFTFUGL_AMBIENT_LIGHT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AMBIENT_NIGHT = 0,
  AMBIENT_DIM,
  AMBIENT_INDOOR,
  AMBIENT_BRIGHT,
  AMBIENT_ZONE_COUNT
} ambient_zone_t;

typedef struct {
  bool valid;
  bool shutdown_verified;
  bool measuring;
  uint16_t raw;
  uint16_t config_raw;
  uint32_t millilux;
  uint32_t samples;
  uint32_t errors;
  ambient_zone_t instant_zone;
  ambient_zone_t confirmed_zone;
  ambient_zone_t candidate_zone;
  uint32_t candidate_samples;
} ambient_light_sample_t;

/* Main context only. Init follows power_monitor_init(); requests come from
 * the existing sensor cycle, never from a separate periodic timer. */
void ambient_light_init(void);
void ambient_light_request_sample(void);
void ambient_light_poll(void);
void ambient_light_snapshot(ambient_light_sample_t *out);
uint16_t ambient_light_multiplier_percent(void);
const char *ambient_light_zone_name(ambient_zone_t zone);
#ifdef LUFTFUGL_MONITOR
bool ambient_light_multiplier_set(ambient_zone_t zone, uint16_t percent);
uint16_t ambient_light_multiplier_get(ambient_zone_t zone);
void ambient_light_multiplier_reset(void);
#endif

#endif
