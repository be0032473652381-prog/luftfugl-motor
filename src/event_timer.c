#include "event_timer.h"

#include "buzzer.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "power_monitor.h"
#include "pico/time.h"
#include <stdint.h>
#include <stdio.h>

#define DS3231_REG_TIME 0x00u
#define DS3231_REG_ALARM1 0x07u
#define DS3231_REG_CONTROL 0x0eu
#define DS3231_REG_STATUS 0x0fu
#define DS3231_CONTROL_INTCN 0x04u
#define DS3231_CONTROL_A1IE 0x01u
#define DS3231_STATUS_A1F 0x01u
#define DS3231_ALARM_DY_DT 0x40u

static volatile bool alarm_pending;
static bool timer_enabled;
static bool alert_active;
static bool alert_lit;
static uint8_t alert_transitions;
static uint32_t alert_deadline_ms;
static uint32_t alarm_target_week_seconds;

static uint8_t bcd_to_binary(uint8_t value) {
  return (uint8_t)((value >> 4u) * 10u + (value & 0x0fu));
}

static uint8_t binary_to_bcd(uint8_t value) {
  return (uint8_t)(((value / 10u) << 4u) | (value % 10u));
}

static bool read_registers(uint8_t reg, uint8_t *data, size_t size) {
  return i2c_write_blocking(i2c0, DS3231_ADDRESS, &reg, 1u, true) == 1 &&
         i2c_read_blocking(i2c0, DS3231_ADDRESS, data, size, false) ==
             (int)size;
}

static bool write_registers(uint8_t reg, const uint8_t *data, size_t size) {
  uint8_t frame[5];
  if (size > sizeof frame - 1u)
    return false;
  frame[0] = reg;
  for (size_t i = 0u; i < size; ++i)
    frame[i + 1u] = data[i];
  return i2c_write_blocking(i2c0, DS3231_ADDRESS, frame, size + 1u, false) ==
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
    seconds += EVENT_TIMER_INTERVAL_MINUTES * 60u;
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

static void gpio_irq(uint gpio, uint32_t events) {
  if (gpio == PIN_DS3231_INT && (events & GPIO_IRQ_EDGE_FALL))
    alarm_pending = true;
}

static void alert_start(void) {
  alert_active = true;
  alert_lit = true;
  alert_transitions = 0u;
  alert_deadline_ms =
      to_ms_since_boot(get_absolute_time()) + EVENT_TIMER_ALERT_ON_MS;
  gpio_put(PIN_ONBOARD_LED, true);
  buzzer_tone_sequence(EVENT_TIMER_BUZZER_HZ, EVENT_TIMER_ALERT_ON_MS,
                       EVENT_TIMER_ALERT_COUNT, EVENT_TIMER_ALERT_OFF_MS);
}

void event_timer_init(void) {
  gpio_init(PIN_ONBOARD_LED);
  gpio_put(PIN_ONBOARD_LED, false);
  gpio_set_dir(PIN_ONBOARD_LED, GPIO_OUT);
  gpio_init(PIN_DS3231_INT);
  gpio_set_dir(PIN_DS3231_INT, GPIO_IN);
  gpio_pull_up(PIN_DS3231_INT);
  alarm_pending = false;
  alert_active = false;
  timer_enabled = arm_next_alarm();
  gpio_set_irq_enabled_with_callback(PIN_DS3231_INT, GPIO_IRQ_EDGE_FALL, true,
                                     gpio_irq);
  if (timer_enabled && !gpio_get(PIN_DS3231_INT))
    alarm_pending = true;
}

void event_timer_poll(void) {
  if (timer_enabled && alarm_pending) {
    if (arm_next_alarm()) {
      alarm_pending = false;
      alert_start();
    }
  }
  if (!alert_active)
    return;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if ((int32_t)(now - alert_deadline_ms) < 0)
    return;
  alert_lit = !alert_lit;
  gpio_put(PIN_ONBOARD_LED, alert_lit);
  ++alert_transitions;
  if (alert_transitions >= EVENT_TIMER_ALERT_COUNT * 2u - 1u) {
    alert_active = false;
    gpio_put(PIN_ONBOARD_LED, false);
    return;
  }
  alert_deadline_ms = now +
      (alert_lit ? EVENT_TIMER_ALERT_ON_MS : EVENT_TIMER_ALERT_OFF_MS);
}

bool event_timer_stop(char *detail, size_t size) {
  if (!timer_enabled) {
    snprintf(detail, size, "event timer already stopped");
    return true;
  }
  if (!power_monitor_i2c_claim()) {
    snprintf(detail, size, "I2C busy; retry");
    return false;
  }
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
  if (!ok) {
    snprintf(detail, size, "failed to stop DS3231 event timer");
    return false;
  }
  timer_enabled = false;
  alarm_pending = false;
  gpio_set_irq_enabled(PIN_DS3231_INT, GPIO_IRQ_EDGE_FALL, false);
  gpio_put(PIN_ONBOARD_LED, false);
  if (alert_active)
    buzzer_tone_stop();
  alert_active = false;
  snprintf(detail, size, "event timer stopped until reset");
  return true;
}

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
