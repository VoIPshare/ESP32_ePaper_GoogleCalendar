#!/Users/nasoni/p3/bin/python3
import cairosvg
import sys

if len(sys.argv) != 3:
    print("Usage: python svg2png.py input.svg output.png")
    sys.exit(1)

cairosvg.svg2png(
    url=sys.argv[1],
    write_to=sys.argv[2],
    output_width=48,
    output_height=48
)

print("SVG converted to PNG")

