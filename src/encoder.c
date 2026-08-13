#include "encoder.h"
#include "hardware/adc.h"

static uint16_t samples[FILTER_DEPTH];
static uint8_t sample_index, sample_ticks;
static uint16_t stable_ms;
static volatile uint16_t raw_value, average_value;
static volatile position_t instant_position, confirmed_position;
static volatile bool confirmed_changed;
#ifdef LUFTFUGL_DEBUG
static volatile bool sim_active;
static volatile uint16_t sim_value;
#else
static uint16_t position_adc[] = {POS_1_ADC, POS_2_ADC, POS_3_ADC, POS_4_ADC,
                                  POS_5_ADC};
#endif

static uint16_t trimmed_average(void) {
  uint32_t sum = 0u;
  uint16_t minimum = UINT16_MAX, maximum = 0u;
  for (uint8_t i = 0u; i < FILTER_DEPTH; ++i) {
    uint16_t sample = samples[i];
    sum += sample;
    if (sample < minimum)
      minimum = sample;
    if (sample > maximum)
      maximum = sample;
  }
  return (uint16_t)((sum - minimum - maximum) / (FILTER_DEPTH - 2u));
}

uint16_t encoder_nominal(position_t position) {
#ifdef LUFTFUGL_DEBUG
  const volatile uint16_t *active = &cfg.pos_1_adc;
  return position >= POS_MIN && position <= POS_MAX ? active[position - POS_MIN]
                                                    : 0u;
#else
  return position >= POS_MIN && position <= POS_MAX
             ? position_adc[position - POS_MIN]
             : 0u;
#endif
}

void encoder_set_nominal(position_t position, uint16_t adc) {
  if (position < POS_MIN || position > POS_MAX)
    return;
#ifdef LUFTFUGL_DEBUG
  volatile uint16_t *active = &cfg.pos_1_adc;
  active[position - POS_MIN] = adc;
#else
  position_adc[position - POS_MIN] = adc;
#endif
}

void encoder_reset_nominals(void) {
#ifdef LUFTFUGL_DEBUG
  cfg.pos_1_adc = POS_1_ADC;
  cfg.pos_2_adc = POS_2_ADC;
  cfg.pos_3_adc = POS_3_ADC;
  cfg.pos_4_adc = POS_4_ADC;
  cfg.pos_5_adc = POS_5_ADC;
#else
  static const uint16_t defaults[] = {POS_1_ADC, POS_2_ADC, POS_3_ADC,
                                      POS_4_ADC, POS_5_ADC};
  for (position_t position = POS_MIN; position <= POS_MAX; ++position)
    position_adc[position - POS_MIN] = defaults[position - POS_MIN];
#endif
}

static position_t position_at(uint16_t value) {
  for (position_t position = POS_MIN; position <= POS_MAX; ++position) {
    uint16_t nominal = encoder_nominal(position);
    uint16_t delta = value > nominal ? value - nominal : nominal - value;
    if (delta <= CFG_POS_WINDOW)
      return position;
  }
  return POS_BETWEEN;
}

void encoder_init(void) {
  adc_init();
  adc_gpio_init(PIN_SENSE);
  adc_select_input(ADC_CHANNEL);
  sample_index = 0;
  sample_ticks = 0u;
  for (uint8_t i = 0; i < FILTER_DEPTH; ++i) {
    samples[i] = adc_read();
  }
  raw_value = samples[FILTER_DEPTH - 1u];
  average_value = trimmed_average();
  instant_position = position_at(average_value);
  confirmed_position = instant_position;
  stable_ms = CFG_DEBOUNCE_MS;
  confirmed_changed = false;
#ifdef LUFTFUGL_DEBUG
  sim_active = false;
  sim_value = DEBUG_SIM_DEFAULT_ADC;
#endif
}

void encoder_tick(void) {
  if (++sample_ticks < ADC_SAMPLE_PERIOD_TICKS)
    return;
  sample_ticks = 0u;
#ifdef LUFTFUGL_DEBUG
  if (encoder_sim_active())
    raw_value = encoder_sim_value();
  else
#endif
    raw_value = adc_read();
  samples[sample_index] = raw_value;
  sample_index = (uint8_t)((sample_index + 1u) % FILTER_DEPTH);
  average_value = trimmed_average();
  position_t classified = position_at(average_value);
  if (classified != instant_position) {
    instant_position = classified;
    stable_ms = ADC_SAMPLE_PERIOD_MS;
  } else if (stable_ms <= UINT16_MAX - ADC_SAMPLE_PERIOD_MS) {
    stable_ms += ADC_SAMPLE_PERIOD_MS;
  }
  if (stable_ms >= CFG_DEBOUNCE_MS && confirmed_position != instant_position) {
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
int16_t encoder_error_to(position_t target) {
  return (int16_t)encoder_nominal(target) - (int16_t)average_value;
}
#ifdef LUFTFUGL_DEBUG
void encoder_sim_enable(bool on) { sim_active = on; }
bool encoder_sim_active(void) { return sim_active; }
void encoder_sim_set(uint16_t adc) {
  sim_value = adc > ADC_MAX_VALUE ? ADC_MAX_VALUE : adc;
}
uint16_t encoder_sim_value(void) { return sim_value; }
#endif
