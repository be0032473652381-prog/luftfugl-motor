#!/usr/bin/env python3
"""Compile the actual endstop persistence code against checked flash stubs."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


def main():
    source = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else (
        ROOT / 'src/debug.c').read_text()
    start = source.index('#define ENDSTOP_SCRATCH_MAGIC')
    record = source[start:source.index('\ntypedef enum {', start)]
    code = r'''
#include "config.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (4u * 1024u * 1024u)
static union { max_align_t align; uint8_t bytes[FLASH_SECTOR_SIZE]; } storage;
#define XIP_BASE ((uintptr_t)storage.bytes - ENDSTOP_FLASH_OFFSET)
static struct { uint32_t scratch[8]; } watchdog;
#define watchdog_hw (&watchdog)
volatile cfg_t cfg;
static bool interrupts_disabled;
static unsigned erases, programs;
static uint32_t save_and_disable_interrupts(void) {
  assert(!interrupts_disabled);
  interrupts_disabled = true;
  return 42u;
}
static void restore_interrupts(uint32_t state) {
  assert(interrupts_disabled && state == 42u);
  interrupts_disabled = false;
}
static void flash_range_erase(uint32_t offset, size_t size) {
  assert(interrupts_disabled && offset == 0x3ff000u);
  assert(size == FLASH_SECTOR_SIZE);
  memset(storage.bytes, 0xff, size);
  ++erases;
}
static void flash_range_program(uint32_t offset, const uint8_t *data, size_t size) {
  assert(interrupts_disabled && offset == 0x3ff000u);
  /* This assertion reproduces the original 16-byte write failure. */
  assert(size == FLASH_PAGE_SIZE);
  assert(offset % FLASH_PAGE_SIZE == 0u);
  memcpy(storage.bytes, data, size);
  ++programs;
}
''' + record + ''.join(function(source, signature) for signature in (
        'static bool endstop_values_valid(',
        'static void endstop_restore(',
        'static void endstop_persist(',
        'static void endstop_clear_persist(')) + r'''
static void reset_ram(void) {
  cfg.pos_1_adc = POS_1_ADC;
  cfg.pos_6_adc = POS_6_ADC;
  cfg.low_endstop_adc = LOW_ENDSTOP_ADC;
  cfg.high_endstop_adc = HIGH_ENDSTOP_ADC;
  memset(&watchdog, 0, sizeof watchdog);
}
int main(void) {
  reset_ram();
  /* Non-default, valid values prove restoration rather than default fallback. */
  cfg.low_endstop_adc = 123u;
  cfg.high_endstop_adc = 3100u;
  endstop_persist();
  assert(erases == 1u && programs == 1u && !interrupts_disabled);
  assert(watchdog.scratch[0] == ENDSTOP_SCRATCH_MAGIC);
  assert(watchdog.scratch[1] == 123u && watchdog.scratch[2] == 3100u);
  assert(offsetof(endstop_record_t, magic) == 0u);
  assert(offsetof(endstop_record_t, low) == 4u);
  assert(offsetof(endstop_record_t, high) == 6u);
  assert(offsetof(endstop_record_t, checksum) == 8u);
  assert(offsetof(endstop_record_t, reserved) == 12u);
  for (size_t i = 16u; i < FLASH_SECTOR_SIZE; ++i)
    assert(storage.bytes[i] == 0xffu);
  reset_ram();
  endstop_restore();
  assert(cfg.low_endstop_adc == 123u && cfg.high_endstop_adc == 3100u);
  puts("PASS: full 256-byte write at 0x3FF000; flash restore without scratch fallback");

  /* An independently encoded legacy 16-byte header remains readable. */
  const uint32_t legacy[4] = {ENDSTOP_FLASH_MAGIC, (3200u << 16) | 150u,
                             ENDSTOP_FLASH_MAGIC ^ 150u ^ 3200u, 0u};
  memset(storage.bytes, 0xa5, sizeof storage.bytes);
  memcpy(storage.bytes, legacy, sizeof legacy);
  reset_ram();
  endstop_restore();
  assert(cfg.low_endstop_adc == 150u && cfg.high_endstop_adc == 3200u);
  puts("PASS: original field offsets and legacy header compatibility; padding ignored");

  storage.bytes[8] ^= 1u;
  reset_ram();
  endstop_restore();
  assert(cfg.low_endstop_adc == LOW_ENDSTOP_ADC);
  assert(cfg.high_endstop_adc == HIGH_ENDSTOP_ADC);
  watchdog.scratch[0] = ENDSTOP_SCRATCH_MAGIC;
  watchdog.scratch[1] = 120u;
  watchdog.scratch[2] = 3050u;
  endstop_restore();
  assert(cfg.low_endstop_adc == 120u && cfg.high_endstop_adc == 3050u);
  const uint32_t invalid[4] = {ENDSTOP_FLASH_MAGIC, (3200u << 16) | 201u,
                              ENDSTOP_FLASH_MAGIC ^ 201u ^ 3200u, 0u};
  memcpy(storage.bytes, invalid, sizeof invalid);
  reset_ram();
  endstop_restore();
  assert(cfg.low_endstop_adc == LOW_ENDSTOP_ADC);
  assert(cfg.high_endstop_adc == HIGH_ENDSTOP_ADC);
  assert(!endstop_values_valid(100u, 2999u));
  assert(!endstop_values_valid(3000u, 100u));
  endstop_clear_persist();
  assert(erases == 2u && programs == 1u && !interrupts_disabled);
  assert(watchdog.scratch[0] == 0u);
  for (size_t i = 0u; i < FLASH_SECTOR_SIZE; ++i)
    assert(storage.bytes[i] == 0xffu);
  puts("PASS: checksum and limit validation, scratch fallback and erase preserved");
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        cfile, binary = Path(tmp) / 'endstop.c', Path(tmp) / 'endstop'
        cfile.write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-DLUFTFUGL_MONITOR=1', '-I' + str(ROOT / 'src'),
                        str(cfile), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
