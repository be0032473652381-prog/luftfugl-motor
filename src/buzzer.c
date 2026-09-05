#include "buzzer.h"

#include "config.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/time.h"
#include <limits.h>

#ifdef LUFTFUGL_DEBUG
#include "buzzer_dds_wave.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#endif

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
static bool tone_active;
static uint32_t tone_deadline_ms;
static uint16_t tone_sequence_frequency;
static uint32_t tone_sequence_duration_ms;
static uint32_t tone_sequence_pause_ms;
static uint32_t tone_sequence_deadline_ms;
static uint8_t tone_sequence_remaining;
static uint8_t tone_sequence_total;
static uint8_t tone_sequence_current;
static bool tone_sequence_pause;
static uint32_t rng = 0xA5A5A5A5u;

#ifdef LUFTFUGL_DEBUG
/* Two 1 KiB buffers give the refill IRQ 2.56 ms at 100 kHz. All synthesis
 * state is prepared in main context before DMA starts, then owned by the
 * DMA IRQ until stopped. No floating point, division, or console work in IRQ. */
enum { DDS_BUFFER_SAMPLES = 256 };
static volatile int dds_channels[2] = {-1, -1};
static volatile uint32_t dds_buffers[2][DDS_BUFFER_SAMPLES]
    __attribute__((aligned(DDS_BUFFER_SAMPLES * sizeof(uint32_t))));
static volatile bool dds_terminal[2];
static volatile bool dds_active;
static volatile bool dds_finished;
static volatile bool dds_underrun;
static volatile uint32_t dds_steps[BIRD_EVENT_MAX];
static volatile uint32_t dds_lengths[BIRD_EVENT_MAX];
static volatile bool dds_on[BIRD_EVENT_MAX];
static volatile uint16_t dds_count;
static volatile uint16_t dds_index;
static volatile uint16_t dds_period;
static volatile uint32_t dds_phase;
static volatile uint32_t dds_remaining;
static volatile uint32_t dds_gap;
static volatile uint32_t dds_gap_remaining;
static volatile unsigned int dds_plays;

static uint32_t dds_next_sample(void) {
  uint32_t silent = (uint32_t)dds_period | ((uint32_t)dds_period << 16);
  if (dds_gap_remaining) {
    --dds_gap_remaining;
    return silent;
  }
  if (dds_index == dds_count) {
    if (dds_plays <= 1u)
      return silent;
    --dds_plays;
    dds_index = 0u;
    dds_remaining = dds_lengths[0];
    dds_gap_remaining = dds_gap - 1u;
    return silent;
  }
  uint32_t value = silent;
  if (dds_on[dds_index]) {
    value = buzzer_dds_compare(dds_phase, dds_period);
    /* Frequency changes retain phase instead of restarting the oscillator. */
    dds_phase += dds_steps[dds_index];
  }
  if (--dds_remaining == 0u) {
    ++dds_index;
    if (dds_index < dds_count)
      dds_remaining = dds_lengths[dds_index];
  }
  return value;
}

static void dds_fill(unsigned int buffer) {
  /* Retire only a wholly silent buffer, allowing PWM's double-buffered
   * compare register to consume the last audible sample first. */
  dds_terminal[buffer] = dds_index == dds_count && dds_plays <= 1u;
  for (unsigned int i = 0u; i < DDS_BUFFER_SAMPLES; ++i)
    dds_buffers[buffer][i] = dds_next_sample();
}

static void dds_dma_irq(void) {
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)dds_channels[i];
    if (!dma_channel_get_irq1_status(channel))
      continue;
    dma_channel_acknowledge_irq1(channel);
    if (!dds_active || dds_finished)
      continue;
    if (dds_terminal[i]) {
      /* The last buffer contains trailing brake samples, so muting here
       * cannot truncate the final note. Main context tears down DMA. */
      gpio_put(PIN_BUZZER_PWMB, false);
      dds_finished = true;
      continue;
    }
    if (!dma_channel_is_busy((uint)dds_channels[i ^ 1u])) {
      gpio_put(PIN_BUZZER_PWMB, false);
      dds_underrun = true;
      dds_finished = true;
      continue;
    }
    dds_fill(i);
    dma_channel_set_read_addr(channel, (const void *)dds_buffers[i], false);
    dma_channel_set_trans_count(channel, DDS_BUFFER_SAMPLES, false);
  }
}

