#!/usr/bin/env python3
"""Exercise the real frame renderer, field updates and DS3231 footer in a terminal."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


def main():
    source = (ROOT / 'src/debug.c').read_text()
    # Compile the actual temperature-refresh block as well as the frame writer.
    start = source.index('  if (ds3231_temperature_valid && ui_page != 7u) {')
    end = source.index('\n  }', start) + 4
    temperature = 'static void temperature_refresh(void) {\n' + source[start:end] + '\n}\n'
    code = r'''
#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static bool plain_mode, command_dirty, frame_measuring, page6_holds_last_input;
static bool ds3231_temperature_valid;
static uint8_t ui_page = 6u, frame_phase, input_len;
static uint32_t frame_bytes_last, frame_bytes_current, frame_draw_count;
static uint16_t field_bytes_last;
static char input[49], page6_last_input[49], ds3231_temperature_text[81];
static char status_shadow[30][81];
static uint16_t out_free(void) { return DEBUG_OUT_BUFFER - 1u; }
static void dbg_out_push(const char *text) { fputs(text, stdout); }
static bool status_frame_complete(void) { return true; }
static void dbg_fields_refresh(void) {}
''' + ''.join(function(source, signature) for signature in (
        'static uint8_t command_row(', 'static uint8_t event_top_row(',
        'static void field(', 'static void ds3231_timer_draw(')
    ) + temperature + function(source, 'static void frame_continue(void) {') + r'''
static void render(void) {
  memset(status_shadow, 0, sizeof status_shadow);
  frame_phase = 1u;
  while (frame_phase) frame_continue();
}
int main(void) {
  (void)command_row();
  render();
  /* First reading, then a redraw with cached temperature (leaving help or
     changing pages); these used to write two rows and hide four commands. */
  ds3231_temperature_valid = true;
  strcpy(ds3231_temperature_text, " DS3231 temperature 30.75 C (raw 1E C0)");
  temperature_refresh();
  render();
  temperature_refresh();
  puts("\nCHECKPOINT\n");
  ds3231_timer_draw("DS3231 countdown 00:30:00");
  strcpy(ds3231_temperature_text, " DS3231 temperature 31.00 C (raw 1F 00)");
  status_shadow[DEBUG_PAGE6_DS3231_TEMP_ROW - 1u][0] = '\0';
  temperature_refresh();
  temperature_refresh();
  puts("\nCHECKPOINT\n");
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        cfile, binary = Path(tmp) / 'page6.c', Path(tmp) / 'page6'
        cfile.write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-DLUFTFUGL_MONITOR=1', '-I' + str(ROOT / 'src'),
                        str(cfile), '-o', str(binary)], check=True)
        output = subprocess.check_output([str(binary)], text=True)
    screen = [''] * 100
    row, col, saved = 0, 0, (0, 0)
    for stage, chunk in enumerate(output.split('\nCHECKPOINT\n')[:-1]):
        for token in re.split(r'(\x1b\[[0-9;]*[A-Za-z])', chunk):
            if not token: continue
            if not token.startswith('\x1b['):
                screen[row] = screen[row][:col] + token + screen[row][col+len(token):]
                col += len(token)
            elif token.endswith('H'):
                r, c = map(int, token[2:-1].split(';'))
                row, col = r-1, c-1
            elif token == '\x1b[s': saved = row, col
            elif token == '\x1b[u': row, col = saved
            elif token == '\x1b[K': screen[row] = screen[row][:col]
            elif token.endswith('r'): pass
            else: raise AssertionError(token)
        copies = sum('DS3231 temperature' in line for line in screen)
        assert copies == 1, f'cached temperature rendered {copies} times after redraw'
        assert all(word in screen[21] for word in ('step', 'stop', 'tick', 'trace')), screen[21]
        assert 'Help: help <command>' in screen[22]
        assert 'Command >' in screen[25]
        if stage:
            assert '31.00 C' in screen[23] and '30.75 C' not in '\n'.join(screen)
            assert 'countdown 00:30:00' in screen[24]
        print(f'PASS: Page 6 stage {stage+1}: one temperature, complete command list, separate timer and prompt')


if __name__ == '__main__':
    main()
