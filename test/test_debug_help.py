#!/usr/bin/env python3
"""Check the real structured-help renderer and export reviewable text."""
from pathlib import Path
import ast
import json
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASELINE = json.loads((ROOT / "docs/help-format-baseline.json").read_text())
SETTINGS = ["DUTY_NORMAL", "DUTY_APPROACH", "DUTY_CREEP", "DUTY_MIN",
            "APPROACH_COUNTS", "POS_WINDOW", "DEBOUNCE_MS", "BRAKE_HOLD_MS"] + [
                f"POS_{i}_ADC" for i in range(1, 7)] + ["LOW_ENDSTOP_ADC", "HIGH_ENDSTOP_ADC"]
SAMPLES = [(row[0], 1) for row in BASELINE["table"]] + [
    (row[0], 5) for row in BASELINE["co2"]] + [(key, 1) for key in SETTINGS] + [
    (row[0], 6) for row in BASELINE["table"]]
HARNESS = r'''
#include "debug_help.h"
#include <stdio.h>
#include <stdlib.h>
static void emit(const char *line, void *context) {
  (void)context;
  puts(line);
}
int main(int argc, char **argv) {
  if (argc != 4) return 2;
  const debug_help_document_t *d = debug_help_find(argv[1], atoi(argv[2]));
  if (!d) return 3;
  debug_help_render(d, atoi(argv[3]) != 0, emit, NULL);
  return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def main():
    rendered = {}
    baseline = json.loads((ROOT / "docs/help-format-baseline.json").read_text())
    source = (ROOT / "src/debug.c").read_text()
    table = source[source.index("static const help_entry_t help_entries[]"):
                   source.index("static const char *resolve")]
    names = [ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', table)]
    assert names == [row[0] for row in baseline["table"]] and len(names) == 86
    for signature, old in baseline["dispatch"].items():
        assert function(source, signature) == old, signature
    print("PASS: command resolution and help abbreviation functions unchanged")
    print("PASS: all 86 command names and their resolution order unchanged")
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        (temp / "help.c").write_text(HARNESS)
        binary = temp / "help"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-DLUFTFUGL_MONITOR=1", "-I" + str(ROOT / "src"),
            str(temp / "help.c"), str(ROOT / "src/debug_help.c"),
            "-o", str(binary)], check=True)
        for name, page in SAMPLES:
            forward = subprocess.check_output(
                [str(binary), name, str(page), "0"], text=True)
            reverse = subprocess.check_output(
                [str(binary), name, str(page), "1"], text=True)
            lines = forward.splitlines()
            assert reverse.splitlines() == lines[::-1]
            assert lines[1].startswith(name.upper() + " — ")
            assert lines[0] == "" and lines[2] == "" and lines[3].startswith("SYNTAX    ")
            has_table = any(line.startswith("PARAMETER") for line in lines)
            if name in ("status", "stop", "serial", "batt chirp"):
                assert not has_table
            if name == "status":
                assert not any(line.startswith("INTERACTIONS") for line in lines)
            headings = [line.split()[0] for line in lines if
                        line.startswith(("SYNTAX", "PARAMETER", "INTERACTIONS", "NOTE"))]
            order = ["SYNTAX", "PARAMETER", "INTERACTIONS,", "NOTE"]
            assert headings == sorted(headings, key=order.index)
            assert all(len(line.encode()) < 256 for line in lines)
            rendered[f"{name} (page {page})"] = forward
            print(f"PASS: {name}, page {page}: conditional sections and reverse emission")
        rich = {"status", "led brightness", "batt chirp", "buzzer crips-3",
                "serial", "led", "led zone", "pos", "batt chirp time", "help"}
        for name, example, limits, description in baseline["table"]:
            if name in rich: continue
            actual = " ".join(rendered[f"{name} (page 1)"].lower().split())
            for fact in (example, limits, description):
                assert " ".join(fact.lower().split()) in actual, (name, fact)
        print("PASS: 76 standard entries retain every original example, limit and description")
        for name, old in baseline["co2"]:
            fact = old.split(": ", 1)[1]
            assert fact in rendered[f"{name} (page 5)"], (name, fact)
        assert "controller" in rendered["status (page 1)"]
        assert "SCD41" in rendered["status (page 5)"]
        assert "1 kHz" in rendered["selftest (page 1)"]
        assert "10-second" in rendered["selftest (page 5)"]
        assert all(name in rendered["help (page 1)"] for name in names)
        # Page-sensitive lookup must retain the sensor/controller distinction.
        for name, page in [("unknown", 1)]:
            assert subprocess.run([str(binary), name, str(page), "0"]).returncode == 3
        text = rendered["led brightness (page 1)"]
        for example in ["station 5", "warning 40", "critical 40", "error 25", "sample 15", "breathe 7"]:
            assert "led brightness " + example in text
        for detail in ["3%", "30%", "10%", "0..100", "100%", "warm-white",
                       "orange double", "orange hazard", "no flash is written",
                       "confirmed VEML7700", "station ceiling", "between stations"]:
            assert detail in text
        for name, facts in {
            "led zone": ["0 to <10 lux", "10 to <100 lux", "100 to <500 lux", "500 lux and above",
                         "100% (1.00x)", "200% (2.00x)", "400% (4.00x)", "800% (8.00x)",
                         "0..2000%", "12/120/600", "8/80/400", "three consecutive", "last confirmed",
                         "100%", "30%", "no flash", "shut down", "bright 900"],
            "led": ["GP0", "GP18", "800 kHz", "300 us", "3%", "0x10", "pull-down",
                    "GGRRBBWW", "8 hexadecimal", "GGRRBB", "6 hexadecimal", "Green/mint",
                    "Yellow-green", "Yellow", "Pink", "1/2/3/4", "00ff0000", "battery-cycle"],
            "pos": ["invalid station", "unknown starting position", "already moving", "no wrap-around",
                    "station 6 reserved for CO2 errors", "target-window braking", "directional crossing",
                    "filtered and limit-enforced", "pos 6", "pos <1-6>"],
            "batt chirp time": ["1..3600", "1..10", "1..5", "i=30", "r=2", "p=5", "d=3",
                    "r-1 pauses", "repeat is one", "(d * r) + (p * (r - 1))", "i - sequence duration",
                    "chirp 0-3, pause 3-8, chirp 8-11, silence 11-30", "i=10 r=3 p=2 d=1",
                    "i=60 r=1 p=10 d=4", "i=120 r=1 p=1 d=5 /s", "i=0 r=0 p=0 d=0",
                    "active critical sequence restarts", "no longer critical", "never overlap",
                    "range, warning, critical, frequency, and timing", "power-on default"],
            "batt chirp": ["0.10 to 10.00", "100 to 10000", "2.700", "3.50 kHz /s",
                           "no boundary chirp", "LED auto/on/off/raw", "no valid flash record"],
            "buzzer crips-3": ["1..200", "8/8/12", "50 ms", "444 ms", "100 ms", "off stops"],
        }.items():
            for fact in facts:
                assert fact in rendered[f"{name} (page 1)"], (name, fact)
        print("PASS: detailed LED, position, chirp and sensor facts spot-checked")
        # Exercise the real fixed-screen emitter too. Insert-line escapes must
        # preserve the menu, blank section separators, and wrapped content.
        source = (ROOT / "src/debug.c").read_text()
        start = source.index("static void help_document_emit(")
        end = source.index("\n}\n", start) + 3
        emitter = source[start:end]
        sink = r'''
