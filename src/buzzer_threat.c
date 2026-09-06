#include "buzzer_threat.h"
#include "buzzer_threat_pattern.h"
#include "buzzer.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

/* Deliberately separate from play-2 and play-3, whose generator, renderer, DMA state,
 * and entry point stay unchanged during this temporary listening test. */
enum { THREAT_BUFFER_SAMPLES = 256, THREAT_BUFFER_RING_BITS = 10 };
static volatile int threat_channels[2] = {-1, -1};
static volatile uint32_t threat_buffers[2][THREAT_BUFFER_SAMPLES]
    __attribute__((aligned(THREAT_BUFFER_SAMPLES * sizeof(uint32_t))));
static volatile bool threat_terminal[2];
static volatile bool threat_active, threat_finished, threat_underrun;
static volatile threat_synth_t threat_synth;
static uint threat_slice;

static void threat_fill(unsigned int buffer) {
  /* A wholly silent terminal buffer accommodates PWM's buffered compare. */
  threat_terminal[buffer] = threat_synth_done(&threat_synth);
  for (unsigned int i = 0u; i < THREAT_BUFFER_SAMPLES; ++i)
    threat_buffers[buffer][i] = threat_next_sample(&threat_synth);
}

static void threat_dma_irq(void) {
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)threat_channels[i];
    if (!dma_channel_get_irq0_status(channel))
      continue;
    dma_channel_acknowledge_irq0(channel);
    if (!threat_active || threat_finished)
      continue;
    if (threat_terminal[i]) {
      gpio_put(PIN_BUZZER_PWMB, false);
      threat_finished = true;
      continue;
    }
    if (!dma_channel_is_busy((uint)threat_channels[i ^ 1u])) {
      gpio_put(PIN_BUZZER_PWMB, false);
      threat_underrun = true;
      threat_finished = true;
      continue;
    }
    threat_fill(i);
    dma_channel_set_read_addr(channel, (const void *)threat_buffers[i], false);
    dma_channel_set_trans_count(channel, THREAT_BUFFER_SAMPLES, false);
  }
}

void buzzer_play_4_stop(void) {
  if (!threat_active)
    return;
  uint32_t saved = save_and_disable_interrupts();
  gpio_put(PIN_BUZZER_PWMB, false);
  pwm_set_enabled(threat_slice, false);
  threat_active = false;
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)threat_channels[i];
    dma_channel_set_irq0_enabled(channel, false);
    hw_clear_bits(&dma_hw->ch[channel].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
  }
  for (unsigned int i = 0u; i < 2u; ++i) {
    dma_channel_abort((uint)threat_channels[i]);
    dma_channel_acknowledge_irq0((uint)threat_channels[i]);
  }
  restore_interrupts(saved);
}

bool buzzer_play_4(unsigned int count) {
  /* Same count and battery-tone arbitration as buzzer_play_2(). */
  if (count < 1u || count > BUZZER_PLAY_MAX || buzzer_tone_sequence_active())
    return false;
  if (threat_channels[0] < 0) {
    int first = dma_claim_unused_channel(false);
    if (first < 0)
      return false;
    int second = dma_claim_unused_channel(false);
    if (second < 0) {
      dma_channel_unclaim((uint)first);
      return false;
    }
    threat_channels[0] = first;
    threat_channels[1] = second;
    irq_add_shared_handler(DMA_IRQ_0, threat_dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_0, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
  }
  /* Stop the current player through its existing off path before borrowing
   * the buzzer pins. This never changes channel A or the shared STBY pin. */
  buzzer_set(false);
  threat_slice = pwm_gpio_to_slice_num(PIN_BUZZER_BIN1);
  if (!threat_pattern_init(&threat_synth, count, clock_get_hz(clk_sys)))
    return false;
  threat_finished = false;
  threat_underrun = false;
  threat_fill(0u);
  threat_fill(1u);
  uint16_t period = threat_synth.period;
  pwm_set_clkdiv(threat_slice, 1.0f);
  pwm_set_wrap(threat_slice, (uint16_t)(period - 1u));
  pwm_set_counter(threat_slice, 0u);
  pwm_set_output_polarity(threat_slice, false, false);
  pwm_set_both_levels(threat_slice, period, period);
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)threat_channels[i];
    dma_channel_config config = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    /* A missed refill can repeat this buffer, never read beyond it. */
    channel_config_set_ring(&config, false, THREAT_BUFFER_RING_BITS);
    channel_config_set_dreq(&config, pwm_get_dreq(threat_slice));
    channel_config_set_chain_to(&config, (uint)threat_channels[i ^ 1u]);
    dma_channel_configure(channel, &config, &pwm_hw->slice[threat_slice].cc,
                          (const void *)threat_buffers[i], THREAT_BUFFER_SAMPLES,
                          false);
    dma_channel_acknowledge_irq0(channel);
    dma_channel_set_irq0_enabled(channel, true);
  }
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  threat_active = true;
  dma_start_channel_mask(1u << threat_channels[0]);
  gpio_put(PIN_BUZZER_PWMB, true);
  pwm_set_enabled(threat_slice, true);
  return true;
}

void buzzer_play_4_tick(void) {
  if (threat_active && threat_finished)
    buzzer_set(false);
}

bool buzzer_play_4_active(void) { return threat_active; }
bool buzzer_play_4_underrun(void) { return threat_underrun; }
