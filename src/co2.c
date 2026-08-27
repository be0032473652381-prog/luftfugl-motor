#include "co2.h"

#include "config.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

static bool detected;
static co2_variant_t variant;
static bool measuring;
static uint32_t next_poll_ms;
static bool sample_valid;
static uint16_t ppm, humidity_tenths;
static int16_t temperature_tenths;
static uint32_t frames;

static uint8_t crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0xffu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8u; ++bit)
      crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u)
                          : (uint8_t)(crc << 1);
  }
  return crc;
}

void co2_init(void) {
  uint8_t command[] = {0x20u, 0x2fu};
  uint8_t response[3];
  detected = false;
  variant = CO2_VARIANT_NONE;
  measuring = false;
  sample_valid = false;
  frames = 0u;
  if (i2c_write_blocking(i2c0, SCD41_ADDRESS, command, sizeof command, true) < 0)
    return;
  sleep_ms(1);
  if (i2c_read_blocking(i2c0, SCD41_ADDRESS, response, sizeof response, false) !=
      (int)sizeof response || crc8(response, 2u) != response[2])
    return;
  detected = true;
  uint16_t raw = (uint16_t)((response[0] << 8) | response[1]);
  switch (raw & 0xf000u) {
  case 0x0000u: variant = CO2_VARIANT_SCD40; break;
  case 0x1000u: variant = CO2_VARIANT_SCD41; break;
  default: variant = CO2_VARIANT_OTHER; break;
  }
  uint8_t start[] = {0x21u, 0xb1u};
  if (i2c_write_blocking(i2c0, SCD41_ADDRESS, start, sizeof start, true) >= 0) {
    measuring = true;
    next_poll_ms = to_ms_since_boot(get_absolute_time()) + 5000u;
  }
}

bool co2_detected(void) { return detected; }
co2_variant_t co2_variant(void) { return variant; }

void co2_tick(void) {
  if (!measuring || !detected)
    return;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if ((int32_t)(now - next_poll_ms) < 0)
    return;
  next_poll_ms = now + 5000u;
  uint8_t ready_cmd[] = {0xe4u, 0xb8u};
  uint8_t ready[3];
  if (i2c_write_blocking(i2c0, SCD41_ADDRESS, ready_cmd, sizeof ready_cmd, true) < 0)
    return;
  sleep_ms(1);
  if (i2c_read_blocking(i2c0, SCD41_ADDRESS, ready, sizeof ready, false) != 3 ||
      crc8(ready, 2u) != ready[2] || !(ready[0] & 0x07u))
    return;
  uint8_t read_cmd[] = {0xecu, 0x05u};
  uint8_t data[9];
  if (i2c_write_blocking(i2c0, SCD41_ADDRESS, read_cmd, sizeof read_cmd, true) < 0)
    return;
  sleep_ms(1);
  if (i2c_read_blocking(i2c0, SCD41_ADDRESS, data, sizeof data, false) != 9)
    return;
  if (crc8(data, 2u) != data[2] || crc8(data + 3, 2u) != data[5] ||
      crc8(data + 6, 2u) != data[8])
    return;
  uint16_t raw_co2 = (uint16_t)((data[0] << 8) | data[1]);
  uint16_t raw_temp = (uint16_t)((data[3] << 8) | data[4]);
  uint16_t raw_rh = (uint16_t)((data[6] << 8) | data[7]);
  ppm = raw_co2;
  temperature_tenths = (int16_t)(-450 + ((1750L * raw_temp + 32768L) / 65535L));
  humidity_tenths = (uint16_t)((1000UL * raw_rh + 32768UL) / 65535UL);
  sample_valid = true;
  ++frames;
}

bool co2_sample_valid(void) { return sample_valid; }
uint16_t co2_ppm(void) { return ppm; }
int16_t co2_temperature_tenths(void) { return temperature_tenths; }
uint16_t co2_humidity_tenths(void) { return humidity_tenths; }
uint32_t co2_frames_read(void) { return frames; }
