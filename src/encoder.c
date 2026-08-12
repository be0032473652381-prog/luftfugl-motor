#include "encoder.h"
#include "hardware/adc.h"

static uint16_t samples[FILTER_DEPTH];
static uint32_t sample_sum;
static uint8_t sample_index;
static uint16_t stable_ms;
static volatile uint16_t raw_value, average_value;
static volatile position_t instant_position, confirmed_position;
static volatile bool confirmed_changed;
static uint16_t position_adc[] = {POS_1_ADC, POS_2_ADC, POS_3_ADC, POS_4_ADC,
                                  POS_5_ADC};

uint16_t encoder_nominal(position_t position) {
  return position >= POS_MIN && position <= POS_MAX
             ? position_adc[position - POS_MIN]
             : 0u;
}

void encoder_set_nominal(position_t position, uint16_t adc) {
  if (position < POS_MIN || position > POS_MAX)
    return;
  position_adc[position - POS_MIN] = adc;
}

void encoder_reset_nominals(void) {
  static const uint16_t defaults[] = {POS_1_ADC, POS_2_ADC, POS_3_ADC,
                                      POS_4_ADC, POS_5_ADC};
  for (position_t position = POS_MIN; position <= POS_MAX; ++position)
    position_adc[position - POS_MIN] = defaults[position - POS_MIN];
}

static position_t position_at(uint16_t value) {
  for (position_t position = POS_MIN; position <= POS_MAX; ++position) {
    uint16_t nominal = encoder_nominal(position);
    uint16_t delta = value > nominal ? value - nominal : nominal - value;
    if (delta <= POS_WINDOW)
      return position;
  }
  return POS_BETWEEN;
}

void encoder_init(void) {
  adc_init();
  adc_gpio_init(PIN_SENSE);
  adc_select_input(ADC_CHANNEL);
  sample_sum = 0;
  sample_index = 0;
  for (uint8_t i = 0; i < FILTER_DEPTH; ++i) {
    samples[i] = adc_read();
    sample_sum += samples[i];
  }
  raw_value = samples[FILTER_DEPTH - 1u];
  average_value = (uint16_t)(sample_sum / FILTER_DEPTH);
  instant_position =
      encoder_in_safe_range() ? position_at(average_value) : POS_UNKNOWN;
  confirmed_position = instant_position;
  stable_ms = DEBOUNCE_MS;
  confirmed_changed = false;
}

void encoder_tick(void) {
    raw_value = adc_read();
  sample_sum -= samples[sample_index];
  samples[sample_index] = raw_value;
  sample_sum += raw_value;
  sample_index = (uint8_t)((sample_index + 1u) % FILTER_DEPTH);
  average_value = (uint16_t)(sample_sum / FILTER_DEPTH);
  position_t classified =
      encoder_in_safe_range() ? position_at(average_value) : POS_UNKNOWN;
  if (classified != instant_position) {
    instant_position = classified;
    stable_ms = 1;
  } else if (stable_ms < UINT16_MAX) {
    ++stable_ms;
  }
  if (stable_ms >= DEBOUNCE_MS && confirmed_position != instant_position) {
    confirmed_position = instant_position;
    confirmed_changed = true;
  }
}

uint16_t encoder_raw(void) { return raw_value; }
uint16_t encoder_average(void) { return average_value; }
position_t encoder_instant(void) { return instant_position; }
position_t encoder_confirmed(void) { return confirmed_position; }
bool encoder_take_change(position_t *out) {
  if (!confirmed_changed)
    return false;
  *out = confirmed_position;
  confirmed_changed = false;
  return true;
}
bool encoder_in_safe_range(void) {
  return average_value >= ADC_SAFE_MIN && average_value <= ADC_SAFE_MAX;
}
int16_t encoder_error_to(position_t target) {
  return (int16_t)encoder_nominal(target) - (int16_t)average_value;
}
