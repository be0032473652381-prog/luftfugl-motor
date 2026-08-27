#include "buzzer.h"

#include "config.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/time.h"
#include <limits.h>

#define BIRD_MAX_MS 650u
#define BIRD_PERIOD_MS 2000u
#define BIRD_GAP_MS 200u
#define BIRD_EVENT_MAX 192u

typedef struct {
  uint16_t frequency;
  uint16_t duration_ms;
  bool on;
} bird_event_t;

static uint buzzer_slice;
static bool enabled;
static bird_event_t events[BIRD_EVENT_MAX];
static uint16_t event_count;
static uint16_t event_index;
static uint16_t applied_index;
static uint32_t sequence_start;
static uint16_t sequence_ms;
static uint8_t plays_left;
static bool finite_play;
static uint32_t rng = 0xA5A5A5A5u;

static void buzzer_gpio_off(void) {
  pwm_set_enabled(buzzer_slice, false);
  pwm_set_output_polarity(buzzer_slice, false, false);
  pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, 0u);
  pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, 0u);
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_SIO);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_BUZZER_BIN1, GPIO_OUT);
  gpio_set_dir(PIN_BUZZER_BIN2, GPIO_OUT);
  gpio_put(PIN_BUZZER_BIN1, false);
  gpio_put(PIN_BUZZER_BIN2, false);
  gpio_put(PIN_BUZZER_PWMB, false);
}

static uint32_t rng_next(void) {
  uint32_t x = rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng = x;
  return x;
}

static int16_t rng_range(int16_t low, int16_t high) {
  return (int16_t)(low + (int16_t)(rng_next() % (uint32_t)(high - low + 1)));
}

static void append_event(uint16_t frequency, uint16_t duration_ms, bool on) {
  if (event_count >= BIRD_EVENT_MAX || sequence_ms >= BIRD_MAX_MS)
    return;
  if (duration_ms > BIRD_MAX_MS - sequence_ms)
    duration_ms = BIRD_MAX_MS - sequence_ms;
  if (duration_ms == 0u)
    return;
  events[event_count++] = (bird_event_t){frequency, duration_ms, on};
  sequence_ms = (uint16_t)(sequence_ms + duration_ms);
}

static void build_bird(void) {
  event_count = 0u;
  event_index = 0u;
  applied_index = UINT16_MAX;
  sequence_ms = 0u;

  uint8_t scream_count = (uint8_t)rng_range(3, 5);
  for (uint8_t k = 0u; k < scream_count && sequence_ms < BIRD_MAX_MS; ++k) {
    int16_t start = (int16_t)(3600 + rng_range(-180, 220));
    int16_t peak = (int16_t)(8000 + rng_range(-350, 400));
    for (int16_t f = start; f <= peak && sequence_ms < BIRD_MAX_MS;
         f += (int16_t)(420 + rng_range(-80, 80)))
      append_event((uint16_t)(f + rng_range(-120, 120)),
                   (uint16_t)rng_range(5, 9), true);
    for (int16_t f = peak; f >= start + 900 && sequence_ms < BIRD_MAX_MS;
         f -= (int16_t)(900 + rng_range(-150, 150)))
      append_event((uint16_t)(f + rng_range(-180, 180)),
                   (uint16_t)rng_range(4, 7), true);
    append_event(0u, (uint16_t)rng_range(10, 18), false);
    if ((rng_next() & 7u) == 0u)
      append_event(0u, (uint16_t)rng_range(18, 45), false);
  }

  uint8_t rattle_count = (uint8_t)rng_range(10, 14);
  for (uint8_t i = 0u; i < rattle_count && sequence_ms < BIRD_MAX_MS; ++i) {
    uint16_t band_a = (uint16_t)(6200 + rng_range(-420, 420));
    uint16_t band_b = (uint16_t)(8400 + rng_range(-520, 260));
    append_event((i & 1u) ? band_b : band_a, (uint16_t)rng_range(10, 16), true);
    append_event(0u, (uint16_t)rng_range(6, 12), false);
  }

  uint8_t burst_count = (uint8_t)rng_range(10, 16);
  for (uint8_t j = 0u; j < burst_count && sequence_ms < BIRD_MAX_MS; ++j) {
    uint16_t frequency = (uint16_t)rng_range(4200, 9000);
    uint16_t on_ms = (uint16_t)rng_range(6, 14);
    append_event((uint16_t)(frequency + rng_range(-220, 220)),
                 (uint16_t)(on_ms / 2u), true);
    append_event((uint16_t)(frequency + rng_range(-220, 220)),
                 (uint16_t)(on_ms - on_ms / 2u), true);
    append_event(0u, (uint16_t)rng_range(5, 14), false);
    if ((rng_next() & 0x3fu) == 0u)
      append_event(0u, (uint16_t)rng_range(25, 70), false);
  }

  int16_t tail_start = (int16_t)(7200 + rng_range(-260, 260));
  int16_t tail_end = (int16_t)(3600 + rng_range(-260, 260));
  for (int16_t f = tail_start; f >= tail_end && sequence_ms < BIRD_MAX_MS;
       f -= (int16_t)(360 + rng_range(-60, 60)))
    append_event((uint16_t)(f + rng_range(-140, 140)),
                 (uint16_t)rng_range(7, 12), true);
}

