#include "power_monitor.h"

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/regs/i2c.h"
#include "hardware/structs/i2c.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

enum { REG_CONFIG, REG_SHUNT, REG_BUS, REG_POWER, REG_CURRENT, REG_CAL };
typedef enum { IO_IDLE, IO_WRITE, IO_READ } io_kind_t;
typedef enum {
  PM_CALIBRATE,
  PM_INITIAL_POWER_DOWN,
  PM_IDLE,
  PM_TRIGGER,
  PM_WAIT_READY,
  PM_READ_SHUNT,
  PM_READ_CURRENT,
  PM_READ_POWER,
  PM_POWER_DOWN,
  PM_READ_DOWN_CONFIG
} pm_state_t;
typedef enum { NOTE_PEAK, NOTE_MINIMUM, NOTE_STALL } note_kind_t;
typedef struct {
  uint32_t ms;
  int32_t current_ua;
  uint32_t bus_mv;
  note_kind_t kind;
} notable_t;

static volatile power_sample_t published;
static power_sample_t working;
static pm_state_t state;
static io_kind_t io_kind;
static uint16_t io_value;
static bool io_done;
static bool sample_requested;
static bool moving;
static bool inrush;
static uint32_t inrush_until_ms;
static int32_t inrush_peak_ua;
static uint32_t next_sample_ms;
static uint32_t session_start_ms;
static uint32_t sample_count;
static uint32_t peak_ms, minimum_ms;
static int32_t peak_ua;
static uint32_t minimum_mv;
static uint64_t measured_uas;
static uint64_t energy_uws;
static uint32_t previous_ms;
static int32_t previous_ua;
static int64_t move_current_sum_ua;
static uint32_t move_current_samples;
static int32_t move_averages_ua[RUN_CURRENT_DEPTH];
static uint8_t move_head, move_used;
static volatile bool simulated;
static volatile uint16_t simulated_bus_mv;
static notable_t events[EVENT_LOG_DEPTH];
static uint8_t event_head, event_used;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

/* 0.04096 / (100 uA * 0.1 ohm) = 4096.  Keep the operands visible so a
 * shunt or resolution change cannot silently leave a stale magic value. */
static uint16_t calibration_value(void) {
  return (uint16_t)(40960000u /
                    (INA219_CURRENT_LSB_UA * INA219_SHUNT_MOHM));
}

static bool io_complete(uint16_t *value) {
  i2c_hw_t *hw = i2c_get_hw(i2c0);
  if (io_kind == IO_IDLE)
    return false;
  if (!(hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS))
    return false;
  (void)hw->clr_stop_det;
  if (hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
    (void)hw->clr_tx_abrt;
    io_kind = IO_IDLE;
    io_done = false;
    return false;
  }
  if (io_kind == IO_READ && hw->rxflr >= 2u) {
    uint16_t high = (uint16_t)(hw->data_cmd & 0xffu);
    uint16_t low = (uint16_t)(hw->data_cmd & 0xffu);
    io_value = (uint16_t)((high << 8) | low);
  }
  io_kind = IO_IDLE;
  io_done = true;
  if (value)
    *value = io_value;
  return true;
}

static bool io_start_write(uint8_t reg, uint16_t value) {
  i2c_hw_t *hw = i2c_get_hw(i2c0);
  if (io_kind != IO_IDLE)
    return false;
  hw->enable = 0u;
  hw->tar = INA219_ADDRESS;
  hw->enable = 1u;
  while (hw->rxflr)
    (void)hw->data_cmd;
  (void)hw->clr_stop_det;
  hw->data_cmd = reg;
  hw->data_cmd = (uint8_t)(value >> 8);
  hw->data_cmd = (uint8_t)value | I2C_IC_DATA_CMD_STOP_BITS;
  io_kind = IO_WRITE;
  io_done = false;
  return true;
}

static bool io_start_read(uint8_t reg) {
  i2c_hw_t *hw = i2c_get_hw(i2c0);
  if (io_kind != IO_IDLE)
    return false;
  hw->enable = 0u;
  hw->tar = INA219_ADDRESS;
  hw->enable = 1u;
  while (hw->rxflr)
    (void)hw->data_cmd;
  (void)hw->clr_stop_det;
  hw->data_cmd = reg;
  hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_RESTART_BITS;
  hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_STOP_BITS;
  io_kind = IO_READ;
  io_done = false;
  return true;
}

