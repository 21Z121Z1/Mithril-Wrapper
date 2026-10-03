#!/usr/bin/env python3
import json
import os
import pathlib
import statistics
import subprocess
import sys
import time
import traceback


ROOT = pathlib.Path(os.environ["E2E_ROOT"])
APK = pathlib.Path(os.environ["E2E_APK"])
PKG = "com.mithril.wrapper.e2e"
ACTIVITY = f"{PKG}/.E2EActivity"


def run(args, *, check=True, text=True, timeout=20):
    try:
        p = subprocess.run(
            args,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=text,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        out = exc.stdout or ("" if text else b"")
        if not text and isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        raise RuntimeError(
            f"command timed out after {timeout}s: {args}\n{out}") from exc
    if check and p.returncode != 0:
        output = p.stdout if text else p.stdout.decode("utf-8", "replace")
        raise RuntimeError(f"command failed rc={p.returncode}: {args}\n{output}")
    return p


def adb(*args, check=True, text=True, timeout=20):
    return run(["adb", *args], check=check, text=text, timeout=timeout)


def write_text(name, data):
    (ROOT / name).write_text(data if isinstance(data, str) else str(data), encoding="utf-8")


def capture_text(name, *args):
    p = adb(*args, check=False)
    write_text(name, p.stdout)
    return p


def capture_binary(name, *args):
    p = adb(*args, check=False, text=False)
    (ROOT / name).write_bytes(p.stdout)
    return p


def pull_run_as(remote_name, local_name):
    p = adb("exec-out", "run-as", PKG, "cat", f"files/{remote_name}",
            check=False, text=False, timeout=5)
    if p.returncode != 0 or not p.stdout:
        raise RuntimeError(f"unable to read app-private {remote_name}")
    (ROOT / local_name).write_bytes(p.stdout)


def validate_frame(mode, meta):
    w, h = int(meta["width"]), int(meta["height"])
    if w <= 0 or h <= 0:
        raise RuntimeError(f"{mode}: invalid drawable {w}x{h}")
    if int(meta["visual_id"]) != 1:
        raise RuntimeError(
            f"{mode}: EGL_NATIVE_VISUAL_ID={meta['visual_id']} "
            "expected Android RGBA8888=1")
    if int(meta["swap_count"]) < 2:
        raise RuntimeError(
            f"{mode}: only {meta['swap_count']} successful presents")

    identity = " ".join([
        str(meta.get("egl_vendor", "")),
        str(meta.get("gl_renderer", "")),
        str(meta.get("gl_version", "")),
    ])
    if "Mithril-Wrapper" not in identity:
        raise RuntimeError(f"{mode}: renderer identity mismatch: {identity}")

    raw_path = ROOT / f"frame-{mode}.rgba"
    raw = raw_path.read_bytes()
    need = w * h * 4
    if len(raw) < need:
        raise RuntimeError(
            f"{mode}: short GPU readback {len(raw)} < {need}")
    raw = raw[:need]

    total = w * h
    stride = max(1, total // 12000)
    colors = set()
    gray = []
    for p in range(0, total, stride):
        i = p * 4
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        colors.add((r >> 3, g >> 3, b >> 3))
        gray.append((r + g + b) / 3.0)

    sd = statistics.pstdev(gray) if gray else 0.0
    span = max(gray) - min(gray) if gray else 0.0
    if len(colors) < 48 or sd < 12.0 or span < 55.0:
        raise RuntimeError(
            f"{mode}: framebuffer not convincingly rendered: "
            f"colors={len(colors)} sd={sd:.2f} span={span:.2f}")

    intercepts = int(meta["hook_intercepts"])
    redirects = int(meta["hook_redirects"])
    if mode == "stock":
        if intercepts != -1 or redirects != -1:
            raise RuntimeError(
                "stock: custom-driver route unexpectedly active: "
                f"{intercepts}/{redirects}")
    elif mode == "hook-fallback":
        # The emulator APK deliberately contains no libvulkan_freedreno.so.
        # This mode proves that a unique system libvulkan actually binds to
        # Mithril's HAL hook, that the Turnip lookup fails, and that fallback
        # to the platform emulator driver still renders and presents.
        if intercepts < 1:
            raise RuntimeError(
                "hook-fallback: libvulkan never called the Mithril HAL hook")
        if redirects != 0:
            raise RuntimeError(
                f"hook-fallback: unexpected custom-driver redirect={redirects}")

    summary = {
        "mode": mode,
        "width": w,
        "height": h,
        "sample_quantized_colors": len(colors),
        "gray_stddev": sd,
        "gray_span": span,
        "hook_intercepts": intercepts,
        "hook_redirects": redirects,
        "identity": identity,
    }
    (ROOT / f"validation-{mode}.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    subprocess.run([
        sys.executable,
        "ci/minecraft-e2e/rgba_to_png.py",
        str(raw_path),
        str(w),
        str(h),
        str(ROOT / f"frame-{mode}.png"),
        "--flip-y",
    ], check=True)
    return summary


def run_mode(mode):
    result_name = f"result-{mode}.json"
    frame_name = f"frame-{mode}.rgba"

    adb("shell", "am", "force-stop", PKG, check=False)
    adb("shell", "run-as", PKG, "rm", "-f",
        f"files/{result_name}", f"files/{frame_name}", check=False)
    adb("logcat", "-c", check=False)

    start = adb(
        "shell", "am", "start", "-W",
        "-n", ACTIVITY,
        "--es", "mode", mode,
        check=False,
        timeout=15,
    )
    write_text(f"am-start-{mode}.txt", start.stdout)
    if start.returncode != 0:
        raise RuntimeError(f"{mode}: activity did not start")

    result_bytes = None
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        p = adb("exec-out", "run-as", PKG, "cat",
                f"files/{result_name}", check=False, text=False, timeout=3)
        if p.returncode == 0 and b'"status"' in p.stdout:
            result_bytes = p.stdout
            break
        time.sleep(1)

    capture_text(f"logcat-{mode}.txt", "logcat", "-d", "-v", "threadtime")
    capture_text(f"gfxinfo-{mode}.txt", "shell", "dumpsys", "gfxinfo", PKG)
    capture_text(f"surface-list-{mode}.txt",
                 "shell", "dumpsys", "SurfaceFlinger", "--list")
    capture_text(f"process-{mode}.txt",
                 "shell", "sh", "-c",
                 f"pidof {PKG} || true; ps -A | grep -E 'mithril|{PKG}' || true")
    capture_text(f"activity-{mode}.txt",
                 "shell", "dumpsys", "activity", "activities")
    capture_text(f"tombstones-{mode}.txt",
                 "shell", "sh", "-c",
                 "ls -lt /data/tombstones 2>/dev/null | head -20 || true")
    capture_binary(f"screenshot-{mode}.png", "exec-out", "screencap", "-p")

    if result_bytes is None:
        tail = (ROOT / f"logcat-{mode}.txt").read_text(
            encoding="utf-8", errors="replace").splitlines()[-240:]
        raise RuntimeError(
            f"{mode}: no result.json within timeout\n" + "\n".join(tail))

    (ROOT / result_name).write_bytes(result_bytes)
    pull_run_as(frame_name, frame_name)

    meta = json.loads(result_bytes.decode("utf-8"))
    if meta.get("status") != "pass":
        raise RuntimeError(f"{mode}: native harness failed: {meta}")
    return validate_frame(mode, meta)


def main():
    ROOT.mkdir(parents=True, exist_ok=True)

    adb("wait-for-device")
    capture_text("device-getprop.txt", "shell", "getprop")
    capture_text("surfaceflinger-before.txt", "shell", "dumpsys", "SurfaceFlinger")
    capture_text("vulkan-properties.txt", "shell", "getprop", "ro.hardware.vulkan")

    install = adb("install", "-r", str(APK), check=False)
    write_text("adb-install.txt", install.stdout)
    if install.returncode != 0 or "Success" not in install.stdout:
        raise RuntimeError("APK installation failed")

    stock = run_mode("stock")
    hook = run_mode("hook-fallback")

    capture_text("surfaceflinger-after.txt", "shell", "dumpsys", "SurfaceFlinger")

    oracles = {
        "l1_process": "pass",
        "l2_runtime_identity": "pass",
        "l3_android_window_surface": "pass",
        "l4_gpu_render_readback": "pass",
        "l5_present": "pass",
        "l6_loader_hook_interposition_fallback": "pass",
        "turnip_real_device_redirect": "not_provable_on_emulator",
    }
    (ROOT / "oracle-results.json").write_text(
        json.dumps(oracles, indent=2) + "\n", encoding="utf-8")

    summary = {
        "schema_version": "1.0",
        "terminal_state": "PASSED",
        "commit_sha": os.getenv("GITHUB_SHA"),
        "run_id": os.getenv("GITHUB_RUN_ID"),
        "stock": stock,
        "hook_fallback": hook,
        "oracles": oracles,
        "physical_device_gap":
            "Hosted x86_64 emulator has no Qualcomm KGSL/Turnip; "
            "a real libvulkan_freedreno.so redirect still requires device evidence.",
    }
    (ROOT / "run-summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        ROOT.mkdir(parents=True, exist_ok=True)
        failure = {
            "schema_version": "1.0",
            "terminal_state": "FAILED",
            "commit_sha": os.getenv("GITHUB_SHA"),
            "run_id": os.getenv("GITHUB_RUN_ID"),
            "error": str(exc),
            "traceback": traceback.format_exc(),
        }
        (ROOT / "run-summary.json").write_text(
            json.dumps(failure, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(failure, indent=2), file=sys.stderr)
        raise
