#!/usr/bin/env python3
"""Run one Minecraft differential lane with a bounded, evidence-preserving watchdog."""
import argparse
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=float, default=360.0)
    ap.add_argument("--log", required=True, type=Path)
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.command
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        raise SystemExit("missing command after --")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w", encoding="utf-8", buffering=1) as log:
        def emit(line):
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line)
            log.flush()

        emit(f"LANE_RUNNER command={cmd!r} timeout={args.timeout:.0f}s\n")
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            start_new_session=True,
        )

        def pump():
            assert proc.stdout is not None
            for line in proc.stdout:
                emit(line)

        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        deadline = time.monotonic() + args.timeout
        while proc.poll() is None and time.monotonic() < deadline:
            time.sleep(0.25)
        if proc.poll() is None:
            emit(f"LANE_TIMEOUT pid={proc.pid} elapsed={args.timeout:.0f}s; requesting JVM/process-group dumps\n")
            try:
                os.killpg(proc.pid, signal.SIGQUIT)
            except ProcessLookupError:
                pass
            time.sleep(3.0)
            emit("LANE_TIMEOUT terminating process group\n")
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=7.0)
            except subprocess.TimeoutExpired:
                emit("LANE_TIMEOUT escalating to SIGKILL\n")
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                proc.wait()
            reader.join(timeout=2.0)
            emit("LANE_RESULT timeout\n")
            return 124
        rc = proc.wait()
        reader.join(timeout=2.0)
        emit(f"LANE_RESULT exit={rc}\n")
        return rc


if __name__ == "__main__":
    raise SystemExit(main())
