#!/usr/bin/env python3
"""Exercise actual LED/ambient modules, including PIO and power ordering."""
from pathlib import Path
import subprocess
import tempfile
from test_ambient_light import ROOT, STUBS, HARNESS

LED_STUBS = {
    **STUBS,
    'pico/time.h': '''
#ifndef TEST_TIME_H
#define TEST_TIME_H
#include <stdint.h>
typedef uint64_t absolute_time_t;
uint64_t time_us_64(void);
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t);
void sleep_us(uint64_t);
#endif
''',
    'hardware/gpio.h': '''
#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <stdbool.h>
#define GPIO_OUT 1
void gpio_init(unsigned int);
void gpio_put(unsigned int, bool);
void gpio_set_dir(unsigned int, bool);
void gpio_pull_down(unsigned int);
#endif
''',
    'hardware/pio.h': '''
#ifndef TEST_PIO_H
#define TEST_PIO_H
#include <stdint.h>
#include <stdbool.h>
typedef unsigned int uint;
typedef struct { int unused; } *PIO;
extern PIO pio0;
uint pio_claim_unused_sm(PIO, bool);
uint pio_add_program(PIO, const void *);
void pio_sm_set_enabled(PIO, uint, bool);
void pio_sm_put_blocking(PIO, uint, uint32_t);
#endif
''',
    'ws2812.pio.h': '''
#include "hardware/pio.h"
extern const int ws2812_program;
void ws2812_program_init(PIO, uint, uint, uint, float, bool);
''',
}

