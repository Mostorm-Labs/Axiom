#!/usr/bin/env python3
"""Install the POC-03 APK, run its fixed trace, and archive device evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


PACKAGE = "dev.mostorm.canvas.poc03"
ACTIVITY = f"{PACKAGE}/.CanvasPoc03Activity"


def output(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--nodes", type=int, choices=(1000, 10000, 50000, 100000),
                        default=100000)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    if not args.apk.is_file():
        raise SystemExit(f"APK does not exist: {args.apk}")
    args.output.mkdir(parents=True, exist_ok=True)
    run("adb", "install", "-r", str(args.apk))
    run("adb", "logcat", "-c")
    run("adb", "shell", "am", "start", "-n", ACTIVITY, "--ei",
        "poc03_nodes", str(args.nodes))
    deadline = time.monotonic() + args.timeout
    log = ""
    while time.monotonic() < deadline:
        log = output("adb", "logcat", "-d", "-s", "CanvasPOC03:I")
        if "CANVAS_POC03_RESULT" in log:
            break
        time.sleep(1.0)
    else:
        raise TimeoutError("POC-03 Android result did not arrive")
    result_text = output(
        "adb", "shell", "run-as", PACKAGE, "cat",
        "files/poc03-android-result.json",
    )
    result = json.loads(result_text)
    (args.output / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (args.output / "logcat.txt").write_text(log + "\n", encoding="utf-8")
    properties = {
        key: output("adb", "shell", "getprop", key)
        for key in (
            "ro.product.manufacturer",
            "ro.product.model",
            "ro.product.device",
            "ro.build.version.release",
            "ro.build.fingerprint",
            "ro.hardware",
            "ro.soc.manufacturer",
            "ro.soc.model",
        )
    }
    metadata = {
        "schema_version": 1,
        "apk_sha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
        "git_commit": output("git", "rev-parse", "HEAD"),
        "adb_serial": output("adb", "get-serialno"),
        "properties": properties,
        "display": output("adb", "shell", "dumpsys", "display"),
    }
    (args.output / "device.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if not result.get("full_incremental_equivalent") or not result.get(
        "visual_equivalent"
    ):
        raise RuntimeError("Android full/incremental correctness gate failed")
    if result.get("maximum_candidates", 5001) > 5000:
        raise RuntimeError("Android candidate gate failed")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
        ValueError,
        TimeoutError,
    ) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
