#include "ambient_light.h"

#include "config.h"
#include "hardware/i2c.h"
#include "pico/time.h"
#include "power_monitor.h"

/* Register addresses and bit assignments are device protocol, not tuning. */
enum { REG_CONFIG = 0x00, REG_POWER_SAVING = 0x03, REG_ALS = 0x04 };
#define ALS_SHUTDOWN_BIT 0x0001u

typedef enum { ALS_IDLE, ALS_INTEGRATING, ALS_NEEDS_SHUTDOWN } als_state_t;
static als_state_t state;
static bool requested;
static uint64_t ready_us;
static ambient_light_sample_t sample;

static const uint32_t zone_min_lux[AMBIENT_ZONE_COUNT] = {
    AMBIENT_NIGHT_MIN_LUX, AMBIENT_DIM_MIN_LUX,
    AMBIENT_INDOOR_MIN_LUX, AMBIENT_BRIGHT_MIN_LUX};
static const uint16_t zone_multiplier[AMBIENT_ZONE_COUNT] = {
    AMBIENT_NIGHT_MULTIPLIER_PERCENT, AMBIENT_DIM_MULTIPLIER_PERCENT,
    AMBIENT_INDOOR_MULTIPLIER_PERCENT, AMBIENT_BRIGHT_MULTIPLIER_PERCENT};
#ifdef LUFTFUGL_MONITOR
static uint16_t zone_multiplier_override[AMBIENT_ZONE_COUNT];
#endif

_Static_assert(AMBIENT_NIGHT_MIN_LUX == 0u &&
                   AMBIENT_NIGHT_MIN_LUX < AMBIENT_DIM_MIN_LUX &&
                   AMBIENT_DIM_MIN_LUX < AMBIENT_INDOOR_MIN_LUX &&
                   AMBIENT_INDOOR_MIN_LUX < AMBIENT_BRIGHT_MIN_LUX,
               "ambient zone floors must start at zero and ascend");
_Static_assert(AMBIENT_HYSTERESIS_PERCENT < 100u &&
                   AMBIENT_CONFIRM_SAMPLES > 1u,
               "ambient transitions require hysteresis and multiple samples");

static void cancel_candidate(void) {
  sample.candidate_zone = sample.confirmed_zone;
  sample.candidate_samples = 0u;
}

static void confirm_zone(void) {
  ambient_zone_t instant = AMBIENT_NIGHT;
  while (instant + 1 < AMBIENT_ZONE_COUNT &&
         sample.millilux >= (uint64_t)zone_min_lux[instant + 1] * 1000u)
    ++instant;
  sample.instant_zone = instant;

  /* Start from the confirmed zone, never from the last raw reading. Every
   * crossed boundary must clear its directional margin, even on a jump
   * across several zones. The confirmed/candidate state survives sensor
   * shutdown and WFI; only initialization resets it to Night. */
  ambient_zone_t candidate = sample.confirmed_zone;
  while (candidate + 1 < AMBIENT_ZONE_COUNT &&
         (uint64_t)sample.millilux * 100u >=
             (uint64_t)zone_min_lux[candidate + 1] * 1000u *
                 (100u + AMBIENT_HYSTERESIS_PERCENT))
    ++candidate;
  while (candidate > AMBIENT_NIGHT &&
         (uint64_t)sample.millilux * 100u <
             (uint64_t)zone_min_lux[candidate] * 1000u *
                 (100u - AMBIENT_HYSTERESIS_PERCENT))
    --candidate;
  if (candidate == sample.confirmed_zone) {
    cancel_candidate();
    return;
  }
  if (candidate != sample.candidate_zone) {
    sample.candidate_zone = candidate;
    sample.candidate_samples = 0u;
  }
  ++sample.candidate_samples;
  if (sample.candidate_samples >= AMBIENT_CONFIRM_SAMPLES) {
    sample.confirmed_zone = candidate;
    cancel_candidate();
  }
}

/* Keep the selected gain, integration time and conversion factor together.
 * Changing the measurement profile requires updating all three constants. */
_Static_assert(VEML7700_ACTIVE_CONFIG == 0x1000u &&
                   VEML7700_INTEGRATION_US == 100000u,
               "update VEML7700 conversion/timing for a different profile");

static bool write_register(uint8_t reg, uint16_t value) {
  uint8_t bytes[] = {reg, (uint8_t)value, (uint8_t)(value >> 8)};
  return i2c_write_timeout_us(i2c0, VEML7700_ADDRESS, bytes, sizeof bytes,
                              false, I2C_TRANSACTION_TIMEOUT_US) ==
         (int)sizeof bytes;
}

static bool read_register(uint8_t reg, uint16_t *value) {
  uint8_t bytes[2];
  if (i2c_write_timeout_us(i2c0, VEML7700_ADDRESS, &reg, 1u, true,
                           I2C_TRANSACTION_TIMEOUT_US) != 1 ||
      i2c_read_timeout_us(i2c0, VEML7700_ADDRESS, bytes, sizeof bytes, false,
                          I2C_TRANSACTION_TIMEOUT_US) != (int)sizeof bytes)
    return false;
  *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
  return true;
}

