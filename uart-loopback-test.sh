#!/usr/bin/env bash
# UART loopback test for the Raspberry Pi Debug Probe.
#
# Measures the actual character error rate on the serial link.
#
# USAGE
#   Disconnect BOTH probe UART leads from the RP2040 board.
#   Short the probe's TX wire directly to its RX wire.
#   Then:  ./uart-loopback-test.sh
#
# WHAT IT TELLS YOU
#   0 errors        the probe and its leads are sound; any corruption you see
#                   in normal use is on the board side of those two wires
#   errors > 0      the probe, its cable, or the USB passthrough is dropping
#                   characters, and no firmware change will fix it

PORT="${1:-/dev/ttyACM0}"
BAUD="${2:-115200}"
ROUNDS="${3:-20}"

command -v python3 >/dev/null || { echo "python3 required"; exit 1; }
[ -c "$PORT" ] || { echo "no such port: $PORT"; exit 1; }

if fuser "$PORT" >/dev/null 2>&1; then
    echo "ERROR: $PORT is in use. Close picocom/minicom first:"
    echo "  pkill picocom"
    exit 1
fi

echo "=== UART loopback test ==="
echo "port    $PORT"
echo "baud    $BAUD"
echo "rounds  $ROUNDS"
echo
echo "Probe TX must be shorted to probe RX, both disconnected from the board."
echo

stty -F "$PORT" "$BAUD" cs8 -cstopb -parenb raw -echo clocal min 0 time 5

python3 - "$PORT" "$ROUNDS" << 'PY'
import os, sys, time

port, rounds = sys.argv[1], int(sys.argv[2])

# 94 printable ASCII characters, repeated - exercises every bit pattern
pattern = bytes(range(33, 127))
sent_total = err_total = lost_total = 0

fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
try:
    # flush anything stale
    time.sleep(0.2)
    try:
        os.set_blocking(fd, False)
        while os.read(fd, 4096):
            pass
    except BlockingIOError:
        pass
    os.set_blocking(fd, True)

    for r in range(1, rounds + 1):
        os.write(fd, pattern)
        time.sleep(len(pattern) * 10.0 / 115200 + 0.15)

        got = b""
        deadline = time.time() + 1.0
        os.set_blocking(fd, False)
        while time.time() < deadline and len(got) < len(pattern):
            try:
                chunk = os.read(fd, 4096)
                if chunk:
                    got += chunk
                else:
                    time.sleep(0.01)
            except BlockingIOError:
                time.sleep(0.01)
        os.set_blocking(fd, True)

        lost = len(pattern) - len(got)
        errs = sum(1 for a, b in zip(pattern, got) if a != b)
        sent_total += len(pattern)
        lost_total += max(lost, 0)
        err_total  += errs

        status = "ok" if lost == 0 and errs == 0 else "FAIL"
        print(f"round {r:3}  sent {len(pattern):3}  got {len(got):3}  "
              f"lost {max(lost,0):3}  corrupt {errs:3}   {status}")
        if status == "FAIL" and got:
            for i, (a, b) in enumerate(zip(pattern, got)):
                if a != b:
                    print(f"           first bad byte at {i}: "
                          f"sent 0x{a:02x} '{chr(a)}'  got 0x{b:02x}")
                    break
finally:
    os.close(fd)

bad = lost_total + err_total
print()
print(f"total sent      {sent_total}")
print(f"total lost      {lost_total}")
print(f"total corrupt   {err_total}")
if sent_total:
    print(f"error rate      {100.0*bad/sent_total:.2f}%")
print()
if bad == 0:
    print("RESULT: PASS - probe and leads are clean.")
    print("Any corruption in normal use is on the board side of these wires:")
    print("  probe TX -> board GP1, probe RX -> board GP0, and a common ground.")
else:
    print("RESULT: FAIL - the probe, its cable or the USB passthrough is at fault.")
    print("Try: reseat the leads, replug the probe, re-attach it in UTM,")
    print("     shorten the wires, or try a different USB port.")
PY
