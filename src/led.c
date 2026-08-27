#include "led.h"

#include "config.h"
#include "co2.h"
#include "controller.h"
#include "encoder.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "power_monitor.h"
#include "ws2812.pio.h"

#include <limits.h>

static PIO led_pio;
static uint led_sm, led_offset;
static uint32_t last_colour;
static uint32_t raw_colour;
static led_mode_t mode;
static bool rgbw_enabled;
static bool power_enabled;
static bool data_enabled;
static uint64_t power_ready_us;
static bool co2_error_latched;
static uint32_t co2_error_started_ms;

static void led_data_disable(void) {
  if (data_enabled)
    pio_sm_set_enabled(led_pio, led_sm, false);
  gpio_init(PIN_LED_DATA);
  gpio_put(PIN_LED_DATA, false);
  gpio_set_dir(PIN_LED_DATA, GPIO_OUT);
  data_enabled = false;
}

static void led_data_enable(void) {
  ws2812_program_init(led_pio, led_sm, led_offset, PIN_LED_DATA, 800000.0f,
                      rgbw_enabled);
  data_enabled = true;
  last_colour = UINT32_MAX;
}

static void led_transmit_dark(void) {
  if (!data_enabled)
    led_data_enable();
  pio_sm_put_blocking(led_pio, led_sm, 0u);
  /* SK6812 reset/latch time is at least 80 us.  Keep the data engine alive
     until the all-zero RGBW frame has definitely latched. */
  sleep_us(100u);
  last_colour = 0u;
}

static uint32_t colour_word(uint8_t r, uint8_t g, uint8_t b,
                            uint8_t brightness_percent) {
  r = (uint8_t)(((uint16_t)r * brightness_percent + 50u) / 100u);
  g = (uint8_t)(((uint16_t)g * brightness_percent + 50u) / 100u);
  b = (uint8_t)(((uint16_t)b * brightness_percent + 50u) / 100u);
  if (rgbw_enabled) {
    /* SK6812 wire order is GRBW. White stays zero to preserve each RGB hue. */
    uint8_t w = 0u;
    return ((uint32_t)g << 24) | ((uint32_t)r << 16) |
           ((uint32_t)b << 8) | w;
  }
  /* WS2812B wire order is GRB; the PIO shifts the upper 24 bits first. */
  uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  return grb << 8;
}

static uint32_t station5_rose(void) {
  return colour_word(LED_STATION5_R, LED_STATION5_G, LED_STATION5_B,
                     LED_HAZARD_BRIGHTNESS_PERCENT);
}

static uint32_t station4_peach(void) {
  return colour_word(LED_STATION4_R, LED_STATION4_G, LED_STATION4_B,
                     LED_STATION_BRIGHTNESS_PERCENT);
}

static uint32_t station3_butter(void) {
  return colour_word(LED_STATION3_R, LED_STATION3_G, LED_STATION3_B,
                     LED_STATION_BRIGHTNESS_PERCENT);
}

static uint32_t station2_seafoam(void) {
  return colour_word(LED_STATION2_R, LED_STATION2_G, LED_STATION2_B,
                     LED_STATION_BRIGHTNESS_PERCENT);
}

static uint32_t station1_mint(void) {
  return colour_word(LED_STATION1_R, LED_STATION1_G, LED_STATION1_B,
                     LED_STATION_BRIGHTNESS_PERCENT);
}

static uint32_t battery_deep_yellow(void) {
  return colour_word(LED_BATTERY_R, LED_BATTERY_G, LED_BATTERY_B,
                     LED_BATTERY_BRIGHTNESS_PERCENT);
}

static uint32_t co2_warm_white_breathe(uint32_t now) {
  /* Eight-second, low-brightness breathing envelope.  The RGBW pixel uses
     its neutral white die with a small amber contribution for a soft,
     warm-white result; it never snaps fully dark between steps. */
  static const uint8_t level[32] = {
      1u, 1u, 1u, 1u, 2u, 2u, 3u, 4u,
      5u, 6u, 7u, 8u, 9u, 10u, 10u, 10u,
      10u, 10u, 10u, 9u, 8u, 7u, 6u, 5u,
      4u, 3u, 2u, 2u, 1u, 1u, 1u, 1u};
  uint8_t brightness = level[(now / 250u) % 32u];
  if (!rgbw_enabled)
    return colour_word(255u, 178u, 96u, brightness);
  uint8_t r = (uint8_t)((32u * brightness + 50u) / 100u);
  uint8_t g = (uint8_t)((14u * brightness + 50u) / 100u);
  uint8_t w = (uint8_t)((255u * brightness + 50u) / 100u);
  return ((uint32_t)g << 24) | ((uint32_t)r << 16) | w;
}

static uint32_t co2_sample_warm_white(void) {
  /* Match the warm-up breath at its gentle 10% peak. */
  if (!rgbw_enabled)
    return colour_word(255u, 178u, 96u, 10u);
  return ((uint32_t)1u << 24) | ((uint32_t)3u << 16) | 26u;
}

static uint32_t co2_error_red(void) {
  return colour_word(255u, 0u, 0u, LED_HAZARD_BRIGHTNESS_PERCENT);
}

static bool hazard_lit(void) {
  uint32_t phase =
      to_ms_since_boot(get_absolute_time()) % LED_HAZARD_PERIOD_MS;
  uint32_t second_pulse = LED_HAZARD_PULSE_MS + LED_HAZARD_GAP_MS;
  return phase < LED_HAZARD_PULSE_MS ||
         (phase >= second_pulse &&
          phase < second_pulse + LED_HAZARD_PULSE_MS);
}