static void note(note_kind_t kind, uint32_t ms, int32_t ua, uint32_t mv) {
  events[event_head] = (notable_t){ms, ua, mv, kind};
  event_head = (uint8_t)((event_head + 1u) % EVENT_LOG_DEPTH);
  if (event_used < EVENT_LOG_DEPTH)
    ++event_used;
}

static void publish_sample(void) {
  uint32_t ms = now_ms();
  working.valid = !working.overflow;
  working.sample_ms = ms;
  working.calibration_written = true;
  working.calibration_raw = calibration_value();
  working.config_raw = inrush ? INA219_INRUSH_CONFIG : INA219_NORMAL_CONFIG;
  working.bus_mv = (uint32_t)(working.bus_raw >> 3) * INA219_BUS_LSB_MV;
  working.shunt_uv = (int32_t)working.shunt_raw * INA219_SHUNT_LSB_UV;
  working.current_ua = (int32_t)working.current_raw * INA219_CURRENT_LSB_UA;
  working.power_uw = (uint32_t)working.power_raw * INA219_POWER_LSB_UW;
  if (working.valid) {
    if (sample_count && ms != previous_ms) {
      uint32_t dt = ms - previous_ms;
      int32_t average_ua = (working.current_ua + previous_ua) / 2;
      if (average_ua > 0)
        measured_uas += (uint64_t)(uint32_t)average_ua * dt / 1000u;
      energy_uws += (uint64_t)working.power_uw * dt / 1000u;
    }
    previous_ms = ms;
    previous_ua = working.current_ua;
    ++sample_count;
    if (!peak_ms || working.current_ua > peak_ua) {
      peak_ua = working.current_ua;
      peak_ms = ms;
      note(NOTE_PEAK, ms, working.current_ua, working.bus_mv);
    }
    if (!minimum_mv || working.bus_mv < minimum_mv) {
      minimum_mv = working.bus_mv;
      minimum_ms = ms;
      note(NOTE_MINIMUM, ms, working.current_ua, working.bus_mv);
    }
    if (inrush && working.current_ua > inrush_peak_ua)
      inrush_peak_ua = working.current_ua;
    if (moving) {
      move_current_sum_ua += working.current_ua;
      ++move_current_samples;
    }
  }
  published = working;
}

void power_monitor_init(void) {
  memset((void *)&published, 0, sizeof published);
  memset(&working, 0, sizeof working);
  i2c_init(i2c0, I2C_BAUD);
  gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(PIN_I2C_SDA);
  gpio_pull_up(PIN_I2C_SCL);
  i2c_get_hw(i2c0)->enable = 0u;
  i2c_get_hw(i2c0)->tar = INA219_ADDRESS;
  i2c_get_hw(i2c0)->enable = 1u;
  state = PM_CALIBRATE;
  io_kind = IO_IDLE;
  io_done = false;
  sample_requested = true;
  session_start_ms = now_ms();
  next_sample_ms = session_start_ms;
}

void power_monitor_request_sample(void) { sample_requested = true; }

void power_monitor_motion_start(void) {
  moving = true;
  inrush = true;
  inrush_peak_ua = 0;
  move_current_sum_ua = 0;
  move_current_samples = 0u;
  inrush_until_ms = now_ms() + INRUSH_SAMPLE_MS;
  sample_requested = true;
}

void power_monitor_motion_stop(void) {
  if (moving && move_current_samples) {
    move_averages_ua[move_head] =
        (int32_t)(move_current_sum_ua / (int64_t)move_current_samples);
    move_head = (uint8_t)((move_head + 1u) % RUN_CURRENT_DEPTH);
    if (move_used < RUN_CURRENT_DEPTH)
      ++move_used;
  }
  moving = false;
  inrush = false;
  sample_requested = true;
}

