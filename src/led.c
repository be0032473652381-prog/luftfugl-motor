#include "led.h"

#include "config.h"
#include "encoder.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

#include <limits.h>

static PIO led_pio;
static uint led_sm, led_offset;
static uint32_t last_colour;
static led_mode_t mode;

static uint32_t dusty_rose(void) {
  /* WS2812B wire order is GRB; the PIO shifts the upper 24 bits first. */
  uint32_t grb = ((uint32_t)LED_STATION5_G << 16) |
                 ((uint32_t)LED_STATION5_R << 8) | LED_STATION5_B;
  return grb << 8;
}

static bool requested_on(void) {
  if (mode == LED_MODE_FORCED_ON)
    return true;
  if (mode == LED_MODE_FORCED_OFF)
    return false;
  return encoder_confirmed() == POS_MAX;
}

void led_init(void) {
  led_pio = pio0;
  led_sm = pio_claim_unused_sm(led_pio, true);
  led_offset = pio_add_program(led_pio, &ws2812_program);
  ws2812_program_init(led_pio, led_sm, led_offset, PIN_LED_DATA, 800000.0f,
                      false);
  mode = LED_MODE_AUTO;
  last_colour = UINT32_MAX;
  led_update();
}

void led_update(void) {
  uint32_t colour = requested_on() ? dusty_rose() : 0u;
  if (colour == last_colour)
    return;
  pio_sm_put_blocking(led_pio, led_sm, colour);
  last_colour = colour;
}

void led_set_mode(led_mode_t new_mode) {
  mode = new_mode;
  led_update();
}

led_mode_t led_mode(void) { return mode; }
bool led_is_on(void) { return last_colour == dusty_rose(); }
uint led_pio_index(void) { return led_pio == pio0 ? 0u : 1u; }
uint led_state_machine(void) { return led_sm; }
uint led_program_offset(void) { return led_offset; }