static position_t led_station_at_live_adc(void) {
  uint16_t adc = encoder_average();
  for (position_t station = POS_MIN; station <= POS_MAX; ++station) {
    uint16_t nominal = encoder_nominal(station);
    uint16_t delta = adc > nominal ? (uint16_t)(adc - nominal)
                                   : (uint16_t)(nominal - adc);
    if (delta <= LED_STATION_WINDOW_COUNTS)
      return station;
  }
  return POS_BETWEEN;
}

static uint32_t requested_colour(void) {
  if (mode == LED_MODE_FORCED_OFF)
    return 0u;
  /* No mode may illuminate the pixel while moving or between stations.
     Apply this gate before the debug forced-on/raw modes so a previous LED
     test command cannot leave the SK6812 lit during subsequent travel. */
  if (controller_state() != ST_IDLE)
    return 0u;
  position_t station = led_station_at_live_adc();
  if (station < POS_MIN || station > POS_MAX)
    return 0u;
  /* Station 5 hazard indication always has priority. */
  if (mode == LED_MODE_AUTO && station == POS_MAX)
    return hazard_lit() ? station5_rose() : 0u;
  if (mode == LED_MODE_FORCED_RAW)
    return rgbw_enabled ? raw_colour : raw_colour << 8;
  if (mode == LED_MODE_FORCED_ON)
    return station5_rose();
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (co2_sensor_error()) {
    if (!co2_error_latched) {
      co2_error_latched = true;
      co2_error_started_ms = now;
    }
    uint32_t elapsed = now - co2_error_started_ms;
    return elapsed < 1200u && (elapsed % 400u) < 200u ? co2_error_red() : 0u;
  }
  co2_error_latched = false;
  if (co2_warming_up())
    return co2_warm_white_breathe(now);
  if (!co2_filtered_valid())
    return co2_sample_flash_active() ? co2_sample_warm_white() : 0u;
  power_sample_t battery;
  power_monitor_snapshot(&battery);
  if (battery.valid && battery.bus_mv < BATTERY_CRITICAL_MV)
    return hazard_lit() ? battery_deep_yellow() : 0u;
  if (battery.valid && battery.bus_mv < BATTERY_WARN_MV)
    return battery_deep_yellow();
  if (station == 1u)
    return station1_mint();
  if (station == 2u)
    return station2_seafoam();
  if (station == 3u)
    return station3_butter();
  if (station == 4u)
    return station4_peach();
  return 0u;
}

static bool station5_hazard_selected(void) {
  return mode == LED_MODE_AUTO && controller_state() == ST_IDLE &&
         led_station_at_live_adc() == POS_MAX;
}

void led_power_init(void) {
  gpio_init(PIN_LED_POWER);
  gpio_pull_down(PIN_LED_POWER);
  gpio_put(PIN_LED_POWER, false);
  gpio_set_dir(PIN_LED_POWER, GPIO_OUT);
  power_enabled = false;
  power_ready_us = 0u;
  co2_error_latched = false;
  co2_error_started_ms = 0u;
}

void led_init(void) {
  led_pio = pio0;
  led_sm = pio_claim_unused_sm(led_pio, true);
  led_offset = pio_add_program(led_pio, &ws2812_program);
  rgbw_enabled = LED_RGBW != 0;
  data_enabled = false;
  led_data_disable();
  mode = LED_MODE_AUTO;
  raw_colour = 0u;
  last_colour = UINT32_MAX;
  /* GP0 is not fitted as a load-switch control on the current hardware.
     Clear any colour retained by the externally powered pixel after reset. */
  led_transmit_dark();
  led_data_disable();
  led_update();
}

void led_update(void) {
  uint32_t colour = requested_colour();
  bool power_required = colour != 0u || station5_hazard_selected();
  if (!power_required) {
    if (power_enabled) {
      if (last_colour != 0u)
        led_transmit_dark();
      led_data_disable();
      gpio_put(PIN_LED_POWER, false);
    } else if (last_colour != 0u) {
      /* Also cover boards where GP0 is not connected and therefore cannot
         represent the pixel's real power state. */
      led_transmit_dark();
      led_data_disable();
    }
    power_enabled = false;
    power_ready_us = 0u;
    last_colour = 0u;
    return;
  }
  if (!power_enabled) {
    gpio_put(PIN_LED_POWER, true);
    power_enabled = true;
    power_ready_us = time_us_64() + LED_POWER_STARTUP_US;
    last_colour = UINT32_MAX;
    return;
  }
  if (time_us_64() < power_ready_us)
    return;
  if (!data_enabled)
    led_data_enable();
  if (colour == last_colour)
    return;
  pio_sm_put_blocking(led_pio, led_sm, colour);
  last_colour = colour;
}

void led_set_mode(led_mode_t new_mode) {
  mode = new_mode;
  /* An explicit command must reach hardware even if it was reconnected while
     the cached logical colour already matched the request. */
  last_colour = UINT32_MAX;
  led_update();
}

led_mode_t led_mode(void) { return mode; }
bool led_is_on(void) { return power_enabled && last_colour != 0u; }
bool led_rgbw(void) { return rgbw_enabled; }
void led_set_rgbw(bool enabled) {
  if (enabled == rgbw_enabled)
    return;
  led_data_disable();
  rgbw_enabled = enabled;
  last_colour = UINT32_MAX;
  led_update();
}
void led_set_raw(uint32_t wire_word) {
  raw_colour = wire_word;
  mode = LED_MODE_FORCED_RAW;
  last_colour = UINT32_MAX;
  led_update();
}
bool led_powered(void) { return power_enabled; }
uint led_pio_index(void) { return led_pio == pio0 ? 0u : 1u; }
uint led_state_machine(void) { return led_sm; }
uint led_program_offset(void) { return led_offset; }