void power_monitor_tick(void) {
  uint16_t value;
  uint32_t ms = now_ms();
  bool completed = io_complete(&value);
  switch (state) {
  case PM_CALIBRATE:
    if (completed)
      state = PM_INITIAL_POWER_DOWN;
    else if (io_kind == IO_IDLE)
      (void)io_start_write(REG_CAL, calibration_value());
    break;
  case PM_INITIAL_POWER_DOWN:
    if (completed) {
      state = PM_IDLE;
      next_sample_ms = ms;
    } else if (io_kind == IO_IDLE) {
      (void)io_start_write(REG_CONFIG, INA219_POWER_DOWN_CONFIG);
    }
    break;
  case PM_IDLE:
    if (sample_requested || (int32_t)(ms - next_sample_ms) >= 0) {
      sample_requested = false;
      state = PM_TRIGGER;
    }
    break;
  case PM_TRIGGER:
    if (completed)
      state = PM_WAIT_READY;
    else if (io_kind == IO_IDLE)
      (void)io_start_write(REG_CONFIG, inrush ? INA219_INRUSH_CONFIG
                                             : INA219_NORMAL_CONFIG);
    break;
  case PM_WAIT_READY:
    if (completed) {
      if (value & 1u) {
        working.bus_raw = value;
        working.overflow = true;
        state = PM_READ_SHUNT;
      } else if (value & 2u) {
        working.bus_raw = value;
        working.overflow = false;
        state = PM_READ_SHUNT;
      }
    } else if (io_kind == IO_IDLE) {
      (void)io_start_read(REG_BUS);
    }
    break;
  case PM_READ_SHUNT:
    if (completed) {
      working.shunt_raw = (int16_t)value;
      state = PM_READ_CURRENT;
    } else if (io_kind == IO_IDLE)
      (void)io_start_read(REG_SHUNT);
    break;
  case PM_READ_CURRENT:
    if (completed) {
      working.current_raw = (int16_t)value;
      state = PM_READ_POWER;
    } else if (io_kind == IO_IDLE)
      (void)io_start_read(REG_CURRENT);
    break;
  case PM_READ_POWER:
    if (completed) {
      working.power_raw = value;
      publish_sample();
      state = PM_POWER_DOWN;
    } else if (io_kind == IO_IDLE)
      (void)io_start_read(REG_POWER);
    break;
  case PM_POWER_DOWN:
    if (completed) {
      state = PM_READ_DOWN_CONFIG;
    } else if (io_kind == IO_IDLE)
      (void)io_start_write(REG_CONFIG, INA219_POWER_DOWN_CONFIG);
    break;
  case PM_READ_DOWN_CONFIG:
    if (completed) {
      published.config_raw = value;
      if (inrush && moving && (int32_t)(ms - inrush_until_ms) < 0)
        state = PM_TRIGGER;
      else {
        inrush = false;
        state = PM_IDLE;
        next_sample_ms = ms + INA219_SAMPLE_PERIOD_MS;
      }
    } else if (io_kind == IO_IDLE)
      (void)io_start_read(REG_CONFIG);
    break;
  }
}

void power_monitor_snapshot(power_sample_t *out) {
  *out = published;
  if (simulated) {
    out->valid = true;
    out->overflow = false;
    out->bus_mv = simulated_bus_mv;
  }
}

bool power_monitor_sim_set(bool enabled, uint16_t bus_mv) {
  if (enabled && (bus_mv < 3900u || bus_mv > 4500u))
    return false;
  simulated_bus_mv = bus_mv;
  simulated = enabled;
  return true;
}

bool power_monitor_sim_active(void) { return simulated; }

static unsigned soc_percent(uint32_t mv) {
  static const uint16_t volts[] = {4000, 4200, 4400, 4800, 5200, 5600, 6000, 6400};
  static const uint8_t soc[] = {0, 8, 15, 30, 50, 70, 85, 100};
  if (mv <= volts[0]) return 0;
  for (unsigned i = 1; i < sizeof volts / sizeof volts[0]; ++i)
    if (mv <= volts[i])
      return soc[i - 1] + (unsigned)(mv - volts[i - 1]) *
             (soc[i] - soc[i - 1]) / (volts[i] - volts[i - 1]);
  return 100;
}

