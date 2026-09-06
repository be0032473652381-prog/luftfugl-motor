#include "led.h"

#include "config.h"
#include "ambient_light.h"
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
static bool pixel_lit;
static uint64_t power_ready_us;
static bool co2_error_latched;
static uint32_t co2_error_started_ms;
#ifdef LUFTFUGL_MONITOR
static uint8_t brightness_override[LED_BRIGHTNESS_KIND_COUNT];
#endif

static void led_data_disable(void) {
  if (data_enabled)
    pio_sm_set_enabled(led_pio, led_sm, false);
  gpio_init(PIN_LED_DATA);
  gpio_put(PIN_LED_DATA, false);
  gpio_set_dir(PIN_LED_DATA, GPIO_OUT);
  data_enabled = false;
}

static void led_data_enable(void) {
  ws2812_program_init(led_pio, led_sm, led_offset, PIN_LED_DATA,
                      (float)LED_DATA_RATE_HZ,
                      rgbw_enabled);
  data_enabled = true;
}

static void led_transmit_frame(uint32_t colour) {
  if (!data_enabled)
    led_data_enable();
  pio_sm_put_blocking(led_pio, led_sm, colour);
  uint32_t frame_bits = rgbw_enabled ? 32u : 24u;
  uint32_t frame_us =
      (frame_bits * 1000000u + LED_DATA_RATE_HZ - 1u) / LED_DATA_RATE_HZ;
  /* FIFO acceptance precedes the final wire bit.  Wait for both the complete
     frame and the SK6812 latch interval before stopping the PIO engine. */
  sleep_us(frame_us + LED_LATCH_US);
  last_colour = colour;
  pixel_lit = colour != 0u;
  led_data_disable();
}

_Static_assert(LED_STATION_BRIGHTNESS_PERCENT > 0u &&
                   LED_AMBIENT_STATION_CEILING_PERCENT <
                       LED_HAZARD_BRIGHTNESS_PERCENT &&
                   LED_MAX_BRIGHTNESS_PERCENT <= 100u,
               "station brightness must remain below the alert base");

unsigned int led_brightness_multiplier_percent(void) {
  unsigned int multiplier = ambient_light_multiplier_percent();
  unsigned int ceiling = LED_AMBIENT_STATION_CEILING_PERCENT * 100u /
                         LED_STATION_BRIGHTNESS_PERCENT;
  return multiplier < ceiling ? multiplier : ceiling;
}

#ifdef LUFTFUGL_MONITOR
void led_brightness_reset(void) {
  brightness_override[LED_BRIGHTNESS_STATION] = LED_STATION_BRIGHTNESS_PERCENT;
  brightness_override[LED_BRIGHTNESS_WARNING] = LED_BATTERY_WARNING_BRIGHTNESS_PERCENT;
  brightness_override[LED_BRIGHTNESS_CRITICAL] = LED_BATTERY_CRITICAL_BRIGHTNESS_PERCENT;
  brightness_override[LED_BRIGHTNESS_ERROR] = LED_HAZARD_BRIGHTNESS_PERCENT;
  brightness_override[LED_BRIGHTNESS_SAMPLE] = LED_SAMPLE_BRIGHTNESS_PERCENT;
  brightness_override[LED_BRIGHTNESS_BREATHE] = 10u;
}
bool led_brightness_set(led_brightness_kind_t kind, uint8_t percent) {
  if (kind >= LED_BRIGHTNESS_KIND_COUNT || percent > 100u)
    return false;
  brightness_override[kind] = percent;
  last_colour = UINT32_MAX;
  led_update();
  return true;
}
uint8_t led_brightness_get(led_brightness_kind_t kind) {
  return kind < LED_BRIGHTNESS_KIND_COUNT ? brightness_override[kind] : 0u;
}
#endif

