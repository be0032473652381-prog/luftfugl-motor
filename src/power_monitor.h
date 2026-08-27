#ifndef LUFTFUGL_POWER_MONITOR_H
#define LUFTFUGL_POWER_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  bool valid;
  bool overflow;
  bool calibration_written;
  uint16_t config_raw;
  uint16_t calibration_raw;
  uint16_t bus_raw;
  int16_t shunt_raw;
  int16_t current_raw;
  uint16_t power_raw;
  uint32_t bus_mv;
  int32_t shunt_uv;
  int32_t current_ua;
  uint32_t power_uw;
  uint32_t sample_ms;
} power_sample_t;

void power_monitor_init(void);
void power_monitor_tick(void);
bool power_monitor_i2c_claim(void);
void power_monitor_i2c_release(void);
void power_monitor_request_sample(void);
void power_monitor_motion_start(void);
void power_monitor_motion_stop(void);
void power_monitor_reset(void);
void power_monitor_snapshot(power_sample_t *out);
void power_monitor_format_batt(char *out, size_t size);
void power_monitor_format_raw(char *out, size_t size);
void power_monitor_format_res(char *out, size_t size);
void power_monitor_format_log(char *out, size_t size);
void power_monitor_format_events(char *out, size_t size);
void power_monitor_format_load(char *out, size_t size);
void power_monitor_format_ina(char *out, size_t size);
void power_monitor_format_menu(char lines[6][81]);
bool power_monitor_sim_set(bool enabled, uint16_t bus_mv);
bool power_monitor_sim_active(void);

#endif