void power_monitor_format_batt(char *out, size_t size) {
  power_sample_t s;
  power_monitor_snapshot(&s);
  uint64_t model_uas = SLEEP_CURRENT_UA ?
      (uint64_t)SLEEP_CURRENT_UA * (now_ms() - session_start_ms) / 1000u : 0u;
  uint64_t total_uas = measured_uas + model_uas;
  uint32_t consumed_uah = (uint32_t)(total_uas / 3600u);
  if (!s.valid) {
    snprintf(out, size, " BATTERY\r\n   no valid sample yet%s\r\n   sleep current    %s",
             s.overflow ? " (math overflow)" : "",
             SLEEP_CURRENT_UA ? "compiled model" : "not measured; model unavailable");
    power_monitor_request_sample();
    return;
  }
  snprintf(out, size,
           " BATTERY\r\n"
           "   bus voltage      %lu.%03lu V    state of charge   ~%u%% (estimate)\r\n"
           "   current          %ld.%01ld mA   remaining         %lu mAh (estimate)\r\n"
           "   power            %lu mW       runtime est.      unavailable\r\n"
           "   pack resistance  unavailable  (need idle/load wake integration)\r\n"
           "   consumed         %lu.%03lu mAh since boot\r\n"
           "   sleep current    %s",
           (unsigned long)(s.bus_mv / 1000u), (unsigned long)(s.bus_mv % 1000u),
           soc_percent(s.bus_mv), (long)(s.current_ua / 1000),
           (long)((s.current_ua < 0 ? -s.current_ua : s.current_ua) % 1000 / 100),
           (unsigned long)(BATTERY_CAPACITY_MAH -
             (consumed_uah / 1000u > BATTERY_CAPACITY_MAH ? BATTERY_CAPACITY_MAH : consumed_uah / 1000u)),
           (unsigned long)(s.power_uw / 1000u),
           (unsigned long)(consumed_uah / 1000u), (unsigned long)(consumed_uah % 1000u),
           SLEEP_CURRENT_UA ? "modelled, not measured by INA219" : "not measured; model unavailable");
}

void power_monitor_format_raw(char *out, size_t size) {
  power_sample_t s = published;
  snprintf(out, size,
           "INA219 RAW\r\n  bus=0x%04x shunt=0x%04x current=0x%04x power=0x%04x\r\n"
           "  current=%ld uA power=%lu uW overflow=%s\r\n"
           "  calibration 0x%04x written before current read: %s",
           s.bus_raw, (uint16_t)s.shunt_raw, (uint16_t)s.current_raw,
           s.power_raw, (long)s.current_ua, (unsigned long)s.power_uw,
           s.overflow ? "YES (invalid)" : "no", s.calibration_raw,
           s.calibration_written ? "yes" : "no");
  power_monitor_request_sample();
}

void power_monitor_format_res(char *out, size_t size) {
  snprintf(out, size,
           "PACK RESISTANCE\r\n  unavailable: CO2 idle/load sampling is not implemented\r\n"
           "  samples 0/%u  fresh reference %s",
           RPACK_MIN_SAMPLES, RPACK_FRESH_MOHM ? "configured" : "not measured");
}

void power_monitor_format_log(char *out, size_t size) {
  uint32_t elapsed = now_ms() - session_start_ms;
  snprintf(out, size,
           "SESSION\r\n  duration %lu d %lu h %lu min  samples %lu\r\n"
           "  measured charge %llu uA s  energy %llu uW s\r\n"
           "  sleep term %s; counters are since last boot",
           (unsigned long)(elapsed / 86400000u),
           (unsigned long)((elapsed / 3600000u) % 24u),
           (unsigned long)((elapsed / 60000u) % 60u),
           (unsigned long)sample_count, (unsigned long long)measured_uas,
           (unsigned long long)energy_uws,
           SLEEP_CURRENT_UA ? "modelled" : "unavailable (not measured)");
}

void power_monitor_format_events(char *out, size_t size) {
  size_t used = 0u;
  int n = snprintf(out, size, "EVENTS (%u/%u)", event_used, EVENT_LOG_DEPTH);
  if (n < 0) return;
  used = (size_t)n < size ? (size_t)n : size;
  uint8_t first = (uint8_t)((event_head + EVENT_LOG_DEPTH - event_used) % EVENT_LOG_DEPTH);
  for (uint8_t i = 0; i < event_used && used < size; ++i) {
    notable_t *e = &events[(first + i) % EVENT_LOG_DEPTH];
    const char *name = e->kind == NOTE_PEAK ? "peak" : e->kind == NOTE_MINIMUM ? "minimum" : "stall";
    n = snprintf(out + used, size - used, "\r\n  %8lu ms %-7s %ld uA %lu mV",
                 (unsigned long)e->ms, name, (long)e->current_ua,
                 (unsigned long)e->bus_mv);
    if (n < 0) break;
    used += (size_t)n < size - used ? (size_t)n : size - used;
  }
}

