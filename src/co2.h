#ifndef LUFTFUGL_CO2_H
#define LUFTFUGL_CO2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  CO2_VARIANT_NONE = 0,
  CO2_VARIANT_SCD40,
  CO2_VARIANT_SCD41,
  CO2_VARIANT_OTHER
} co2_variant_t;

void co2_init(void);
bool co2_detected(void);
co2_variant_t co2_variant(void);
void co2_tick(void);
bool co2_sample_valid(void);
uint16_t co2_ppm(void);
int16_t co2_temperature_tenths(void);
uint16_t co2_humidity_tenths(void);
uint32_t co2_frames_read(void);
bool co2_warming_up(void);
bool co2_filtered_valid(void);
uint8_t co2_filter_samples(void);
bool co2_sample_flash_active(void);
bool co2_sensor_error(void);
void co2_format_menu(char lines[18][81]);
bool co2_command(const char *command, const char *args, char *out, size_t size);
const char *co2_command_help(const char *command);

#endif
