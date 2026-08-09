#!/usr/bin/env python3
"""Convert a mithril render smoke PPM export to PNG.

After running tests/render3d_smoke (which writes tests/render3d.ppm):

    python3 scripts/ppm_render.py tests/render3d.ppm tests/render3d.png

Requires Pillow. Output defaults to <input path>.png when omitted.
"""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow (PIL) required: pip install Pillow", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <in.ppm> [out.png]", file=sys.stderr)
        sys.exit(1)
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_suffix(".png")
    im = Image.open(src).convert("RGB")
    im.save(dst)
    print(f"{dst} ({im.width}x{im.height})")


if __name__ == "__main__":
    main()