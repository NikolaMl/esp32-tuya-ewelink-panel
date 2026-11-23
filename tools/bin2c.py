#!/usr/bin/env python3
"""Pretvara binarni fajl (npr. TTF font) u C niz koji se ugrađuje u firmware.

Upotreba:  python3 tools/bin2c.py <ulaz.ttf> <izlaz.c> <ime_niza>
"""
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src, dst, name = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    if not name.isidentifier():
        sys.exit(f"neispravno ime niza: {name}")
    data = src.read_bytes()
    lines = []
    for i in range(0, len(data), 20):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in data[i : i + 20]) + ",")
    dst.write_text(
        f"// Generisano iz {src.name} pomoću tools/bin2c.py — ne menjati ručno.\n"
        "#include <stddef.h>\n#include <stdint.h>\n\n"
        f"const uint8_t {name}[] = {{\n" + "\n".join(lines) + "\n};\n"
        f"const size_t {name}_len = {len(data)};\n",
        encoding="utf-8",
    )
    print(f"{dst}: {len(data)} bajtova")


if __name__ == "__main__":
    main()