static uint8_t scaled_channel(uint8_t channel, uint32_t brightness) {
  /* brightness is percent * multiplier-percent: round only once, after
   * scaling, so low-level RGBW breathing retains its night-time values. */
  return (uint8_t)(((uint32_t)channel * brightness + 5000u) / 10000u);
}

static uint32_t colour_word_rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                                 uint8_t base_percent) {
  /* One shared scale for every base percentage: stations, both battery
   * alerts, CO2 errors, accepted samples, and every step of warm-up breathing.
   * Saturate the percentage before scaling channels to preserve hue. */
  uint32_t brightness = base_percent * led_brightness_multiplier_percent();
  if (brightness > LED_MAX_BRIGHTNESS_PERCENT * 100u)
    brightness = LED_MAX_BRIGHTNESS_PERCENT * 100u;
  r = scaled_channel(r, brightness);
  g = scaled_channel(g, brightness);
  b = scaled_channel(b, brightness);
  w = scaled_channel(w, brightness);
  if (rgbw_enabled) {
    /* SK6812 wire order is GRBW. Station and alert callers supply W=0. */
    return ((uint32_t)g << 24) | ((uint32_t)r << 16) |
           ((uint32_t)b << 8) | w;
  }
  /* WS2812B wire order is GRB; the PIO shifts the upper 24 bits first. */
  uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  return grb << 8;
}

static uint8_t brightness_base(led_brightness_kind_t kind, uint8_t fallback) {
#ifdef LUFTFUGL_MONITOR
  if (kind < LED_BRIGHTNESS_KIND_COUNT)
    return brightness_override[kind];
#else
  (void)kind;
#endif
  return fallback;
}

static uint32_t colour_word_kind(uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                                 uint8_t base, led_brightness_kind_t kind) {
  uint8_t effective = brightness_base(kind, base);
#ifdef LUFTFUGL_MONITOR
  if (kind == LED_BRIGHTNESS_BREATHE)
    effective = (uint8_t)(((uint16_t)base * effective + 5u) / 10u);
#endif
  return colour_word_rgbw(r, g, b, w, effective);
}

static uint32_t colour_word(uint8_t r, uint8_t g, uint8_t b,
                            uint8_t brightness_percent) {
  return colour_word_kind(r, g, b, 0u, brightness_percent,
                          LED_BRIGHTNESS_STATION);
}

