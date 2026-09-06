#!/usr/bin/env python3
"""Exercise the actual arrival queue/dispatch in both build configurations, without motion."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/console.c').read_text()
start = source.index('void console_push_event(')
end = source.index('void console_watchdog_reset(', start)
prefix = r'''
#include "config.h"
#include "buzzer_compact.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { event_kind_t kind; uint8_t arg; } event_t;
static volatile event_t event_queue[EVENT_QUEUE_DEPTH];
static volatile uint8_t event_head, event_tail;
static unsigned int calls[64], used;
static bool monitor_active;
static unsigned int monitor_events;
bool buzzer_play_5(unsigned int count) { calls[used++] = count; return true; }
static void write_line(const char *text) { (void)text; }
static void console_adc(void) {}
#ifdef LUFTFUGL_MONITOR
static bool dbg_active(void) { return monitor_active; }
static void dbg_event(event_kind_t kind, uint8_t arg) {
  (void)kind; (void)arg; ++monitor_events;
}
#endif
'''
suffix = r'''
int main(void) {
  for (unsigned int mode = 0u; mode < 2u; ++mode) {
    used = 0u; monitor_events = 0u; monitor_active = mode != 0u;
    for (unsigned int station = 1u; station <= 6u; ++station) {
      unsigned int before = used;
      console_push_event(EV_PASS, station);
      console_drain_events();
      assert(used == before);
      console_push_event(EV_ARRIVE, station);
      console_drain_events();
      if (station >= 2u && station <= 5u) {
        assert(used == before + 1u && calls[before] == station - 1u);
      } else assert(used == before);
    }
    assert(used == 4u);
    console_push_event(EV_TIMEOUT, 3u);
    console_push_event(EV_HOMING, 2u);
    console_drain_events();
    assert(used == 4u);
#ifdef LUFTFUGL_MONITOR
    assert(monitor_events == (monitor_active ? 14u : 0u));
#endif
  }
  puts("PASS: actual station dispatch uses crips-5 counts 1/2/3/4 only on arrivals at 2/3/4/5; 1/6 and non-arrivals silent; monitor routing preserved");
}
'''
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'station.c'
    path.write_text(prefix + source[start:end] + suffix)
    for debug in (False, True):
        command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                   '-fsanitize=address,undefined', '-I'+str(root/'src')]
        if debug:
            command += ['-DLUFTFUGL_DEBUG=1', '-DLUFTFUGL_MONITOR=1']
        command += [str(path), '-o', str(Path(d)/'station')]
        subprocess.run(command, check=True)
        subprocess.run([str(Path(d)/'station')], check=True)
