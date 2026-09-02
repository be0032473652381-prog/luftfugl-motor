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
  uint32_t filtered_bus_mv;
  bool filter_valid;
  int32_t shunt_uv;
  int32_t current_ua;
  uint32_t power_uw;
  uint32_t sample_ms;
} power_sample_t;
typedef enum {
  BATTERY_STATE_NORMAL = 0,
  BATTERY_STATE_WARNING,
  BATTERY_STATE_CRITICAL
} battery_state_t;
typedef struct {
  uint16_t interval_s;
  uint8_t repeat;
  uint8_t pause_s;
  uint8_t duration_s;
} battery_chirp_timing_t;

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
void power_monitor_format_menu(char lines[10][81]);
battery_state_t power_monitor_battery_state(void);
bool power_monitor_sim_set(bool enabled, uint16_t bus_mv);
bool power_monitor_sim_active(void);
bool power_monitor_sim_range_set(uint16_t minimum_mv, uint16_t maximum_mv);
void power_monitor_sim_range_get(uint16_t *minimum_mv, uint16_t *maximum_mv);
bool power_monitor_warning_set(uint16_t warning_mv);
uint16_t power_monitor_warning_mv(void);
bool power_monitor_critical_set(uint16_t critical_mv);
uint16_t power_monitor_critical_mv(void);
bool power_monitor_settings_save(void);
bool power_monitor_settings_from_flash(void);
bool power_monitor_adc0_offset_set(int16_t offset_mv);
int16_t power_monitor_adc0_offset_mv(void);
bool power_monitor_chirp_frequency_set(uint16_t frequency_hz);
uint16_t power_monitor_chirp_frequency_hz(void);
bool power_monitor_chirp_timing_set(const battery_chirp_timing_t *timing);
void power_monitor_chirp_timing_get(battery_chirp_timing_t *timing);

#endif
