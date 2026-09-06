#include "buzzer_compact.h"
#include "buzzer_compact_pattern.h"
#include "buzzer.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

/* Deliberately separate from play-2, play-3 and play-4, whose generator, renderer, DMA state,
 * and entry point stay unchanged. Crips-5 also serves station arrivals. */
enum { COMPACT_BUFFER_SAMPLES = 256, COMPACT_BUFFER_RING_BITS = 10 };
static volatile int compact_channels[2] = {-1, -1};
static volatile uint32_t compact_buffers[2][COMPACT_BUFFER_SAMPLES]
    __attribute__((aligned(COMPACT_BUFFER_SAMPLES * sizeof(uint32_t))));
static volatile bool compact_terminal[2];
static volatile bool compact_active, compact_finished, compact_underrun;
static volatile compact_synth_t compact_synth;
static uint compact_slice;

static void compact_fill(unsigned int buffer) {
  /* A wholly silent terminal buffer accommodates PWM's buffered compare. */
  compact_terminal[buffer] = compact_synth_done(&compact_synth);
  for (unsigned int i = 0u; i < COMPACT_BUFFER_SAMPLES; ++i)
    compact_buffers[buffer][i] = compact_next_sample(&compact_synth);
}

static void compact_dma_irq(void) {
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)compact_channels[i];
    if (!dma_channel_get_irq0_status(channel))
      continue;
    dma_channel_acknowledge_irq0(channel);
    if (!compact_active || compact_finished)
      continue;
    if (compact_terminal[i]) {
      gpio_put(PIN_BUZZER_PWMB, false);
      compact_finished = true;
      continue;
    }
    if (!dma_channel_is_busy((uint)compact_channels[i ^ 1u])) {
      gpio_put(PIN_BUZZER_PWMB, false);
      compact_underrun = true;
      compact_finished = true;
      continue;
    }
    compact_fill(i);
    dma_channel_set_read_addr(channel, (const void *)compact_buffers[i], false);
    dma_channel_set_trans_count(channel, COMPACT_BUFFER_SAMPLES, false);
  }
}

void buzzer_play_5_stop(void) {
  if (!compact_active)
    return;
  uint32_t saved = save_and_disable_interrupts();
  gpio_put(PIN_BUZZER_PWMB, false);
  pwm_set_enabled(compact_slice, false);
  compact_active = false;
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)compact_channels[i];
    dma_channel_set_irq0_enabled(channel, false);
    hw_clear_bits(&dma_hw->ch[channel].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
  }
  for (unsigned int i = 0u; i < 2u; ++i) {
    dma_channel_abort((uint)compact_channels[i]);
    dma_channel_acknowledge_irq0((uint)compact_channels[i]);
  }
  restore_interrupts(saved);
}

bool buzzer_play_5(unsigned int count) {
  /* Same count and battery-tone arbitration as buzzer_play_2(). */
  if (count < 1u || count > BUZZER_PLAY_MAX || buzzer_tone_sequence_active())
    return false;
  if (compact_channels[0] < 0) {
    int first = dma_claim_unused_channel(false);
    if (first < 0)
      return false;
    int second = dma_claim_unused_channel(false);
    if (second < 0) {
      dma_channel_unclaim((uint)first);
      return false;
    }
    compact_channels[0] = first;
    compact_channels[1] = second;
    irq_add_shared_handler(DMA_IRQ_0, compact_dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_0, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
  }
  /* Stop the current player through its existing off path before borrowing
   * the buzzer pins. This never changes channel A or the shared STBY pin. */
  buzzer_set(false);
  compact_slice = pwm_gpio_to_slice_num(PIN_BUZZER_BIN1);
  if (!compact_pattern_init(&compact_synth, count, clock_get_hz(clk_sys)))
    return false;
  compact_finished = false;
  compact_underrun = false;
  compact_fill(0u);
  compact_fill(1u);
  uint16_t period = compact_synth.period;
  pwm_set_clkdiv(compact_slice, 1.0f);
  pwm_set_wrap(compact_slice, (uint16_t)(period - 1u));
  pwm_set_counter(compact_slice, 0u);
  pwm_set_output_polarity(compact_slice, false, false);
  pwm_set_both_levels(compact_slice, period, period);
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)compact_channels[i];
    dma_channel_config config = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    /* A missed refill can repeat this buffer, never read beyond it. */
    channel_config_set_ring(&config, false, COMPACT_BUFFER_RING_BITS);
    channel_config_set_dreq(&config, pwm_get_dreq(compact_slice));
    channel_config_set_chain_to(&config, (uint)compact_channels[i ^ 1u]);
    dma_channel_configure(channel, &config, &pwm_hw->slice[compact_slice].cc,
                          (const void *)compact_buffers[i], COMPACT_BUFFER_SAMPLES,
                          false);
    dma_channel_acknowledge_irq0(channel);
    dma_channel_set_irq0_enabled(channel, true);
  }
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  compact_active = true;
  dma_start_channel_mask(1u << compact_channels[0]);
  gpio_put(PIN_BUZZER_PWMB, true);
  pwm_set_enabled(compact_slice, true);
  return true;
}

void buzzer_play_5_tick(void) {
  if (compact_active && compact_finished)
    buzzer_set(false);
}

bool buzzer_play_5_active(void) { return compact_active; }
bool buzzer_play_5_underrun(void) { return compact_underrun; }