LED_HARNESS = r'''
#include "led.h"
#include "power_monitor.h"
#include "hardware/pio.h"
#include "pico/time.h"

static bool gpio_power, sm_enabled;
static uint32_t wire;
static unsigned transmissions;
static uint16_t position_adc;
static sys_state_t controller = ST_IDLE;
static battery_state_t battery;
static bool co2_error, co2_warm, co2_valid = true, co2_flash;
static struct { int unused; } pio;
PIO pio0 = (PIO)&pio;
const int ws2812_program = 1;
absolute_time_t get_absolute_time(void) { return now; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }
void sleep_us(uint64_t us) { now += us; }
void gpio_init(unsigned pin) { assert(pin == PIN_LED_POWER || pin == PIN_LED_DATA); }
void gpio_put(unsigned pin, bool value) {
  if (pin == PIN_LED_POWER) {
    if (!value && gpio_power) assert(wire == 0 && !sm_enabled);
    gpio_power = value;
  } else assert(pin == PIN_LED_DATA && !value);
}
void gpio_set_dir(unsigned pin, bool output) { (void)pin; assert(output); }
void gpio_pull_down(unsigned pin) { assert(pin == PIN_LED_POWER); }
uint pio_claim_unused_sm(PIO port, bool required) { assert(port == pio0 && required); return 0; }
uint pio_add_program(PIO port, const void *program) { (void)program; assert(port == pio0); return 0; }
void pio_sm_set_enabled(PIO port, uint sm, bool enabled) {
  assert(port == pio0 && sm == 0); sm_enabled = enabled;
}
void pio_sm_put_blocking(PIO port, uint sm, uint32_t colour) {
  assert(port == pio0 && sm == 0 && gpio_power && sm_enabled);
  wire = colour; ++transmissions;
}
void ws2812_program_init(PIO port, uint sm, uint offset, uint pin, float hz, bool rgbw) {
  (void)offset; (void)rgbw;
  assert(port == pio0 && sm == 0 && pin == PIN_LED_DATA && hz == 800000);
  sm_enabled = true;
}
sys_state_t controller_state(void) { return controller; }
uint16_t encoder_average(void) { return position_adc; }
uint16_t encoder_nominal(position_t position) {
  static const uint16_t adc[] = {0, POS_1_ADC, POS_2_ADC, POS_3_ADC,
                                POS_4_ADC, POS_5_ADC, POS_6_ADC};
  assert(position >= 1 && position <= 6); return adc[position];
}
battery_state_t power_monitor_battery_state(void) { return battery; }
bool co2_sensor_error(void) { return co2_error; }
bool co2_warming_up(void) { return co2_warm; }
bool co2_filtered_valid(void) { return co2_valid; }
bool co2_sample_flash_active(void) { return co2_flash; }
static uint32_t render(void) {
  led_update(); now += LED_POWER_STARTUP_US; led_update();
  assert(!sm_enabled); return led_colour();
}
static uint32_t expected(unsigned r, unsigned g, unsigned b, unsigned w,
                         unsigned base, unsigned multiplier, bool rgbw) {
  double fraction = base * multiplier / 10000.0;
  if (fraction > 1.0) fraction = 1.0;
  r = (unsigned)(r * fraction + 0.5);
  g = (unsigned)(g * fraction + 0.5);
  b = (unsigned)(b * fraction + 0.5);
  w = (unsigned)(w * fraction + 0.5);
  return (g << 24) | (r << 16) | (b << 8) | (rgbw ? w : 0);
}
static void phase_ms(unsigned phase) {
  /* Advance monotonically to a known point in all existing indication cycles. */
  now = ((now / 120000000) + 1) * 120000000 + (uint64_t)phase * 1000;
}
int main(void) {
  ambient_light_init();
  position_adc = POS_1_ADC;
  led_power_init(); led_init();
  static const uint16_t raw[] = {0, 100, 500, 2000};
  static const unsigned scales[] = {100, 200, 400, 800};
  static const unsigned colours[][3] = {
      {0,224,24}, {96,144,8}, {160,48,0}, {160,24,48}, {192,4,8}};
  static const unsigned breath[] = LED_BREATHE_LEVELS;
  for (unsigned zone = 0; zone < 4; ++zone) {
    for (unsigned n = 0; n < AMBIENT_CONFIRM_SAMPLES; ++n) measure(raw[zone]);
    assert(led_brightness_multiplier_percent() == scales[zone]);
    for (unsigned rgbw = 0; rgbw < 2; ++rgbw) {
      led_set_rgbw(rgbw != 0);
      for (unsigned station = 1; station <= 5; ++station) {
        position_adc = encoder_nominal(station);
        const unsigned *c = colours[station - 1];
        assert(render() == expected(c[0], c[1], c[2], 0, 3, scales[zone], rgbw));
        unsigned before = transmissions;
        assert(render() == led_colour());
        assert(transmissions == before); /* unchanged frame does not wake PIO */
      }
      position_adc = POS_6_ADC;
      battery = BATTERY_STATE_WARNING; phase_ms(0);
      assert(render() == expected(255,48,0,0,30,scales[zone],rgbw));
      battery = BATTERY_STATE_CRITICAL; phase_ms(0);
      assert(render() == expected(255,48,0,0,30,scales[zone],rgbw));
      phase_ms(160); assert(render() == 0 && !gpio_power);
      battery = BATTERY_STATE_NORMAL;
      co2_error = true;
      assert(render() == expected(255,0,0,0,30,scales[zone],rgbw));
      co2_error = false; co2_warm = true;
      for (unsigned step = 0; step < sizeof breath / sizeof breath[0]; ++step) {
        phase_ms(step * 250);
        assert(render() == (rgbw ? expected(32,14,0,255,breath[step],scales[zone],true)
                                 : expected(255,178,96,0,breath[step],scales[zone],false)));
      }
      co2_warm = false; co2_valid = false; co2_flash = true;
      assert(render() == (rgbw ? expected(32,14,0,255,10,scales[zone],true)
                               : expected(255,178,96,0,10,scales[zone],false)));
      co2_flash = false; assert(render() == 0 && !gpio_power);
      co2_valid = true;
    }
  }
  /* Gates still override raw/forced-on tests and scaled alert indications. */
  led_set_rgbw(true); position_adc = POS_2_ADC;
  led_set_raw(0x11223344); assert(render() == 0x11223344);
  controller = ST_MOVING; assert(render() == 0 && !gpio_power);
  controller = ST_IDLE; position_adc = (POS_1_ADC + POS_2_ADC) / 2;
  assert(render() == 0 && !gpio_power);
  position_adc = POS_2_ADC; led_set_mode(LED_MODE_FORCED_ON);
  assert(render() == expected(192,4,8,0,3,800,true));
  battery = BATTERY_STATE_CRITICAL; phase_ms(0);
  led_set_mode(LED_MODE_FORCED_OFF); assert(render() == 0 && !gpio_power);
  puts("PASS: all five stations, battery warning/critical, CO2 error, 32 breathing steps and sample flash at all four scales in RGB/RGBW; night values, saturation, raw mode, motion/off gates, PIO cache and off-before-power-down");
  return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        for name, contents in LED_STUBS.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        harness = temp / 'led_test.c'
        harness.write_text(HARNESS[:HARNESS.index('int main(void)')] + LED_HARNESS)
        executable = temp / 'led_test'
        for debug in (False, True):
            command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-g', '-O1',
                       '-I' + str(temp), '-I' + str(ROOT / 'src')]
            if debug:
                command += ['-DLUFTFUGL_DEBUG=1', '-DLUFTFUGL_MONITOR=1']
            subprocess.run(command + [str(harness), str(ROOT / 'src/ambient_light.c'),
                                      str(ROOT / 'src/led.c'), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
