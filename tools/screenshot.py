#!/usr/bin/env python3
"""Snimak ekrana panela preko USB serijskog porta (debug konzola u firmware-u).

Upotreba:
  ~/.platformio/penv/bin/python tools/screenshot.py izlaz.png [--port /dev/cu.usbserial-XXX] \
      [--cmd "d"] [--cmd "t 400 240"] [--wait 1.0]

--cmd  komanda koja se pošalje pre snimanja (može više puta): d = demo, t X Y = dodir, w X1 Y1 X2 Y2 = prevlačenje,
       p N = podešavanja tab N, m = glavni ekran, pclk N = takt ekrana (MHz)
"""
import argparse
import base64
import struct
import sys
import time
import zlib
from glob import glob

import serial  # pyserial (dolazi uz PlatformIO)


HALF = True  # podrazumevano pola rezolucije (brže)


def default_port():
    ports = sorted(glob("/dev/cu.usbserial-*"))
    return ports[0] if ports else "/dev/cu.usbserial-210"


def write_png(path, w, h, rgb):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgb[y * w * 3 : (y + 1) * w * 3] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def capture(s):
    """Pošalje 's' i vrati (w, h, rgb, ispravno) ili None ako panel ne odgovori.

    Linije su numerisane ("@S <i> <base64>"); oštećene (log drugog taska upao u liniju)
    traže se ponovo komandom "r <i>".
    """
    s.reset_input_buffer()
    s.write(b"s2\n" if HALF else b"s\n")
    w = h = size = count = None
    raw = False
    chunks = {}
    deadline = time.time() + 240
    while time.time() < deadline:
        line = s.readline().decode("ascii", "replace").strip()
        if line.startswith("@H "):
            parts = line.split()
            w, h, size, count = map(int, parts[1:5])
            raw = len(parts) > 5 and parts[5] == "raw"
            chunks.clear()
        elif line.startswith("@S ") and w:
            parse_chunk(line, chunks)
        elif line == "@E" and w:
            break
        elif line.startswith("@ERR"):
            sys.exit(line)
        elif "ESP-ROM" in line or "=== Smart Light Panel" in line:
            print("  UPOZORENJE: ploca se resetovala tokom snimanja", file=sys.stderr)
    if not w:
        return None

    for _ in range(5):  # ponovo traži oštećene/izgubljene linije
        missing = [i for i in range(count) if i not in chunks]
        if not missing:
            break
        for i in missing:
            s.write(f"r {i}\n".encode())
            end = time.time() + 2
            while time.time() < end:
                line = s.readline().decode("ascii", "replace").strip()
                if line.startswith(f"@R {i} ") and parse_chunk(line, chunks):
                    break

    data = b"".join(chunks.get(i, b"") for i in range(count))
    def rgb(col):
        r = (col >> 11) & 0x1F
        g = (col >> 5) & 0x3F
        b = col & 0x1F
        return bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    px = bytearray()
    if raw:  # sirovi RGB565
        for i in range(0, len(data) - 1, 2):
            px += rgb(struct.unpack_from("<H", data, i)[0])
    else:  # RLE: (broj, boja)
        for i in range(0, len(data) - 3, 4):
            run, col = struct.unpack_from("<HH", data, i)
            px += rgb(col) * run
    ok = len(data) == size and len(px) == w * h * 3
    return w, h, px, ok


def parse_chunk(line, chunks):
    parts = line.split(" ", 2)
    if len(parts) != 3 or not parts[1].isdigit():
        return False
    try:
        chunks[int(parts[1])] = base64.b64decode(parts[2], validate=True)
        return True
    except Exception:
        return False  # u liniju je upao log drugog taska


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--port", default=default_port())
    ap.add_argument("--cmd", action="append", default=[])
    ap.add_argument("--wait", type=float, default=1.0, help="pauza posle svake komande (s)")
    ap.add_argument("--full", action="store_true", help="puna rezolucija (sporo kad je fotografija na ekranu)")
    a = ap.parse_args()
    global HALF
    HALF = not a.full

    # Bez reseta ploče: port se otvara sa DTR=RTS=1 (EN visok), pa se prvo spušta RTS, tek onda DTR.
    # (Obrnuti redosled, koji pyserial radi sa dtr=False pre open(), na trenutak spusti EN -> reset.)
    s = serial.Serial(a.port, 115200, timeout=0.5)
    s.rts = False
    s.dtr = False
    time.sleep(0.2)
    s.reset_input_buffer()
    for c in a.cmd:
        s.write((c + "\n").encode())
        time.sleep(a.wait)
    for attempt in range(1, 6):
        result = capture(s)
        if result is None:
            sys.exit("nema odgovora od panela")
        w, h, px, ok = result
        if ok:
            break
        print(f"  pokusaj {attempt}: prenos nepotpun, ponavljam", file=sys.stderr)
    s.close()
    if not ok:
        print("UPOZORENJE: slika je i dalje ostecena", file=sys.stderr)
        px = px[: w * h * 3].ljust(w * h * 3, b"\x00")
    write_png(a.out, w, h, bytes(px))
    print(f"{a.out}: {w}x{h}")


if __name__ == "__main__":
    main()
