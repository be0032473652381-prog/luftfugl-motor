#!/usr/bin/env bash
set -u

PORT="${1:-/dev/ttyACM0}"
BAUD="${2:-115200}"

command -v python3 >/dev/null || { echo "python3 required"; exit 1; }
[ -c "$PORT" ] || { echo "no such port: $PORT"; exit 1; }

if fuser "$PORT" >/dev/null 2>&1; then
    echo "ERROR: $PORT is in use"
    exit 1
fi

stty -F "$PORT" "$BAUD" cs8 -cstopb -parenb raw -echo clocal min 0 time 2

python3 - "$PORT" <<'PY'
import os
import select
import sys
import time

port = sys.argv[1]
test = b"UART0_TEST_7A3C\r"
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

def receive(duration):
    data = bytearray()
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        if chunk:
            data.extend(chunk)
    return bytes(data)

try:
    receive(0.5)
    written = os.write(fd, test)
    received = receive(2.0)
finally:
    os.close(fd)

matched = test in received
print(f"sent bytes      {written}")
print(f"received bytes  {len(received)}")
print(f"test string     {test!r}")
print(f"exact echo      {'yes' if matched else 'no'}")
if received:
    print(f"received data   {received[:512]!r}")
print(f"RESULT: {'PASS' if matched else 'FAIL'}")
raise SystemExit(0 if matched else 1)
PY
