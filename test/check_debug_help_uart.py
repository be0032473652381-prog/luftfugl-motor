#!/usr/bin/env python3
"""Live, read-only help/prompt regression check; requires sole UART access.

Usage: python3 test/check_debug_help_uart.py /dev/ttyACM4
Requires pyserial. The board must already be running the debug firmware.
Sends page selection, help, editing keys and one invalid command; no motion.
"""
import re
import sys
import time
from pathlib import Path

import serial


PROMPT = re.compile(rb"\x1b\[24;1H Command > ([^\x1b]*)\x1b\[K")


def run(port):
    transcript = bytearray()
    with serial.Serial(port, 115200, timeout=0.05) as uart:
        uart.reset_input_buffer()
        def exchange(keys, expected, title=None):
            uart.write(keys)
            response = bytearray()
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                response.extend(uart.read(4096))
                prompts = PROMPT.findall(response)
                if (prompts and prompts[-1] == expected.encode()
                        and (title is None or title.encode() in response)):
                    transcript.extend(response)
                    return bytes(response)
            transcript.extend(response)
            raise AssertionError(
                f"keys={keys!r}: expected prompt {expected!r}, "
                f"title={title!r}; received {bytes(response)!r}")

        def type_line(text):
            for end, char in enumerate(text, 1):
                response = exchange(char.encode(), text[:end])
                # Typing changes only the prompt; it must preserve the help
                # text and the fixed command index above the prompt.
                assert not PROMPT.sub(b"", response), response

        exchange(b"6", "")
        for command, title in (
                ("help led brightness", "LED BRIGHTNESS —"),
                ("help led zone", "LED ZONE —"),
                ("help ledzone", "LED ZONE —"),
                ("help ledbrightness", "LED BRIGHTNESS —")):
            type_line(command)
            exchange(b"\r", "", title)
            print(f"PASS: {command}: every key echoed; Enter clears prompt")

        type_line("help led zonx")
        exchange(b"\x7f", "help led zon")
        exchange(b"e", "help led zone")
        exchange(b"\r", "", "LED ZONE —")
        type_line("help led")
        for remaining in range(len("help led") - 1, -1, -1):
            exchange(b"\x7f", "help led"[:remaining])
        type_line("help led brightness")
        exchange(b"\r", "", "LED BRIGHTNESS —")
        print("PASS: Backspace edits and clears the prompt after help")

        type_line("unknowncommand argument")
        exchange(b"\r", "", "rejected")
        type_line("help led zone")
        exchange(b"\r", "", "LED ZONE —")
        print("PASS: invalid command with argument is rejected; console stays responsive")
    return transcript


if __name__ == "__main__":
    capture = run(sys.argv[1])
    Path("/tmp/debug-help-prompt-uart.bin").write_bytes(capture)
    print(f"Captured {len(capture)} UART bytes in /tmp/debug-help-prompt-uart.bin")
