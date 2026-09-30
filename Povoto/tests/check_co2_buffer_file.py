"""data/co2buffers.bin reserves in the filesystem image the space of the CO2
buffers file written by the firmware (PressureControl.cpp, saveCO2Buffers).
After a filesystem upload the firmware finds it all zeros, rejects it
("placeholder") and replaces it at the first save.

Checks that its size equals the largest file the firmware can write, from the
packed structs and the buffer sizes. Run: python tests/check_co2_buffer_file.py
Pass --write to (re)create the placeholder with the right size.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/PressureControl.cpp").read_text(encoding="utf-8")
SIZES = {"uint8_t": 1, "uint16_t": 2, "uint32_t": 4, "float": 4, "double": 8}


def struct_size(name):
    body = re.search(r"struct " + name + r" \{(.*?)\} __attribute__\(\(packed\)\);", src, re.S)[1]
    body = re.sub(r"//[^\n]*", "", body)
    total = 0
    for decl in body.split(";"):
        decl = decl.strip()
        if not decl:
            continue
        m = re.match(r"(\w+)\s+(.*)", decl)
        total += SIZES[m[1]] * len([v for v in m[2].split(",") if v.strip()])
    return total


def constant(name):
    return int(re.search(name + r" = (\d+);", src)[1])


expected = (struct_size("CO2BufferHeader")
            + constant("CO2_EVOLUTION_HISTORY_SIZE") * struct_size("CO2SampleFile")
            + constant("HENRY_MEAN_SAMPLES") * struct_size("HenrySampleFile"))
placeholder = root / "data/co2buffers.bin"
if "--write" in sys.argv:
    placeholder.write_bytes(bytes(expected))
actual = placeholder.stat().st_size if placeholder.exists() else None
print(f"largest CO2 buffers file: {expected} bytes; data/co2buffers.bin: {actual}")
ok = actual == expected and placeholder.read_bytes() == bytes(expected)
print("RESULT: PASS" if ok else "RESULT: FAIL (run with --write)")
sys.exit(0 if ok else 1)
