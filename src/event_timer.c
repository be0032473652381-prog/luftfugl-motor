#include "event_timer.h"

#include "config.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "power_monitor.h"
#include "pico/time.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DS3231_REG_TIME 0x00u
#define DS3231_REG_ALARM1 0x07u
#define DS3231_REG_CONTROL 0x0eu
#define DS3231_REG_STATUS 0x0fu
#define DS3231_CONTROL_INTCN 0x04u
#define DS3231_CONTROL_A1IE 0x01u
#define DS3231_STATUS_A1F 0x01u
#define DS3231_ALARM_DY_DT 0x40u
#define EVENT_TIMER_SETTINGS_MAGIC 0x45544d31u /* "ETM1" */
#define EVENT_TIMER_SETTINGS_VERSION 1u
#define EVENT_TIMER_SETTINGS_FLASH_OFFSET \
  (PICO_FLASH_SIZE_BYTES - 4u * FLASH_SECTOR_SIZE)

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t interval_seconds;
  uint32_t checksum;
  uint8_t padding[FLASH_PAGE_SIZE - 16u];
} event_timer_settings_record_t;

_Static_assert(sizeof(event_timer_settings_record_t) == FLASH_PAGE_SIZE,
               "event timer settings record must fill one flash page");

static volatile bool alarm_pending;
static bool alarm_ack_pending;
static bool timer_enabled;
static bool alert_active;
static uint32_t alert_deadline_ms;
static uint32_t alarm_target_week_seconds;
static uint32_t event_interval_seconds;

static uint32_t settings_checksum(const event_timer_settings_record_t *record) {
  return record->magic ^ record->version ^ record->interval_seconds;
}

static bool interval_valid(uint32_t seconds) {
  return seconds >= EVENT_TIMER_INTERVAL_MIN_SECONDS &&
         seconds <= EVENT_TIMER_INTERVAL_MAX_SECONDS;
}

static void settings_restore(void) {
  const event_timer_settings_record_t *record =
      (const event_timer_settings_record_t *)(XIP_BASE +
                                              EVENT_TIMER_SETTINGS_FLASH_OFFSET);
  if (record->magic == EVENT_TIMER_SETTINGS_MAGIC &&
      record->version == EVENT_TIMER_SETTINGS_VERSION &&
      interval_valid(record->interval_seconds) &&
      record->checksum == settings_checksum(record))
    event_interval_seconds = record->interval_seconds;
}

