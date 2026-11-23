#!/usr/bin/env python3
"""Pravi pozadinu početnog ekrana: assets/background.rgb565 (800x480, RGB565 little-endian).

Upotreba:
  python3 tools/make_background.py slika.jpg      # fotografija -> iseče na 800x480 (popuni ekran)
  python3 tools/make_background.py --placeholder  # privremena pozadina (gradijent)

Koristi samo macOS `sips` (ugrađen) i Python standardnu biblioteku. Boje se svode na 16 bita
uz Floyd–Steinberg dithering, da nebo i senke ne bi imali vidljive trake.
"""
import os
import struct
import subprocess
import sys
import tempfile

W, H = 800, 480
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "background.rgb565")


def sips_dims(path):
    out = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", path],
                         check=True, capture_output=True, text=True).stdout
    vals = {}
    for line in out.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            vals[k.strip()] = v.strip()
    return int(vals["pixelWidth"]), int(vals["pixelHeight"])


def photo_to_rgb(path):
    """Vrati listu (r, g, b) 800x480: skalira da pokrije ekran, pa iseče sredinu."""
    with tempfile.TemporaryDirectory() as tmp:
        work = os.path.join(tmp, "work.png")
        subprocess.run(["sips", "-s", "format", "png", path, "--out", work], check=True, capture_output=True)
        w, h = sips_dims(work)
        if w / h > W / H:  # šira od ekrana -> visina na 480
            subprocess.run(["sips", "--resampleHeight", str(H), work], check=True, capture_output=True)
        else:
            subprocess.run(["sips", "--resampleWidth", str(W), work], check=True, capture_output=True)
        subprocess.run(["sips", "--cropToHeightWidth", str(H), str(W), work], check=True, capture_output=True)
        bmp = os.path.join(tmp, "out.bmp")
        subprocess.run(["sips", "-s", "format", "bmp", work, "--out", bmp], check=True, capture_output=True)
        return read_bmp(bmp)


def read_bmp(path):
    data = open(path, "rb").read()
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if (w, abs(h)) != (W, H) or bpp not in (24, 32):
        sys.exit(f"neočekivan BMP: {w}x{h}, {bpp} bita")
    step = bpp // 8
    stride = (w * step + 3) & ~3
    px = []
    for y in range(H):
        row = (H - 1 - y) if h > 0 else y  # BMP je obično odozdo nagore
        base = off + row * stride
        for x in range(W):
            b, g, r = data[base + x * step: base + x * step + 3]
            px.append((r, g, b))
    return px


def placeholder():
    """Tamni gradijent (sumrak), dok ne stigne fotografija."""
    top, bottom = (16, 24, 40), (28, 52, 58)
    px = []
    for y in range(H):
        t = y / (H - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        px.extend([c] * W)
    return px


def to_rgb565(px):
    """Floyd–Steinberg dithering na 5/6/5 bita."""
    err = [[0.0, 0.0, 0.0] for _ in range(W * 2)]  # dva reda grešaka
    out = bytearray(W * H * 2)
    bits = (5, 6, 5)
    for y in range(H):
        cur = err[(y % 2) * W:(y % 2) * W + W]
        nxt_i = ((y + 1) % 2) * W
        for i in range(W):
            err[nxt_i + i] = [0.0, 0.0, 0.0]
        for x in range(W):
            r, g, b = px[y * W + x]
            e = cur[x]
            q = []
            for ch, (v, nb) in enumerate(zip((r, g, b), bits)):
                v = min(255.0, max(0.0, v + e[ch]))
                levels = (1 << nb) - 1
                qv = round(v * levels / 255)
                q.append(qv)
                d = v - qv * 255 / levels
                if x + 1 < W:
                    cur[x + 1][ch] += d * 7 / 16
                if y + 1 < H:
                    n = err[nxt_i:nxt_i + W]
                    if x > 0:
                        n[x - 1][ch] += d * 3 / 16
                    n[x][ch] += d * 5 / 16
                    if x + 1 < W:
                        n[x + 1][ch] += d * 1 / 16
            v565 = (q[0] << 11) | (q[1] << 5) | q[2]
            struct.pack_into("<H", out, (y * W + x) * 2, v565)
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    px = placeholder() if sys.argv[1] == "--placeholder" else photo_to_rgb(sys.argv[1])
    data = to_rgb565(px)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "wb").write(data)
    print(f"{os.path.normpath(OUT)}: {W}x{H}, {len(data)} B")


if __name__ == "__main__":
    main()