static uint32_t station5_rose(void) {
  return colour_word(LED_STATION5_R, LED_STATION5_G, LED_STATION5_B,
                     LED_STATION_BRIGHTNESS_PERCENT);
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

static uint32_t battery_warning_orange(void) {
  return colour_word_kind(LED_BATTERY_R, LED_BATTERY_G, LED_BATTERY_B, 0u,
                          LED_BATTERY_WARNING_BRIGHTNESS_PERCENT,
                          LED_BRIGHTNESS_WARNING);
}

static uint32_t battery_critical_orange(void) {
  return colour_word_kind(LED_BATTERY_R, LED_BATTERY_G, LED_BATTERY_B, 0u,
                          LED_BATTERY_CRITICAL_BRIGHTNESS_PERCENT,
                          LED_BRIGHTNESS_CRITICAL);
}

static bool battery_warning_flash_lit(uint32_t now) {
  uint32_t phase = now % LED_BATTERY_WARNING_PERIOD_MS;
  uint32_t second_flash =
      LED_BATTERY_WARNING_FLASH_MS + LED_BATTERY_WARNING_GAP_MS;
  return phase < LED_BATTERY_WARNING_FLASH_MS ||
         (phase >= second_flash &&
          phase < second_flash + LED_BATTERY_WARNING_FLASH_MS);
}

static uint32_t co2_warm_white_breathe(uint32_t now) {
  /* Eight-second, low-brightness breathing envelope.  The RGBW pixel uses
     its neutral white die with a small amber contribution for a soft,
     warm-white result; it never snaps fully dark between steps. */
  static const uint8_t level[] = LED_BREATHE_LEVELS;
  uint8_t brightness =
      level[(now / LED_BREATHE_STEP_MS) % (sizeof level / sizeof level[0])];
  if (!rgbw_enabled)
    return colour_word_kind(LED_WARM_RGB_R, LED_WARM_RGB_G, LED_WARM_RGB_B, 0u,
                            brightness, LED_BRIGHTNESS_BREATHE);
  return colour_word_kind(LED_WARM_RGBW_R, LED_WARM_RGBW_G, LED_WARM_RGBW_B,
                          LED_WARM_RGBW_W, brightness, LED_BRIGHTNESS_BREATHE);
}

static uint32_t co2_sample_warm_white(void) {
  /* Match the warm-up breath at its gentle 10% peak. */
  if (!rgbw_enabled)
    return colour_word_kind(LED_WARM_RGB_R, LED_WARM_RGB_G, LED_WARM_RGB_B, 0u,
                            LED_SAMPLE_BRIGHTNESS_PERCENT, LED_BRIGHTNESS_SAMPLE);
  return colour_word_kind(LED_WARM_RGBW_R, LED_WARM_RGBW_G, LED_WARM_RGBW_B,
                          LED_WARM_RGBW_W, LED_SAMPLE_BRIGHTNESS_PERCENT,
                          LED_BRIGHTNESS_SAMPLE);
}

static uint32_t co2_error_red(void) {
  return colour_word_kind(255u, 0u, 0u, 0u, LED_HAZARD_BRIGHTNESS_PERCENT,
                          LED_BRIGHTNESS_ERROR);
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
  uint32_t now = to_ms_since_boot(get_absolute_time());
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
  /* Critical battery indication has absolute priority.  Warning indication
     borrows only its brief pulse so the primary CO2 colour remains useful. */
  battery_state_t battery_state = power_monitor_battery_state();
  if (battery_state == BATTERY_STATE_CRITICAL)
    return hazard_lit() ? battery_critical_orange() : 0u;
  /* A low-battery warning must remain noticeable without hiding the CO2
     level that is the product's primary indication. */
  if (battery_state == BATTERY_STATE_WARNING &&
      battery_warning_flash_lit(now))
    return battery_warning_orange();
  /* Station 5 is a normal static CO2 colour outside a battery pulse. */
  if (mode == LED_MODE_AUTO && station == 5u)
    return station5_rose();
  if (mode == LED_MODE_FORCED_RAW)
    return rgbw_enabled ? raw_colour : raw_colour << 8;
  if (mode == LED_MODE_FORCED_ON)
    return station5_rose();
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

void led_power_init(void) {
  gpio_init(PIN_LED_POWER);
  gpio_pull_down(PIN_LED_POWER);
  gpio_put(PIN_LED_POWER, false);
  gpio_set_dir(PIN_LED_POWER, GPIO_OUT);
  power_enabled = false;
  pixel_lit = false;
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
  /* GP0 now supplies the pixel directly.  Its low startup state guarantees
     darkness without transmitting to an unpowered device. */
  last_colour = 0u;
#ifdef LUFTFUGL_MONITOR
  led_brightness_reset();
#endif
  led_update();
}

void led_update(void) {
  uint32_t colour = requested_colour();
  bool power_required = colour != 0u;
  if (!power_required) {
    /* GP0 is the supply: latch off before removing power from a lit pixel. */
    if (power_enabled && pixel_lit)
      led_transmit_frame(0u);
    led_data_disable();
    gpio_put(PIN_LED_POWER, false);
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
  if (colour == last_colour)
    return;
  led_transmit_frame(colour);
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
uint32_t led_colour(void) {
  /* UINT32_MAX invalidates the software cache while power settles; it is not
     a frame transmitted to the pixel and must not be reported as one. */
  return last_colour == UINT32_MAX ? 0u : last_colour;
}
uint led_pio_index(void) { return led_pio == pio0 ? 0u : 1u; }
uint led_state_machine(void) { return led_sm; }
uint led_program_offset(void) { return led_offset; }
