#!/Users/nasoni/p3/bin/python3
from PIL import Image
import sys

if len(sys.argv) < 3:
    print("Usage: python img2h.py input.png output.h [invert]")
    sys.exit(1)

invert = len(sys.argv) == 4 and sys.argv[3] == "invert"

img = Image.open(sys.argv[1]).convert("L")

if invert:
    img = Image.eval(img, lambda x: 255 - x)

# Convert to 1-bit
img = img.convert("1")

w, h = img.size
pixels = img.load()

name = sys.argv[2].split(".")[0]

with open(sys.argv[2], "w") as f:
    f.write(f"#pragma once\n\n")
    f.write(f"#define ICON_{name.upper()}_WIDTH {w}\n")
    f.write(f"#define ICON_{name.upper()}_HEIGHT {h}\n\n")
    f.write(f"const unsigned char ICON_{name.upper()}_48[] PROGMEM = {{\n")

    for y in range(h):
        byte = 0
        bit = 7
        for x in range(w):
            if pixels[x, y] == 0:
                byte |= (1 << bit)
            bit -= 1
            if bit < 0:
                f.write(f"0x{byte:02X}, ")
                byte = 0
                bit = 7
        if bit != 7:
            f.write(f"0x{byte:02X}, ")
        f.write("\n")

    f.write("};\n")

print("Done")