/* Called with the shared bus claimed, including after a failed wake/read.
 * Read back the whole config: an ACK alone does not prove ALS_SD is set. */
static bool shutdown_sensor(void) {
  const uint16_t shutdown = VEML7700_ACTIVE_CONFIG | ALS_SHUTDOWN_BIT;
  sample.shutdown_verified =
      write_register(REG_CONFIG, shutdown) &&
      read_register(REG_CONFIG, &sample.config_raw) &&
      sample.config_raw == shutdown;
  state = sample.shutdown_verified ? ALS_IDLE : ALS_NEEDS_SHUTDOWN;
  sample.measuring = false;
  return sample.shutdown_verified;
}

static uint32_t counts_to_millilux(uint16_t raw) {
  /* All lux arithmetic runs in main context. The sensor returns counts,
   * not lux; the high-light correction is unnecessary at our zone edges. */
  float lux = (float)raw * VEML7700_LUX_PER_COUNT;
  if (lux > VEML7700_CORRECTION_THRESHOLD_LUX)
    lux = (((VEML7700_CORRECTION_A * lux + VEML7700_CORRECTION_B) * lux +
            VEML7700_CORRECTION_C) * lux + VEML7700_CORRECTION_D) * lux;
  return (uint32_t)(lux * 1000.0f + 0.5f);
}

void ambient_light_init(void) {
  sample = (ambient_light_sample_t){0};
  requested = false;
  state = ALS_NEEDS_SHUTDOWN;
#ifdef LUFTFUGL_MONITOR
  ambient_light_multiplier_reset();
#endif
  if (power_monitor_i2c_claim()) {
    if (!shutdown_sensor())
      ++sample.errors;
    power_monitor_i2c_release();
  }
}

void ambient_light_request_sample(void) {
  /* Coalesce cycles while a measurement is in flight; never queue a burst of
   * stale wake requests after main-context work was delayed. */
  if (state != ALS_INTEGRATING)
    requested = true;
}

void ambient_light_poll(void) {
  if (state == ALS_INTEGRATING) {
    if (time_us_64() < ready_us || !power_monitor_i2c_claim())
      return;
    uint16_t raw = 0u;
    bool read_ok = read_register(REG_ALS, &raw);
    bool down_ok = shutdown_sensor();
    power_monitor_i2c_release();
    sample.valid = read_ok && down_ok;
    if (sample.valid) {
      sample.raw = raw;
      sample.millilux = counts_to_millilux(raw);
      ++sample.samples;
      confirm_zone();
    } else {
      ++sample.errors;
      cancel_candidate();
    }
    return;
  }
  if (!requested || !power_monitor_i2c_claim())
    return;
  requested = false;
  /* Recovery retries only on an existing sensor cycle. A missing device or
   * failed shutdown must not cause I2C traffic on every 1 ms safety wake. */
  if (state == ALS_NEEDS_SHUTDOWN && !shutdown_sensor()) {
    sample.valid = false;
    ++sample.errors;
    cancel_candidate();
    power_monitor_i2c_release();
    return;
  }
  /* Explicitly disable PSM, including after MCU-only reset, so the documented
   * integration wait applies regardless of the sensor's retained state. */
  bool started = write_register(REG_POWER_SAVING, 0u) &&
                 write_register(REG_CONFIG, VEML7700_ACTIVE_CONFIG);
  if (started) {
    ready_us = time_us_64() + VEML7700_READY_US;
    state = ALS_INTEGRATING;
    sample.measuring = true;
    sample.shutdown_verified = false;
  } else {
    sample.valid = false;
    ++sample.errors;
    cancel_candidate();
    (void)shutdown_sensor();
  }
  power_monitor_i2c_release();
}

void ambient_light_snapshot(ambient_light_sample_t *out) { *out = sample; }

uint16_t ambient_light_multiplier_percent(void) {
  uint16_t multiplier = zone_multiplier[sample.confirmed_zone];
#ifdef LUFTFUGL_MONITOR
  multiplier = zone_multiplier_override[sample.confirmed_zone];
#endif
  return multiplier;
}

const char *ambient_light_zone_name(ambient_zone_t zone) {
  static const char *const names[AMBIENT_ZONE_COUNT] = {
      "night", "dim", "indoor", "bright"};
  return zone < AMBIENT_ZONE_COUNT ? names[zone] : "unknown";
}

#ifdef LUFTFUGL_MONITOR
bool ambient_light_multiplier_set(ambient_zone_t zone, uint16_t percent) {
  if (zone >= AMBIENT_ZONE_COUNT || percent > 2000u)
    return false;
  zone_multiplier_override[zone] = percent;
  return true;
}
uint16_t ambient_light_multiplier_get(ambient_zone_t zone) {
  return zone < AMBIENT_ZONE_COUNT ? zone_multiplier_override[zone] : 0u;
}
void ambient_light_multiplier_reset(void) {
  for (unsigned int i = 0u; i < AMBIENT_ZONE_COUNT; ++i)
    zone_multiplier_override[i] = zone_multiplier[i];
}
#endif
