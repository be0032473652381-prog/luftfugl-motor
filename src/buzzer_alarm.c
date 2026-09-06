#include "buzzer_alarm.h"
#include "buzzer_alarm_pattern.h"
#include "buzzer.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

/* Deliberately separate from play-2, whose generator, renderer, DMA state,
 * and entry point stay unchanged during this temporary listening test. */
enum { ALARM_BUFFER_SAMPLES = 256, ALARM_BUFFER_RING_BITS = 10 };
static volatile int alarm_channels[2] = {-1, -1};
static volatile uint32_t alarm_buffers[2][ALARM_BUFFER_SAMPLES]
    __attribute__((aligned(ALARM_BUFFER_SAMPLES * sizeof(uint32_t))));
static volatile bool alarm_terminal[2];
static volatile bool alarm_active, alarm_finished, alarm_underrun;
static volatile alarm_synth_t alarm_synth;
static uint alarm_slice;

static void alarm_fill(unsigned int buffer) {
  /* A wholly silent terminal buffer accommodates PWM's buffered compare. */
  alarm_terminal[buffer] = alarm_synth_done(&alarm_synth);
  for (unsigned int i = 0u; i < ALARM_BUFFER_SAMPLES; ++i)
    alarm_buffers[buffer][i] = alarm_next_sample(&alarm_synth);
}

static void alarm_dma_irq(void) {
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)alarm_channels[i];
    if (!dma_channel_get_irq0_status(channel))
      continue;
    dma_channel_acknowledge_irq0(channel);
    if (!alarm_active || alarm_finished)
      continue;
    if (alarm_terminal[i]) {
      gpio_put(PIN_BUZZER_PWMB, false);
      alarm_finished = true;
      continue;
    }
    if (!dma_channel_is_busy((uint)alarm_channels[i ^ 1u])) {
      gpio_put(PIN_BUZZER_PWMB, false);
      alarm_underrun = true;
      alarm_finished = true;
      continue;
    }
    alarm_fill(i);
    dma_channel_set_read_addr(channel, (const void *)alarm_buffers[i], false);
    dma_channel_set_trans_count(channel, ALARM_BUFFER_SAMPLES, false);
  }
}

void buzzer_play_3_stop(void) {
  if (!alarm_active)
    return;
  uint32_t saved = save_and_disable_interrupts();
  gpio_put(PIN_BUZZER_PWMB, false);
  pwm_set_enabled(alarm_slice, false);
  alarm_active = false;
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)alarm_channels[i];
    dma_channel_set_irq0_enabled(channel, false);
    hw_clear_bits(&dma_hw->ch[channel].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
  }
  for (unsigned int i = 0u; i < 2u; ++i) {
    dma_channel_abort((uint)alarm_channels[i]);
    dma_channel_acknowledge_irq0((uint)alarm_channels[i]);
  }
  restore_interrupts(saved);
}

bool buzzer_play_3(unsigned int count) {
  /* Same count and battery-tone arbitration as buzzer_play_2(). */
  if (count < 1u || count > BUZZER_PLAY_MAX || buzzer_tone_sequence_active())
    return false;
  if (alarm_channels[0] < 0) {
    int first = dma_claim_unused_channel(false);
    if (first < 0)
      return false;
    int second = dma_claim_unused_channel(false);
    if (second < 0) {
      dma_channel_unclaim((uint)first);
      return false;
    }
    alarm_channels[0] = first;
    alarm_channels[1] = second;
    irq_add_shared_handler(DMA_IRQ_0, alarm_dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_0, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
  }
  /* Stop the current player through its existing off path before borrowing
   * the buzzer pins. This never changes channel A or the shared STBY pin. */
  buzzer_set(false);
  alarm_slice = pwm_gpio_to_slice_num(PIN_BUZZER_BIN1);
  if (!alarm_pattern_init(&alarm_synth, count, clock_get_hz(clk_sys)))
    return false;
  alarm_finished = false;
  alarm_underrun = false;
  alarm_fill(0u);
  alarm_fill(1u);
  uint16_t period = alarm_synth.period;
  pwm_set_clkdiv(alarm_slice, 1.0f);
  pwm_set_wrap(alarm_slice, (uint16_t)(period - 1u));
  pwm_set_counter(alarm_slice, 0u);
  pwm_set_output_polarity(alarm_slice, false, false);
  pwm_set_both_levels(alarm_slice, period, period);
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)alarm_channels[i];
    dma_channel_config config = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    /* A missed refill can repeat this buffer, never read beyond it. */
    channel_config_set_ring(&config, false, ALARM_BUFFER_RING_BITS);
    channel_config_set_dreq(&config, pwm_get_dreq(alarm_slice));
    channel_config_set_chain_to(&config, (uint)alarm_channels[i ^ 1u]);
    dma_channel_configure(channel, &config, &pwm_hw->slice[alarm_slice].cc,
                          (const void *)alarm_buffers[i], ALARM_BUFFER_SAMPLES,
                          false);
    dma_channel_acknowledge_irq0(channel);
    dma_channel_set_irq0_enabled(channel, true);
  }
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  alarm_active = true;
  dma_start_channel_mask(1u << alarm_channels[0]);
  gpio_put(PIN_BUZZER_PWMB, true);
  pwm_set_enabled(alarm_slice, true);
  return true;
}

