#!/usr/bin/env python3
"""Resetuje ploču i ispisuje serijski log N sekundi (neinteraktivna zamena za `pio device monitor`).

Upotreba:  ~/.platformio/penv/bin/python tools/serial_log.py [port] [sekunde]
"""
import sys
import time
from glob import glob

import serial  # pyserial (dolazi uz PlatformIO)

ports = sorted(glob("/dev/cu.usbserial-*"))
port = sys.argv[1] if len(sys.argv) > 1 else (ports[0] if ports else "/dev/cu.usbserial-210")
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 15

with serial.Serial(port, 115200, timeout=0.2) as s:
    # Reset preko RTS (EN), DTR drži GPIO0 visoko -> normalan boot
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    start = time.time()
    end = start + seconds
    while time.time() < end:
        line = s.readline()
        if line:
            sys.stdout.write(f"{time.time() - start:6.2f}s | " + line.decode("utf-8", "replace"))
            sys.stdout.flush()
