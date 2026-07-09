#!/usr/bin/env python3
"""Reset the board via USB-Serial-JTAG and capture boot output for N seconds."""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0

s = serial.Serial(port, 115200, timeout=0.2)
# esptool-style hard reset over USJ
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
end = time.time() + secs
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        sys.stdout.write(chunk.decode("utf-8", errors="replace"))
        sys.stdout.flush()
s.close()
