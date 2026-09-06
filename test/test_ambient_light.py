#!/usr/bin/env python3
"""Run the actual VEML7700 driver against a timed, fault-injecting I2C fake."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "hardware/i2c.h": r"""
#ifndef TEST_I2C_H
#define TEST_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { int unused; } i2c_inst_t;
extern i2c_inst_t *i2c0;
int i2c_write_timeout_us(i2c_inst_t *, uint8_t, const uint8_t *, size_t, bool, uint32_t);
int i2c_read_timeout_us(i2c_inst_t *, uint8_t, uint8_t *, size_t, bool, uint32_t);
#endif
""",
    "pico/time.h": r"""
#ifndef TEST_TIME_H
#define TEST_TIME_H
#include <stdint.h>
uint64_t time_us_64(void);
#endif
""",
}

HARNESS = r"""
#include "ambient_light.h"
#include "config.h"
#include "hardware/i2c.h"
#include <assert.h>
#include <stdio.h>

static i2c_inst_t bus;
i2c_inst_t *i2c0 = &bus;
static uint64_t now, activated;
static bool owned, busy, absent, read_failure, shutdown_failure, wrong_readback;
static uint16_t config_reg, counts;
static uint8_t pointer;
static unsigned transactions, starts, stops, reads, psm_writes;
uint64_t time_us_64(void) { return now; }
bool power_monitor_i2c_claim(void) {
  assert(!owned);
  if (busy) return false;
  owned = true;
  return true;
}
void power_monitor_i2c_release(void) { assert(owned); owned = false; }
int i2c_write_timeout_us(i2c_inst_t *port, uint8_t addr, const uint8_t *bytes,
                         size_t size, bool nostop, uint32_t timeout) {
  assert(port == i2c0 && addr == 0x10 && owned && timeout == 5000);
  ++transactions;
  if (absent) return -1;
  pointer = bytes[0];
  if (size == 1) { assert(nostop); return 1; }
  assert(size == 3 && !nostop);
  uint16_t value = bytes[1] | ((uint16_t)bytes[2] << 8);
  if (pointer == 3) { assert(value == 0); ++psm_writes; }
  else {
    assert(pointer == 0);
    if (value & 1) {
      ++stops;
      if (shutdown_failure) return -1;
      assert(value == 0x1001);
    } else {
      assert(value == 0x1000);
      assert(config_reg == 0x1001 && psm_writes);
      activated = now;
      ++starts;
    }
    config_reg = value;
  }
  return 3;
}
int i2c_read_timeout_us(i2c_inst_t *port, uint8_t addr, uint8_t *bytes,
                        size_t size, bool nostop, uint32_t timeout) {
  assert(port == i2c0 && addr == 0x10 && owned && size == 2 && !nostop);
  assert(timeout == 5000);
  ++transactions;
  if (absent) return -1;
  uint16_t value;
  if (pointer == 4) {
    assert(config_reg == 0x1000 && now - activated >= 132500);
    ++reads;
    if (read_failure) return -1;
    value = counts;
  } else {
    assert(pointer == 0);
    value = wrong_readback ? 0 : config_reg;
  }
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  return 2;
}
static ambient_light_sample_t snapshot(void) {
  ambient_light_sample_t result;
  ambient_light_snapshot(&result);
  assert(!owned);
  return result;
}
static void measure(uint16_t raw) {
  counts = raw;
  ambient_light_request_sample();
  ambient_light_poll();
  assert(snapshot().measuring);
  now += VEML7700_READY_US;
  ambient_light_poll();
}
int main(void) {
  ambient_light_init();
  assert(snapshot().shutdown_verified && config_reg == 0x1001);
  assert(!snapshot().valid);
  unsigned before = transactions;
  for (unsigned i = 0; i < 2000; ++i) { ++now; ambient_light_poll(); }
  assert(transactions == before); /* no autonomous polling */
  busy = true;
  ambient_light_request_sample();
  ambient_light_poll();
  assert(transactions == before);
  busy = false;
  counts = 100;
  ambient_light_poll();
  assert(snapshot().measuring && !snapshot().shutdown_verified);
  before = transactions;
  now += VEML7700_READY_US - 1;
  ambient_light_poll();
  assert(transactions == before);
  ++now;
  busy = true;
  ambient_light_poll();
  assert(transactions == before);
  busy = false;
  ambient_light_poll();
  assert(snapshot().valid && snapshot().millilux == 53760);
  assert(snapshot().samples == 1 && snapshot().shutdown_verified);
  assert(reads == 1 && starts == 1);
  measure(0x1234); /* verifies low-byte-first assembly */
  assert(snapshot().raw == 0x1234 && snapshot().millilux > 2500000);
  measure(65535); /* sunlight/saturation must not wrap to a dark reading */
  assert(snapshot().millilux > 35000000 && snapshot().millilux < 1000000000);
  measure(0);
  assert(snapshot().valid && snapshot().millilux == 0);
  read_failure = true;
  before = stops;
  measure(100);
  assert(!snapshot().valid && snapshot().shutdown_verified && stops > before);
  read_failure = false;
  ambient_light_request_sample();
  ambient_light_poll();
  shutdown_failure = true;
  now += VEML7700_READY_US;
  ambient_light_poll();
  assert(!snapshot().valid && !snapshot().shutdown_verified);
  before = transactions;
  for (unsigned i = 0; i < 1000; ++i) ambient_light_poll();
  assert(transactions == before);
  shutdown_failure = false;
  measure(100);
  assert(snapshot().valid && snapshot().shutdown_verified);
  wrong_readback = true;
  measure(100);
  assert(!snapshot().valid && !snapshot().shutdown_verified);
  wrong_readback = false;
  measure(100);
  assert(snapshot().valid);
  absent = true;
  ambient_light_request_sample();
  ambient_light_poll();
  assert(!snapshot().valid && !snapshot().shutdown_verified);
  before = transactions;
  for (unsigned i = 0; i < 1000; ++i) ambient_light_poll();
  assert(transactions == before);
  absent = false;
  measure(100);
  assert(snapshot().valid && snapshot().shutdown_verified);
  ambient_light_init();
  assert(ambient_light_multiplier_percent() == 100);
  /* Near each boundary, both directional margins and consecutive samples
   * matter. These counts straddle 12/8, 120/80 and 600/400 lux. */
  for (unsigned i = 0; i < 6; ++i) measure(20);
  assert(snapshot().instant_zone == AMBIENT_DIM);
  assert(snapshot().confirmed_zone == AMBIENT_NIGHT);
  measure(23); measure(23);
  assert(snapshot().confirmed_zone == AMBIENT_NIGHT);
  measure(20); /* one return into the deadband cancels the candidate */
  assert(snapshot().candidate_samples == 0);
  measure(23); measure(23); measure(23);
  assert(snapshot().confirmed_zone == AMBIENT_DIM);
  for (unsigned i = 0; i < 6; ++i) measure(15);
  assert(snapshot().confirmed_zone == AMBIENT_DIM);
  measure(14); measure(14); measure(14);
  assert(snapshot().confirmed_zone == AMBIENT_NIGHT);
  measure(23); measure(23); measure(23);
  for (unsigned i = 0; i < 6; ++i) measure(223);
  assert(snapshot().confirmed_zone == AMBIENT_DIM);
  measure(224); measure(224); measure(224);
  assert(snapshot().confirmed_zone == AMBIENT_INDOOR);
  for (unsigned i = 0; i < 6; ++i) measure(149);
  assert(snapshot().confirmed_zone == AMBIENT_INDOOR);
  measure(148); measure(148); measure(148);
  assert(snapshot().confirmed_zone == AMBIENT_DIM);
  measure(224); measure(224); measure(224);
  for (unsigned i = 0; i < 6; ++i) measure(1116);
  assert(snapshot().confirmed_zone == AMBIENT_INDOOR);
  measure(1117); measure(1117); measure(1117);
  assert(snapshot().confirmed_zone == AMBIENT_BRIGHT);
  for (unsigned i = 0; i < 6; ++i) measure(745);
  assert(snapshot().confirmed_zone == AMBIENT_BRIGHT);
  measure(744); measure(744); measure(744);
  assert(snapshot().confirmed_zone == AMBIENT_INDOOR);
  measure(0); measure(0); measure(0);
  measure(65535); measure(0);
  assert(snapshot().confirmed_zone == AMBIENT_NIGHT);
  measure(65535); measure(65535);
  read_failure = true;
  measure(65535);
  read_failure = false;
  measure(65535);
  assert(snapshot().confirmed_zone == AMBIENT_NIGHT);
  measure(65535); measure(65535);
  assert(snapshot().confirmed_zone == AMBIENT_BRIGHT);
  assert(ambient_light_multiplier_percent() == 800);
  read_failure = true;
  measure(0);
  assert(snapshot().confirmed_zone == AMBIENT_BRIGHT);
  read_failure = false;
  /* A long WFI interval and the 32-bit microsecond wrap cannot reset zones. */
  now += (UINT64_C(1) << 32);
  measure(65535);
  assert(snapshot().confirmed_zone == AMBIENT_BRIGHT);
  puts("PASS: VEML7700 wire sequence, 132.5 ms minimum, bus arbitration, endian/lux conversion, shutdown readback, failure/reconnect, no independent polling");
  puts("PASS: all three hysteresis boundaries in both directions, three-sample debounce, outlier/error rejection, multi-zone changes and persistence across idle/wrap");
  return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        for name, contents in STUBS.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        harness = temp / "ambient_test.c"
        harness.write_text(HARNESS)
        executable = temp / "ambient_test"
        for debug in (False, True):
            command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-g", "-O1",
                       "-I" + str(temp), "-I" + str(ROOT / "src")]
            if debug:
                command += ["-DLUFTFUGL_DEBUG=1", "-DLUFTFUGL_MONITOR=1"]
            subprocess.run(command + [str(harness), str(ROOT / "src/ambient_light.c"),
                                      "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
