#!/usr/bin/env python3
"""
rgba_to_png.py — deterministic, dependency-free raw RGBA -> PNG converter and
evidence probe for the Minecraft 1.21.1 on Mithril E2E lane.

Standard library only (struct + zlib), so it runs on a stock GitHub-hosted
macOS runner with no pip install. Usage:

  python3 rgba_to_png.py <input.rgba> <width> <height> <output.png> [--meta out.json]

Prints one machine-readable line:
  uniform_fill=<f> dominant=<r,g,b> distinct=<n> gray_stddev=<f> png=<path>
"""
import argparse
import json
import os
import pathlib
import statistics
import struct
import sys
import zlib


def write_png(path, width, height, rgba, flip_y=False, opaque=False):
    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    # 8-bit truecolor RGBA (color type 6), no interlace.
    stride = width * 4
    raw = bytearray()
    for row in range(height):
        # PNG scanlines are top-row-first. glReadPixels returns GL convention
        # (first row = bottom-left), so with flip_y the rows are reversed to
        # reproduce the upright on-screen drawable.
        y = (height - 1 - row) if flip_y else row
        raw.append(0)  # filter type 0 (None) for this scanline
        scan = bytearray(rgba[y * stride:(y + 1) * stride])
        if opaque:
            # The CAMetalLayer/swapchain surface carries no meaningful alpha
            # (MoltenVK reports 0 for every pixel). Minecraft draws an opaque
            # frame; force alpha 255 so the evidence PNG is viewable instead of
            # compositing as fully transparent. The raw .rgba is left untouched.
            for a in range(3, len(scan), 4):
                scan[a] = 255
        raw += scan
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("width", type=int)
    ap.add_argument("height", type=int)
    ap.add_argument("output")
    ap.add_argument("--meta", default=None)
    ap.add_argument("--flip-y", action="store_true",
                    help="reverse rows (glReadPixels bottom-first -> PNG top-first)")
    args = ap.parse_args()

    w, h = args.width, args.height
    need = w * h * 4
    data = pathlib.Path(args.input).read_bytes()[:need]
    if len(data) < need:
        print("ERROR: input has %d bytes, expected %d" % (len(data), need), file=sys.stderr)
        return 2

    total = w * h
    counts = {}
    for i in range(0, total * 4, 4):
        key = (data[i], data[i + 1], data[i + 2])
        counts[key] = counts.get(key, 0) + 1
    dom = max(counts, key=counts.get)
    uniform = counts[dom] / total
    gray = [(data[i] + data[i + 1] + data[i + 2]) / 3.0
            for i in range(0, total * 4, 4 * max(1, total // 20000))]
    stddev = statistics.pstdev(gray) if gray else 0.0

    opaque_frac = sum(1 for i in range(3, total * 4, 4)
                      if data[i] == 255) / total
    write_png(args.output, w, h, data, flip_y=args.flip_y, opaque=True)

    print("uniform_fill=%.6f dominant=%s distinct=%d gray_stddev=%.3f png=%s"
          % (uniform, "%d,%d,%d" % dom, len(counts), stddev, args.output))

    if args.meta:
        meta = {
            "schema_version": "1.0",
            "width": w, "height": h, "bytes": need,
            "pixel_format": "RGBA8",
            "capture_source": "native-bridge pre-present glReadPixels",
            "rows_flipped_for_display": bool(args.flip_y),
            "source_alpha_opaque_fraction": opaque_frac,
            "png_alpha_forced_opaque": True,
            "uniform_fill_ratio": uniform,
            "dominant_color_rgb": list(dom),
            "distinct_colors": len(counts),
            "gray_stddev": stddev,
            "png": args.output,
            "github_sha": os.environ.get("GITHUB_SHA", ""),
            "github_run_id": os.environ.get("GITHUB_RUN_ID", ""),
            "renderer_identity": os.environ.get("MITHRIL_RENDERER_IDENTITY", ""),
            "minecraft_version": os.environ.get("MITHRIL_MINECRAFT_VERSION", "1.21.1"),
        }
        pathlib.Path(args.meta).write_text(json.dumps(meta, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