/* Temporary diagnostic sibling: normal play-3 composition is unchanged. */
bool buzzer_tone_3(uint32_t frequency_hz, uint32_t duration_ms) {
  /* Same fixed-tone validation and battery arbitration as tone-2. */
  if (frequency_hz < BATTERY_ALERT_FREQUENCY_MIN_HZ ||
      frequency_hz > BATTERY_ALERT_FREQUENCY_MAX_HZ ||
      duration_ms < BATTERY_CHIRP_DURATION_MIN_S * 1000u ||
      duration_ms > BATTERY_CHIRP_DURATION_MAX_S * 1000u ||
      buzzer_tone_sequence_active())
    return false;
  if (alarm_channels[0] < 0) {
    int first = dma_claim_unused_channel(false);
    if (first < 0)
      return false;
    int second = dma_claim_unused_channel(false);
    if (second < 0) {
      dma_channel_unclaim((uint)first);
      return false;
    }
    alarm_channels[0] = first;
    alarm_channels[1] = second;
    irq_add_shared_handler(DMA_IRQ_0, alarm_dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_0, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
  }
  /* Stop the current player through its existing off path before borrowing
   * the buzzer pins. This never changes channel A or the shared STBY pin. */
  buzzer_set(false);
  alarm_slice = pwm_gpio_to_slice_num(PIN_BUZZER_BIN1);
  if (!alarm_tone_init(&alarm_synth, frequency_hz, duration_ms, clock_get_hz(clk_sys)))
    return false;
  alarm_finished = false;
  alarm_underrun = false;
  alarm_fill(0u);
  alarm_fill(1u);
  uint16_t period = alarm_synth.period;
  pwm_set_clkdiv(alarm_slice, 1.0f);
  pwm_set_wrap(alarm_slice, (uint16_t)(period - 1u));
  pwm_set_counter(alarm_slice, 0u);
  pwm_set_output_polarity(alarm_slice, false, false);
  pwm_set_both_levels(alarm_slice, period, period);
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)alarm_channels[i];
    dma_channel_config config = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    /* A missed refill can repeat this buffer, never read beyond it. */
    channel_config_set_ring(&config, false, ALARM_BUFFER_RING_BITS);
    channel_config_set_dreq(&config, pwm_get_dreq(alarm_slice));
    channel_config_set_chain_to(&config, (uint)alarm_channels[i ^ 1u]);
    dma_channel_configure(channel, &config, &pwm_hw->slice[alarm_slice].cc,
                          (const void *)alarm_buffers[i], ALARM_BUFFER_SAMPLES,
                          false);
    dma_channel_acknowledge_irq0(channel);
    dma_channel_set_irq0_enabled(channel, true);
  }
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  alarm_active = true;
  dma_start_channel_mask(1u << alarm_channels[0]);
  gpio_put(PIN_BUZZER_PWMB, true);
  pwm_set_enabled(alarm_slice, true);
  return true;
}

void buzzer_play_3_tick(void) {
  if (alarm_active && alarm_finished)
    buzzer_set(false);
}

bool buzzer_play_3_active(void) { return alarm_active; }
bool buzzer_play_3_underrun(void) { return alarm_underrun; }
