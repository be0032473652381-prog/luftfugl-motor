#ifndef LUFTFUGL_LED_H
#define LUFTFUGL_LED_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  LED_MODE_AUTO = 0,
  LED_MODE_FORCED_ON,
  LED_MODE_FORCED_OFF,
  LED_MODE_FORCED_RAW
} led_mode_t;

void led_power_init(void);
void led_init(void);
void led_update(void);
void led_set_mode(led_mode_t mode);
led_mode_t led_mode(void);
bool led_is_on(void);
bool led_rgbw(void);
void led_set_rgbw(bool enabled);
void led_set_raw(uint32_t wire_word);
bool led_powered(void);
uint32_t led_colour(void);
unsigned int led_pio_index(void);
unsigned int led_state_machine(void);
unsigned int led_program_offset(void);

#endif