#include "debug_help.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool plain_mode, first_result;
static uint8_t ui_page;
static void datalog_push(const char *a, const char *b) { (void)a; (void)b; }
static void power_monitor_sim_range_get(uint16_t *a, uint16_t *b) { *a=1500; *b=5500; }
static uint16_t power_monitor_warning_mv(void) { return 3600; }
static uint16_t power_monitor_critical_mv(void) { return 3300; }
static uint16_t encoder_nominal(position_t p) {
  const uint16_t values[] = {CFG_POS_1_ADC, CFG_POS_2_ADC, CFG_POS_3_ADC,
                            CFG_POS_4_ADC, CFG_POS_5_ADC, CFG_POS_6_ADC};
  return values[p-1];
}
static unsigned out_free(void) { return DEBUG_OUT_BUFFER - 1u; }
static void dbg_out_drain(void) {}
static void dbg_out_push(const char *s) { fputs(s, stdout); }
''' + function(source, 'static uint8_t event_top_row(') + emitter + ''.join(function(source, signature) for signature in (
            'static uint16_t angle_tenths(', 'static uint16_t cfg_smallest_gap(',
            'static bool help_live_description(', 'static bool help_show(')) + r'''
int main(int argc, char **argv) {
  if (argc < 4) return 2;
  cfg_reset();
  if (argc > 4) cfg.pos_window = atoi(argv[4]);
  ui_page = atoi(argv[2]);
  plain_mode = atoi(argv[3]);
  return help_show(argv[1], "help") ? 0 : 3;
}
'''
        (temp / "sink.c").write_text(sink)
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-DLUFTFUGL_MONITOR=1", "-I" + str(ROOT / "src"),
            str(temp / "sink.c"), str(ROOT / "src/debug_help.c"), str(ROOT / "src/config.c"),
            "-o", str(binary)], check=True)
        live_rendered = {}
        max_rows = (0, "")
        for name, page in SAMPLES:
            plain_bytes = subprocess.check_output([str(binary), name, str(page), "1"])
            assert plain_bytes.startswith(b"\r\n" + name.upper().encode())
            assert b"\x1b" not in plain_bytes
            logical = plain_bytes.decode().replace("\r\n", "\n")
            live_rendered[f"{name} (page {page})"] = logical
            output = subprocess.check_output([str(binary), name, str(page), "0"], text=True)
            prompt_row = 26 if page == 6 else 24
            screen = [f"preserved row {i}" for i in range(prompt_row)] + [""] * (100 - prompt_row)
            row, column, saved = prompt_row, 0, (prompt_row, 0)
            for token in re.split(r"(\x1b\[[0-9;]*[A-Za-z])", output):
                if not token: continue
                if not token.startswith("\x1b["):
                    screen[row] = screen[row][:column] + token
                    column += len(token)
                    assert column <= 78
                elif token.endswith("H"):
                    r, c = map(int, token[2:-1].split(";"))
                    row, column = r - 1, c - 1
                elif token == "\x1b[s": saved = row, column
                elif token == "\x1b[u": row, column = saved
                elif token == "\x1b[L": screen.insert(row, ""); screen.pop()
                elif token == "\x1b[K": screen[row] = screen[row][:column]
                else: raise AssertionError(token)
            assert screen[prompt_row] == "", (name, "missing blank after prompt")
            assert screen[:prompt_row] == [f"preserved row {i}" for i in range(prompt_row)]
            used_rows = max(i + 1 for i, row in enumerate(screen[prompt_row:]) if row)
            max_rows = max(max_rows, (used_rows, name))
            # Word content must survive wrapping/insertion without omissions.
            assert " ".join("\n".join(screen[prompt_row:]).split()) == " ".join(logical.split()), name
            print(f"PASS: {name}, page {page}: actual ANSI emitter preserves full text and menu")
        for window in (40, 80):
            snapshot = subprocess.check_output([str(binary), "POS_WINDOW", "1", "1", str(window)], text=True)
            assert f"now {window} counts" in snapshot
        assert "warning <3.600 V; critical <3.300 V" in live_rendered["batt (page 1)"]
        assert "1=200, 2=611, 3=1022, 4=1433, 5=1844, 6=3000" in live_rendered["pos (page 1)"]
        print(f"PASS: live snapshots preserved; longest help uses {max_rows[0]}/76 result rows ({max_rows[1]})")
    Path("/tmp/debug-help-live-rendered.json").write_text(json.dumps(live_rendered, indent=2) + "\n")
    Path("/tmp/debug-help-rendered.json").write_text(
        json.dumps(rendered, indent=2, ensure_ascii=False) + "\n")
    print("PASS: 86 commands, 17 Page-5 variants, 16 settings; also all 86 references on Page 6")


if __name__ == "__main__":
    main()