static void dds_stop(void) {
  if (!dds_active)
    return;
  uint32_t saved = save_and_disable_interrupts();
  gpio_put(PIN_BUZZER_PWMB, false);
  pwm_set_enabled(buzzer_slice, false);
  dds_active = false;
  /* Disable both chains before aborting either channel. This also avoids
   * RP2040 DMA abort's late IRQ affecting a subsequent playback. */
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)dds_channels[i];
    dma_channel_set_irq1_enabled(channel, false);
    hw_clear_bits(&dma_hw->ch[channel].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
  }
  for (unsigned int i = 0u; i < 2u; ++i) {
    dma_channel_abort((uint)dds_channels[i]);
    dma_channel_acknowledge_irq1((uint)dds_channels[i]);
  }
  restore_interrupts(saved);
}
#else
static void dds_stop(void) {}
#endif

static void buzzer_gpio_off(void) {
  dds_stop();
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
  tone_active = false;
  tone_deadline_ms = 0u;
  buzzer_gpio_off();
}

void buzzer_set(bool on) {
  buzzer_gpio_off();
  tone_active = false;
  tone_deadline_ms = 0u;
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
  if (tone_active || tone_sequence_remaining || tone_sequence_pause)
    return;
  buzzer_gpio_off();
  if (count < 1u)
    count = 1u;
  if (count > BUZZER_PLAY_MAX)
    count = BUZZER_PLAY_MAX;
  enabled = true;
  tone_active = false;
  tone_deadline_ms = 0u;
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

#ifdef LUFTFUGL_DEBUG
bool buzzer_play_2(unsigned int count) {
  if (count < 1u || count > BUZZER_PLAY_MAX || buzzer_tone_sequence_active())
    return false;
  if (dds_channels[0] < 0) {
    int first = dma_claim_unused_channel(false);
    if (first < 0)
      return false;
    int second = dma_claim_unused_channel(false);
    if (second < 0) {
      dma_channel_unclaim((uint)first);
      return false;
    }
    dds_channels[0] = first;
    dds_channels[1] = second;
    irq_add_shared_handler(DMA_IRQ_1, dds_dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    /* Below the default timer IRQ priority: motor safety always preempts. */
    irq_set_priority(DMA_IRQ_1, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
  }
  buzzer_gpio_off();
  enabled = false;
  build_bird();
  uint32_t sys_hz = clock_get_hz(clk_sys);
  uint32_t period = (sys_hz + BUZZER_DDS_CARRIER_HZ - 1u) /
                    BUZZER_DDS_CARRIER_HZ;
  if (period < 2u || period > UINT16_MAX)
    return false;
  dds_period = (uint16_t)period;
  dds_count = event_count;
  for (uint16_t i = 0u; i < event_count; ++i) {
    dds_steps[i] = (uint32_t)(((uint64_t)events[i].frequency << 32) *
                              period / sys_hz);
    dds_lengths[i] = (uint32_t)((uint64_t)events[i].duration_ms * sys_hz /
                                ((uint64_t)period * 1000u));
    dds_on[i] = events[i].on;
  }
  dds_gap = (uint32_t)((uint64_t)BIRD_GAP_MS * sys_hz /
                       ((uint64_t)period * 1000u));
  dds_index = 0u;
  dds_phase = 0u;
  dds_remaining = dds_lengths[0];
  dds_gap_remaining = 0u;
  dds_plays = count;
  dds_finished = false;
  dds_underrun = false;
  dds_fill(0u);
  dds_fill(1u);
  pwm_set_clkdiv(buzzer_slice, 1.0f);
  pwm_set_wrap(buzzer_slice, (uint16_t)(period - 1u));
  pwm_set_counter(buzzer_slice, 0u);
  pwm_set_output_polarity(buzzer_slice, false, false);
  pwm_set_both_levels(buzzer_slice, (uint16_t)period, (uint16_t)period);
  for (unsigned int i = 0u; i < 2u; ++i) {
    uint channel = (uint)dds_channels[i];
    dma_channel_config config = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    /* If interrupts are delayed beyond a whole buffer, repeat only this
     * buffer until the underrun handler mutes; never read unrelated RAM. */
    channel_config_set_ring(&config, false, 10u);
    channel_config_set_dreq(&config, pwm_get_dreq(buzzer_slice));
    channel_config_set_chain_to(&config, (uint)dds_channels[i ^ 1u]);
    dma_channel_configure(channel, &config, &pwm_hw->slice[buzzer_slice].cc,
                          (const void *)dds_buffers[i], DDS_BUFFER_SAMPLES,
                          false);
    dma_channel_acknowledge_irq1(channel);
    dma_channel_set_irq1_enabled(channel, true);
  }
  gpio_set_function(PIN_BUZZER_BIN1, GPIO_FUNC_PWM);
  gpio_set_function(PIN_BUZZER_BIN2, GPIO_FUNC_PWM);
  dds_active = true;
  dma_start_channel_mask(1u << dds_channels[0]);
  gpio_put(PIN_BUZZER_PWMB, true);
  pwm_set_enabled(buzzer_slice, true);
  return true;
}

bool buzzer_play_2_active(void) { return dds_active; }
bool buzzer_play_2_underrun(void) { return dds_underrun; }
#endif

void buzzer_tone(uint32_t frequency_hz, uint32_t duration_ms) {
  if (frequency_hz == 0u || duration_ms == 0u)
    return;
  if (frequency_hz > UINT16_MAX)
    frequency_hz = UINT16_MAX;
  buzzer_gpio_off();
  /* A fixed tone is an exclusive buzzer operation.  Leaving the bird-call
   * player armed lets buzzer_tick() reclaim the PWM pins as soon as the tone
   * ends, which corrupts the silent pause and subsequent battery chirps. */
  enabled = false;
  finite_play = false;
  plays_left = 0u;
  event_count = 0u;
  event_index = 0u;
  applied_index = UINT16_MAX;
  buzzer_apply_frequency((uint16_t)frequency_hz);
  tone_active = true;
  tone_deadline_ms = to_ms_since_boot(get_absolute_time()) + duration_ms;
}

void buzzer_tone_sequence(uint32_t frequency_hz, uint32_t duration_ms,
                          uint8_t repeat, uint32_t pause_ms) {
  if (!frequency_hz || !duration_ms || !repeat)
    return;
  tone_sequence_frequency =
      (uint16_t)(frequency_hz > UINT16_MAX ? UINT16_MAX : frequency_hz);
  tone_sequence_duration_ms = duration_ms;
  tone_sequence_pause_ms = pause_ms;
  tone_sequence_remaining = (uint8_t)(repeat - 1u);
  tone_sequence_total = repeat;
  tone_sequence_current = 1u;
  tone_sequence_pause = false;
  buzzer_tone(tone_sequence_frequency, tone_sequence_duration_ms);
}

void buzzer_tone_stop(void) {
  if (tone_active)
    buzzer_gpio_off();
  tone_active = false;
  tone_deadline_ms = 0u;
  tone_sequence_remaining = 0u;
  tone_sequence_pause = false;
  tone_sequence_deadline_ms = 0u;
  applied_index = UINT16_MAX;
}

bool buzzer_tone_sequence_active(void) {
  return tone_active || tone_sequence_pause || tone_sequence_remaining;
}

void buzzer_tone_sequence_status(uint8_t *current, uint8_t *total,
                                 bool *paused,
                                 uint32_t *deadline_remaining_ms) {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  uint32_t deadline = tone_sequence_pause ? tone_sequence_deadline_ms
                                           : tone_deadline_ms;
  if (current)
    *current = tone_sequence_current;
  if (total)
    *total = tone_sequence_total;
  if (paused)
    *paused = tone_sequence_pause;
  if (deadline_remaining_ms)
    *deadline_remaining_ms =
        deadline && (int32_t)(deadline - now) > 0 ? deadline - now : 0u;
}

void buzzer_tick(void) {
#ifdef LUFTFUGL_DEBUG
  if (dds_active) {
    if (dds_finished)
      buzzer_gpio_off();
    return;
  }
#endif
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (tone_active) {
    if (tone_deadline_ms && (int32_t)(now - tone_deadline_ms) >= 0) {
      buzzer_gpio_off();
      tone_active = false;
      tone_deadline_ms = 0u;
      applied_index = UINT16_MAX;
      if (tone_sequence_remaining) {
        /* Pause begins when the preceding chirp physically stops. */
        tone_sequence_pause = true;
        tone_sequence_deadline_ms = now + tone_sequence_pause_ms;
      }
    } else {
      return;
    }
  }
  if (tone_sequence_pause) {
    if ((int32_t)(now - tone_sequence_deadline_ms) < 0)
      return;
    tone_sequence_pause = false;
    --tone_sequence_remaining;
    ++tone_sequence_current;
    /* The next chirp begins only after the complete silent pause. */
    buzzer_tone(tone_sequence_frequency, tone_sequence_duration_ms);
    return;
  }
  if (!enabled)
    return;
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

bool buzzer_enabled(void) {
#ifdef LUFTFUGL_DEBUG
  if (dds_active)
    return true;
#endif
  return enabled || tone_active;
}