static void settings_save(void) {
  event_timer_settings_record_t record;
  memset(&record, 0xff, sizeof record);
  record.magic = EVENT_TIMER_SETTINGS_MAGIC;
  record.version = EVENT_TIMER_SETTINGS_VERSION;
  record.reserved = 0u;
  record.interval_seconds = event_interval_seconds;
  record.checksum = settings_checksum(&record);
  uint32_t irq_state = save_and_disable_interrupts();
  flash_range_erase(EVENT_TIMER_SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(EVENT_TIMER_SETTINGS_FLASH_OFFSET,
                      (const uint8_t *)&record, sizeof record);
  restore_interrupts(irq_state);
}

static uint8_t bcd_to_binary(uint8_t value) {
  return (uint8_t)((value >> 4u) * 10u + (value & 0x0fu));
}

static uint8_t binary_to_bcd(uint8_t value) {
  return (uint8_t)(((value / 10u) << 4u) | (value % 10u));
}

static bool read_registers(uint8_t reg, uint8_t *data, size_t size) {
  return i2c_write_timeout_us(i2c0, DS3231_ADDRESS, &reg, 1u, true,
                              I2C_TRANSACTION_TIMEOUT_US) == 1 &&
         i2c_read_timeout_us(i2c0, DS3231_ADDRESS, data, size, false,
                             I2C_TRANSACTION_TIMEOUT_US) ==
             (int)size;
}

static bool write_registers(uint8_t reg, const uint8_t *data, size_t size) {
  uint8_t frame[5];
  if (size > sizeof frame - 1u)
    return false;
  frame[0] = reg;
  for (size_t i = 0u; i < size; ++i)
    frame[i + 1u] = data[i];
  return i2c_write_timeout_us(i2c0, DS3231_ADDRESS, frame, size + 1u, false,
                              I2C_TRANSACTION_TIMEOUT_US) ==
         (int)(size + 1u);
}

static uint8_t hour_24(uint8_t encoded) {
  if (!(encoded & 0x40u))
    return bcd_to_binary((uint8_t)(encoded & 0x3fu));
  uint8_t hour = bcd_to_binary((uint8_t)(encoded & 0x1fu));
  if (hour == 12u)
    hour = 0u;
  if (encoded & 0x20u)
    hour = (uint8_t)(hour + 12u);
  return hour;
}

static bool arm_next_alarm(void) {
  uint8_t time[4], control, status;
  if (!power_monitor_i2c_claim())
    return false;
  bool ok = read_registers(DS3231_REG_TIME, time, sizeof time) &&
            read_registers(DS3231_REG_CONTROL, &control, 1u) &&
            read_registers(DS3231_REG_STATUS, &status, 1u);
  if (ok) {
    uint32_t seconds = bcd_to_binary((uint8_t)(time[0] & 0x7fu));
    seconds += (uint32_t)bcd_to_binary((uint8_t)(time[1] & 0x7fu)) * 60u;
    seconds += (uint32_t)hour_24(time[2]) * 3600u;
    seconds += event_interval_seconds;
    uint8_t day = bcd_to_binary((uint8_t)(time[3] & 0x07u));
    if (seconds >= 86400u) {
      seconds -= 86400u;
      day = day >= 7u ? 1u : (uint8_t)(day + 1u);
    }
    uint8_t alarm[4] = {
        binary_to_bcd((uint8_t)(seconds % 60u)),
        binary_to_bcd((uint8_t)((seconds / 60u) % 60u)),
        binary_to_bcd((uint8_t)(seconds / 3600u)),
        (uint8_t)(DS3231_ALARM_DY_DT | binary_to_bcd(day))};
    alarm_target_week_seconds =
        ((uint32_t)day - 1u) * 86400u + seconds;
    status = (uint8_t)(status & ~DS3231_STATUS_A1F);
    control = (uint8_t)(control | DS3231_CONTROL_INTCN | DS3231_CONTROL_A1IE);
    ok = write_registers(DS3231_REG_ALARM1, alarm, sizeof alarm) &&
         write_registers(DS3231_REG_STATUS, &status, 1u) &&
         write_registers(DS3231_REG_CONTROL, &control, 1u);
  }
  power_monitor_i2c_release();
  return ok;
}

static bool disable_alarm(void) {
  if (!power_monitor_i2c_claim())
    return false;
  uint8_t control, status;
  bool ok = read_registers(DS3231_REG_CONTROL, &control, 1u) &&
            read_registers(DS3231_REG_STATUS, &status, 1u);
  if (ok) {
    control = (uint8_t)(control & ~DS3231_CONTROL_A1IE);
    status = (uint8_t)(status & ~DS3231_STATUS_A1F);
    ok = write_registers(DS3231_REG_CONTROL, &control, 1u) &&
         write_registers(DS3231_REG_STATUS, &status, 1u);
  }
  power_monitor_i2c_release();
  return ok;
}

static void gpio_irq(uint gpio, uint32_t events) {
  if (gpio == PIN_DS3231_INT && (events & GPIO_IRQ_EDGE_FALL))
    alarm_pending = true;
}

static void alert_start(void) {
  alert_active = true;
  alert_deadline_ms =
      to_ms_since_boot(get_absolute_time()) + EVENT_TIMER_LED_ON_MS;
  gpio_put(PIN_ONBOARD_LED, true);
}

void event_timer_init(void) {
  gpio_init(PIN_ONBOARD_LED);
  gpio_put(PIN_ONBOARD_LED, false);
  gpio_set_dir(PIN_ONBOARD_LED, GPIO_OUT);
  gpio_init(PIN_DS3231_INT);
  gpio_set_dir(PIN_DS3231_INT, GPIO_IN);
  gpio_pull_up(PIN_DS3231_INT);
  alarm_pending = false;
  alarm_ack_pending = false;
  alert_active = false;
  event_interval_seconds = EVENT_TIMER_INTERVAL_DEFAULT_SECONDS;
  settings_restore();
  timer_enabled = false;
  (void)disable_alarm();
  gpio_set_irq_enabled_with_callback(PIN_DS3231_INT, GPIO_IRQ_EDGE_FALL, true,
                                     gpio_irq);
}

void event_timer_poll(void) {
  /* INT/SQW is level-low until A1F is cleared.  Sampling the level as well as
   * using the falling-edge IRQ makes alarm servicing recover if the edge is
   * missed while GPIO IRQs are being configured or briefly masked. */
  if (timer_enabled && !alarm_ack_pending && !gpio_get(PIN_DS3231_INT))
    alarm_pending = true;
  if (alarm_pending) {
    alarm_pending = false;
    alert_start();
    if (timer_enabled)
      alarm_ack_pending = true;
  }
  if (alarm_ack_pending && disable_alarm()) {
    alarm_ack_pending = false;
    timer_enabled = false;
  }
  if (!alert_active)
    return;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if ((int32_t)(now - alert_deadline_ms) < 0)
    return;
  alert_active = false;
  gpio_put(PIN_ONBOARD_LED, false);
}

bool event_timer_start(char *detail, size_t size) {
  if (!arm_next_alarm()) {
    snprintf(detail, size, "failed to start DS3231 event timer");
    return false;
  }
  timer_enabled = true;
  alarm_pending = false;
  alarm_ack_pending = false;
  snprintf(detail, size, "DS3231 timer started: %lu seconds",
           (unsigned long)event_interval_seconds);
  return true;
}

bool event_timer_stop(char *detail, size_t size) {
  if (!timer_enabled) {
    snprintf(detail, size, "event timer already stopped");
    return true;
  }
  if (!disable_alarm()) {
    snprintf(detail, size, "failed to stop DS3231 event timer");
    return false;
  }
  timer_enabled = false;
  alarm_pending = false;
  alarm_ack_pending = false;
  gpio_put(PIN_ONBOARD_LED, false);
  alert_active = false;
  snprintf(detail, size, "DS3231 event timer stopped");
  return true;
}

bool event_timer_set_interval(uint32_t seconds, bool persist, char *detail,
                              size_t size) {
  if (!interval_valid(seconds)) {
    snprintf(detail, size, "interval must be %u..%u seconds",
             EVENT_TIMER_INTERVAL_MIN_SECONDS,
             EVENT_TIMER_INTERVAL_MAX_SECONDS);
    return false;
  }
  uint32_t previous = event_interval_seconds;
  event_interval_seconds = seconds;
  if (timer_enabled && !arm_next_alarm()) {
    event_interval_seconds = previous;
    snprintf(detail, size, "failed to re-arm DS3231 event timer");
    return false;
  }
  if (timer_enabled)
    alarm_pending = false;
  if (persist)
    settings_save();
  snprintf(detail, size,
           "DS3231 interval set to %lu seconds%s%s",
           (unsigned long)seconds, timer_enabled ? "; timer re-armed" : "",
           persist ? "; saved to flash" : "");
  return true;
}

bool event_timer_running(void) { return timer_enabled; }
bool event_timer_alert_active(void) { return alert_active; }

bool event_timer_format_countdown(char *detail, size_t size) {
  if (!timer_enabled) {
    snprintf(detail, size, "DS3231 timer stopped");
    return false;
  }
  if (alarm_pending || !gpio_get(PIN_DS3231_INT)) {
    snprintf(detail, size, "DS3231 timer: 0 seconds remaining");
    return true;
  }
  if (!power_monitor_i2c_claim()) {
    snprintf(detail, size, "I2C busy; retry");
    return false;
  }
  uint8_t time[4];
  bool ok = read_registers(DS3231_REG_TIME, time, sizeof time);
  power_monitor_i2c_release();
  if (!ok) {
    snprintf(detail, size, "DS3231 timer read failed");
    return false;
  }
  uint8_t day = bcd_to_binary((uint8_t)(time[3] & 0x07u));
  if (day < 1u || day > 7u) {
    snprintf(detail, size, "DS3231 timer has invalid day value");
    return false;
  }
  uint32_t now = ((uint32_t)day - 1u) * 86400u;
  now += (uint32_t)hour_24(time[2]) * 3600u;
  now += (uint32_t)bcd_to_binary((uint8_t)(time[1] & 0x7fu)) * 60u;
  now += bcd_to_binary((uint8_t)(time[0] & 0x7fu));
  uint32_t remaining = alarm_target_week_seconds >= now
                           ? alarm_target_week_seconds - now
                           : alarm_target_week_seconds + 604800u - now;
  snprintf(detail, size, "DS3231 timer: %lu seconds remaining",
           (unsigned long)remaining);
  return true;
}