static void buzzer_apply_frequency(uint16_t frequency) {
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  gpio_put(PIN_BUZZER_PWMB, true);
  uint32_t sys_hz = clock_get_hz(clk_sys);
  float divider = (float)sys_hz / ((float)frequency * 65535.0f);
  if (divider < 1.0f)
    divider = 1.0f;
  if (divider > 255.0f)
    divider = 255.0f;
  uint32_t top_plus_one = (uint32_t)((float)sys_hz /
                                     (divider * (float)frequency) + 0.5f);
  if (top_plus_one < 2u)
    top_plus_one = 2u;
  if (top_plus_one > 65536u)
    top_plus_one = 65536u;
  uint16_t top = (uint16_t)(top_plus_one - 1u);
  uint16_t level = (uint16_t)(top_plus_one / 2u);
  pwm_set_enabled(buzzer_slice, false);
  pwm_set_clkdiv(buzzer_slice, divider);
  pwm_set_wrap(buzzer_slice, top);
  pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, level);
  pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, level);
  pwm_set_output_polarity(buzzer_slice, false, true);
  pwm_set_enabled(buzzer_slice, true);
}

void buzzer_init(void) {
  gpio_init(PIN_BUZZER_PWMB);
  gpio_set_dir(PIN_BUZZER_PWMB, GPIO_OUT);
  gpio_set_drive_strength(PIN_BUZZER_BIN1, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_drive_strength(PIN_BUZZER_BIN2, GPIO_DRIVE_STRENGTH_12MA);
  buzzer_slice = pwm_gpio_to_slice_num(PIN_BUZZER_BIN1);
  pwm_config config = pwm_get_default_config();
  pwm_init(buzzer_slice, &config, false);
  enabled = false;
  buzzer_gpio_off();
}

void buzzer_set(bool on) {
  enabled = on;
  if (!on) {
    buzzer_gpio_off();
    return;
  }
  finite_play = false;
  plays_left = 0u;
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  gpio_put(PIN_BUZZER_PWMB, true);
  sequence_start = to_ms_since_boot(get_absolute_time());
  event_count = 0u;
  sequence_ms = 0u;
  event_index = 0u;
  applied_index = UINT16_MAX;
}

void buzzer_play(unsigned int count) {
  if (count < 1u)
    count = 1u;
  if (count > 10u)
    count = 10u;
  enabled = true;
  finite_play = true;
  plays_left = (uint8_t)count;
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  gpio_put(PIN_BUZZER_PWMB, true);
  sequence_start = to_ms_since_boot(get_absolute_time());
  event_count = 0u;
  sequence_ms = 0u;
  event_index = 0u;
  applied_index = UINT16_MAX;
}

void buzzer_tick(void) {
  if (!enabled)
    return;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (event_count == 0u) {
    sequence_start = now;
    build_bird();
  }
  uint32_t elapsed = now - sequence_start;
  if (elapsed >= sequence_ms) {
    if (finite_play) {
      if (plays_left <= 1u) {
        buzzer_gpio_off();
        enabled = false;
        event_count = 0u;
        return;
      }
      if (elapsed < (uint32_t)sequence_ms + BIRD_GAP_MS) {
        buzzer_gpio_off();
        return;
      }
      --plays_left;
      sequence_start = now;
      build_bird();
      elapsed = 0u;
    } else {
      if (elapsed < BIRD_PERIOD_MS) {
        buzzer_gpio_off();
        return;
      }
      sequence_start = now;
      build_bird();
      elapsed = 0u;
    }
  }
  uint32_t cursor = 0u;
  event_index = 0u;
  while (event_index < event_count &&
         elapsed >= cursor + events[event_index].duration_ms) {
    cursor += events[event_index].duration_ms;
    ++event_index;
  }
  if (event_index >= event_count || !events[event_index].on) {
    if (applied_index != event_index) {
      buzzer_gpio_off();
      applied_index = event_index;
    }
    return;
  }
  if (applied_index != event_index) {
    buzzer_apply_frequency(events[event_index].frequency);
    applied_index = event_index;
  }
}

bool buzzer_enabled(void) { return enabled; }