void power_monitor_format_load(char *out, size_t size) {
  int32_t last = 0;
  int32_t trend = 0;
  if (move_used) {
    uint8_t last_index = (uint8_t)((move_head + RUN_CURRENT_DEPTH - 1u) % RUN_CURRENT_DEPTH);
    uint8_t first_index = (uint8_t)((move_head + RUN_CURRENT_DEPTH - move_used) % RUN_CURRENT_DEPTH);
    last = move_averages_ua[last_index];
    if (move_averages_ua[first_index] > 0)
      trend = (last - move_averages_ua[first_index]) * 100 /
              move_averages_ua[first_index];
  }
  snprintf(out, size,
           "LOAD\r\n  last running %s%ld uA  moves %u\r\n"
           "  inrush peak %ld uA (lower bound; 20 ms capture)\r\n"
           "  no-load ratio unavailable: no-load current not measured\r\n"
           "  running trend %s%ld%% (oldest to newest rolling sample)\r\n"
           "  stall diagnostic %s; short-circuit diagnostic %s",
           move_used ? "" : "unavailable; ", (long)last, move_used,
           (long)inrush_peak_ua, move_used > 1u ? "" : "unavailable; ",
           (long)trend, STALL_CURRENT_MA ? "enabled" : "disabled (threshold not measured)",
           SHORT_CIRCUIT_MA ? "enabled" : "disabled (threshold not set)");
}

void power_monitor_format_ina(char *out, size_t size) {
  snprintf(out, size,
           "INA219\r\n  configured 0x%04x  active 0x%04x\r\n"
           "  calibration 0x%04x (computed: 0.1 ohm, 100 uA/LSB)\r\n"
           "  bus 16 V, PGA +/-160 mV, ADC 12-bit, triggered\r\n"
           "  idle 0x%04x MODE 000 (powered down); inrush ADC 9-bit",
           INA219_NORMAL_CONFIG, published.config_raw, calibration_value(),
           INA219_POWER_DOWN_CONFIG);
}

void power_monitor_format_menu(char lines[6][81]) {
  power_sample_t s;
  power_monitor_snapshot(&s);
  const char *battery_state = !s.valid ? "NO DATA"
                              : s.bus_mv < BATTERY_CRITICAL_MV ? "CRITICAL"
                              : s.bus_mv < BATTERY_WARN_MV ? "WARNING"
                                                         : "NORMAL";
  uint32_t elapsed = now_ms() - session_start_ms;
  uint32_t consumed_uah = (uint32_t)(measured_uas / 3600u);
  snprintf(lines[0], 81,
           "  BATTERY  %lu.%03lu V  %ld.%01ld mA  %lu mW  SOC ~%u%% est  %s%s",
           (unsigned long)(s.bus_mv / 1000u), (unsigned long)(s.bus_mv % 1000u),
           (long)(s.current_ua / 1000),
           (long)((s.current_ua < 0 ? -s.current_ua : s.current_ua) % 1000 / 100),
           (unsigned long)(s.power_uw / 1000u), soc_percent(s.bus_mv),
           battery_state, simulated ? " SIM" : "");
  snprintf(lines[1], 81,
           "  RAW      bus %04x  shunt %04x  current %04x  power %04x  overflow %s",
           s.bus_raw, (uint16_t)s.shunt_raw, (uint16_t)s.current_raw,
           s.power_raw, s.overflow ? "YES" : "no");
  snprintf(lines[2], 81,
           "  CONFIG   addr 0x%02x  active %04x  normal %04x  cal %04x  idle MODE 000",
           INA219_ADDRESS, s.config_raw, INA219_NORMAL_CONFIG,
           calibration_value());
  snprintf(lines[3], 81,
           "  CONVERT  normal 12-bit triggered; inrush 9-bit/%u ms (lower bound)",
           INRUSH_SAMPLE_MS);
  snprintf(lines[4], 81,
           "  SESSION  %lu.%03lu mAh  %lu samples  %lu:%02lu elapsed  peak %ld.%01ld mA",
           (unsigned long)(consumed_uah / 1000u),
           (unsigned long)(consumed_uah % 1000u), (unsigned long)sample_count,
           (unsigned long)(elapsed / 3600000u),
           (unsigned long)((elapsed / 60000u) % 60u), (long)(peak_ua / 1000),
           (long)((peak_ua < 0 ? -peak_ua : peak_ua) % 1000 / 100));
  snprintf(lines[5], 81,
           "  MODEL    sleep %s; resistance/runtime unavailable; since boot",
           SLEEP_CURRENT_UA ? "compiled (not INA measured)" : "not measured");
}

void power_monitor_reset(void) {
  session_start_ms = now_ms();
  sample_count = 0u;
  peak_ms = minimum_ms = 0u;
  peak_ua = 0;
  minimum_mv = 0u;
  measured_uas = energy_uws = 0u;
  previous_ms = 0u;
  event_head = event_used = 0u;
  move_head = move_used = 0u;
}
