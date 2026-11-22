from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = root / "assets" / "looppad_splash.png"
dst = root / "src" / "looppad_splash_data.h"

data = src.read_bytes()

with dst.open("w", encoding="ascii") as f:
    f.write("#pragma once\n\n")
    f.write("static const unsigned char looppad_splash_png[] = {\n")

    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")

    f.write("};\n\n")
    f.write(
        f"static const unsigned int looppad_splash_png_len = {len(data)};\n"
    )

print(f"Generated {dst}")
print(f"Embedded PNG: {len(data)} bytes")
