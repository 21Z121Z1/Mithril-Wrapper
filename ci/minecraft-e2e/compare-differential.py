#!/usr/bin/env python3
"""Coarse structural comparator for native OpenGL vs Mithril DirectMetal captures.

The goal is not bit-identical rasterization. We compare static-world structure at
coarse spatial scales and allow each Mithril frame to choose the closest native
frame in the same camera phase (frames 1-4 or 5-8), absorbing harmless animation
or swap-timing jitter while still catching missing terrain, giant wedges, bands,
orientation mistakes, and large coverage changes.
"""
import argparse
import json
import math
from pathlib import Path


def state(root: Path):
    return json.loads((root / "differential-state.json").read_text())


def read_frame(root: Path, frame: int):
    stem = root / "render" / f"prepresent-frame-{frame:04d}"
    w, h, declared = map(int, (stem.with_suffix(".meta")).read_text().split())
    raw = stem.with_suffix(".rgba").read_bytes()
    if len(raw) != declared or declared != w * h * 4:
        raise SystemExit(f"invalid frame {stem}: {w}x{h} declared={declared} actual={len(raw)}")
    return w, h, raw


def feature(w, h, raw, tiles_x=16, tiles_y=9):
    # Crop a small border and the lowest 12% where HUD/hand animation dominates.
    x0, x1 = int(w * 0.05), int(w * 0.95)
    y0, y1 = int(h * 0.12), int(h * 0.95)  # raw coordinates are GL lower-left
    rgb = []
    lum = []
    for ty in range(tiles_y):
        ya = y0 + (y1 - y0) * ty // tiles_y
        yb = y0 + (y1 - y0) * (ty + 1) // tiles_y
        for tx in range(tiles_x):
            xa = x0 + (x1 - x0) * tx // tiles_x
            xb = x0 + (x1 - x0) * (tx + 1) // tiles_x
            sr = sg = sb = n = 0
            # 2-pixel sampling keeps the comparator cheap while deterministic.
            for y in range(ya, max(ya + 1, yb), 2):
                for x in range(xa, max(xa + 1, xb), 2):
                    i = (y * w + x) * 4
                    sr += raw[i]
                    sg += raw[i + 1]
                    sb += raw[i + 2]
                    n += 1
            r, g, b = sr / n, sg / n, sb / n
            rgb.extend((r, g, b))
            lum.append(0.2126 * r + 0.7152 * g + 0.0722 * b)

    dark_rows = []
    xa, xb = int(w * 0.10), int(w * 0.90)
    for y in range(h):
        dark = n = 0
        for x in range(xa, xb, 4):
            i = (y * w + x) * 4
            l = 0.2126 * raw[i] + 0.7152 * raw[i + 1] + 0.0722 * raw[i + 2]
            dark += l < 24.0
            n += 1
        dark_rows.append(dark / max(1, n))
    return rgb, lum, dark_rows


def pearson(a, b):
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    if va < 1e-9 or vb < 1e-9:
        return 1.0 if all(abs(x - y) < 1e-6 for x, y in zip(a, b)) else 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def distance(a, b):
    ar, al, ad = a
    br, bl, bd = b
    rmse = math.sqrt(sum((x - y) ** 2 for x, y in zip(ar, br)) / len(ar)) / 255.0
    corr = pearson(al, bl)
    dark_mae = sum(abs(x - y) for x, y in zip(ad, bd)) / len(ad)
    return rmse, corr, dark_mae


def compare(native_root: Path, mithril_root: Path, label: str):
    ns, ms = state(native_root), state(mithril_root)
    if ns.get("backend") != "native" or ms.get("backend") != "mithril":
        raise SystemExit(f"{label}: backend identities are not native/mithril: {ns.get('backend')}/{ms.get('backend')}")
    if ns.get("sodium_loaded") != ms.get("sodium_loaded"):
        raise SystemExit(f"{label}: sodium load state differs")
    if bool(ns.get("sodium_loaded")) != (label == "sodium"):
        raise SystemExit(f"{label}: unexpected Sodium load state")
    if ns["width"] != ms["width"] or ns["height"] != ms["height"]:
        raise SystemExit(f"{label}: framebuffer dimensions differ: {ns['width']}x{ns['height']} vs {ms['width']}x{ms['height']}")

    nf = {}
    mf = {}
    for i in range(1, 9):
        nw, nh, nr = read_frame(native_root, i)
        mw, mh, mr = read_frame(mithril_root, i)
        if (nw, nh) != (mw, mh):
            raise SystemExit(f"{label}: frame {i} dimensions differ")
        nf[i] = feature(nw, nh, nr)
        mf[i] = feature(mw, mh, mr)

    matches = []
    for mi in range(1, 9):
        candidates = range(1, 5) if mi <= 4 else range(5, 9)
        scored = []
        for ni in candidates:
            rmse, corr, dark = distance(nf[ni], mf[mi])
            # Ranking prioritizes coarse color/coverage, then row darkness.
            scored.append((rmse + 0.20 * dark, ni, rmse, corr, dark))
        _, ni, rmse, corr, dark = min(scored)
        matches.append({"mithril_frame": mi, "native_frame": ni,
                        "coarse_rgb_nrmse": rmse, "luma_corr": corr,
                        "dark_row_mae": dark})

    def median(name):
        xs = sorted(x[name] for x in matches)
        return (xs[3] + xs[4]) / 2.0

    summary = {
        "schema_version": "1.0",
        "label": label,
        "native_gl": {k: ns.get(k, "") for k in ("gl_vendor", "gl_renderer", "gl_version", "sodium_version")},
        "mithril_gl": {k: ms.get(k, "") for k in ("gl_vendor", "gl_renderer", "gl_version", "sodium_version")},
        "matches": matches,
        "median_coarse_rgb_nrmse": median("coarse_rgb_nrmse"),
        "median_luma_corr": median("luma_corr"),
        "median_dark_row_mae": median("dark_row_mae"),
    }
    out = mithril_root.parent / f"comparison-{label}.json"
    out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", required=True, type=Path)
    ap.add_argument("--mithril", required=True, type=Path)
    ap.add_argument("--label", choices=("vanilla", "sodium"), required=True)
    ap.add_argument("--assert-pass", action="store_true")
    args = ap.parse_args()
    s = compare(args.native, args.mithril, args.label)
    if args.assert_pass:
        failures = []
        if s["median_coarse_rgb_nrmse"] > 0.28:
            failures.append("coarse_rgb_nrmse")
        if s["median_luma_corr"] < 0.45:
            failures.append("luma_corr")
        if s["median_dark_row_mae"] > 0.30:
            failures.append("dark_row_mae")
        if failures:
            raise SystemExit("DIFFERENTIAL_RENDER_MISMATCH: " + ",".join(failures))
        print("DIFFERENTIAL_RENDER_MATCH: structural thresholds passed")


if __name__ == "__main__":
    main()
