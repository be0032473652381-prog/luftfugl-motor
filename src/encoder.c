#include "encoder.h"
#include "hardware/adc.h"

static uint16_t samples[FILTER_DEPTH];
static uint32_t sample_sum;
static uint8_t sample_index;
static uint16_t stable_ms;
static volatile uint16_t raw_value, average_value;
static volatile position_t instant_position, confirmed_position;
static volatile bool confirmed_changed;
#ifdef LUFTFUGL_DEBUG
static volatile bool sim_active;
static volatile uint16_t sim_value;
#endif

static position_t classify(uint16_t value)
{
    if (value <= CFG_BAND_P1_MAX) return 1;
    if (value <= CFG_BAND_P2_MAX) return 2;
    if (value <= CFG_BAND_P3_MAX) return 3;
    if (value <= CFG_BAND_P4_MAX) return 4;
    if (value <= CFG_BAND_P5_MAX) return 5;
    return POS_UNKNOWN;
}

void encoder_init(void)
{
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
    instant_position = classify(average_value);
    confirmed_position = instant_position;
    stable_ms = CFG_DEBOUNCE_MS;
    confirmed_changed = false;
#ifdef LUFTFUGL_DEBUG
    sim_active = false; sim_value = DEBUG_SIM_DEFAULT_ADC;
#endif
}

void encoder_tick(void)
{
#ifdef LUFTFUGL_DEBUG
    if (encoder_sim_active()) raw_value = encoder_sim_value(); else
#endif
    raw_value = adc_read();
    sample_sum -= samples[sample_index];
    samples[sample_index] = raw_value;
    sample_sum += raw_value;
    sample_index = (uint8_t)((sample_index + 1u) % FILTER_DEPTH);
    average_value = (uint16_t)(sample_sum / FILTER_DEPTH);
    position_t classified = classify(average_value);
    if (classified != instant_position) {
        instant_position = classified;
        stable_ms = 1;
    } else if (stable_ms < UINT16_MAX) {
        ++stable_ms;
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
bool encoder_take_change(position_t *out)
{
    if (!confirmed_changed) return false;
    *out = confirmed_position;
    confirmed_changed = false;
    return true;
}
#ifdef LUFTFUGL_DEBUG
void encoder_sim_enable(bool on) { sim_active = on; }
bool encoder_sim_active(void) { return sim_active; }
void encoder_sim_set(uint16_t adc) { sim_value = adc > ADC_MAX_VALUE ? ADC_MAX_VALUE : adc; }
uint16_t encoder_sim_value(void) { return sim_value; }
#endif
