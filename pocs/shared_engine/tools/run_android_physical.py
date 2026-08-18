#!/usr/bin/env python3
"""Run the POC-01 non-debuggable Android physical-device gate."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

from memory_series import analyze


PACKAGE = "dev.mostorm.canvas.poc01"
ACTIVITY = f"{PACKAGE}/dev.mostorm.canvas.CanvasPocActivity"
EXPECTED_DIGEST = "47826449b895ac4f4a57b4f386379775"
REMOTE_RGBA = f"/sdcard/Android/data/{PACKAGE}/files/android-actual.rgba"
REPO_ROOT = Path(__file__).resolve().parents[3]
SKIA_COMMIT = "b6d106297ff9ef2ff8094033695d045e87775581"


def adb(serial: str, *args: str, capture: bool = True, check: bool = True) -> str:
    completed = subprocess.run(
        ["adb", "-s", serial, *args],
        check=check,
        text=True,
        capture_output=capture,
    )
    return completed.stdout.strip() if capture else ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def properties(serial: str, names: list[str]) -> dict[str, str]:
    return {name: adb(serial, "shell", "getprop", name) for name in names}


def dumpsys_value(serial: str, service: str, pattern: str) -> str | None:
    value = adb(serial, "shell", "dumpsys", service, check=False)
    match = re.search(pattern, value, flags=re.MULTILINE | re.IGNORECASE)
    return match.group(1).strip() if match else None


def pss_bytes(serial: str) -> int:
    output = adb(serial, "shell", "dumpsys", "meminfo", PACKAGE)
    match = re.search(r"^\s*TOTAL\s+(\d+)\b", output, flags=re.MULTILINE)
    if not match:
        match = re.search(r"TOTAL PSS:\s*(\d+)", output)
    if not match:
        raise RuntimeError("could not parse TOTAL PSS from dumpsys meminfo")
    return int(match.group(1)) * 1024


def application_debuggable(serial: str) -> bool:
    package = adb(serial, "shell", "dumpsys", "package", PACKAGE)
    match = re.search(r"pkgFlags=\[([^]]*)\]", package)
    if not match:
        raise RuntimeError("could not read installed package flags")
    return "DEBUGGABLE" in match.group(1).split()


def display_environment(serial: str) -> dict:
    refresh = dumpsys_value(serial, "display", r"mRefreshRate\s*=\s*([0-9.]+)")
    if refresh is None:
        refresh = dumpsys_value(serial, "display", r"refreshRate\s+([0-9.]+)")
    mode = dumpsys_value(serial, "display", r"mActiveModeId\s*=\s*([^\s]+)")
    peak = adb(serial, "shell", "settings", "get", "system", "peak_refresh_rate", check=False)
    minimum = adb(serial, "shell", "settings", "get", "system", "min_refresh_rate", check=False)
    return {
        "current_refresh": {
            "status": "observed" if refresh else "unavailable",
            "hz": float(refresh) if refresh else None,
        },
        "active_mode_id": mode,
        "peak_refresh_setting_hz": None if peak in {"", "null"} else peak,
        "minimum_refresh_setting_hz": None if minimum in {"", "null"} else minimum,
        "observation_method": "adb dumpsys display and settings system",
        "vrr": {
            "status": "unavailable",
            "observation_method": "Android public shell output did not expose a reliable active-VRR state",
        },
    }


def environment(serial: str) -> dict:
    props = properties(
        serial,
        [
            "ro.product.manufacturer",
            "ro.product.model",
            "ro.product.device",
            "ro.build.version.release",
            "ro.build.version.sdk",
            "ro.product.cpu.abi",
            "ro.hardware.egl",
        ],
    )
    battery = adb(serial, "shell", "dumpsys", "battery", check=False)
    thermal = adb(serial, "shell", "dumpsys", "thermalservice", check=False)
    power = adb(serial, "shell", "dumpsys", "power", check=False)
    status_match = re.search(r"status:\s*(\d+)", battery)
    plugged_match = re.search(r"powered:\s*(true|false)", battery, re.IGNORECASE)
    thermal_match = re.search(r"Thermal Status:\s*(\d+)", thermal)
    if thermal_match is None:
        thermal_match = re.search(r"mStatus\s*=\s*(\d+)", thermal)
    low_power = re.search(r"mLowPowerModeEnabled\s*=\s*(true|false)", power)
    return {
        "device": props,
        "thermal": {
            "status": int(thermal_match.group(1)) if thermal_match else "unavailable",
            "observation_method": "adb dumpsys thermalservice",
        },
        "power": {
            "battery_status_code": int(status_match.group(1)) if status_match else None,
            "plugged": plugged_match.group(1).lower() == "true" if plugged_match else None,
            "low_power_mode": low_power.group(1).lower() == "true" if low_power else "unavailable",
            "observation_method": "adb dumpsys battery and power",
        },
        "display": display_environment(serial),
        "browser_throttling": {
            "status": "not_applicable",
            "observation_method": "native GLES runner",
        },
        "target_frame_interval": {
            "status": "unbounded-submit-loop",
            "value_ms": None,
            "observation_method": "native POC-01 GLES smoke loop",
        },
        "privacy": {
            "redacted": True,
            "excluded": [
                "ADB serial",
                "device serial number",
                "build fingerprint",
                "network and account identifiers",
            ],
        },
    }


def choose_serial(requested: str | None) -> str:
    rows = []
    for line in subprocess.run(
        ["adb", "devices", "-l"], check=True, text=True, capture_output=True
    ).stdout.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            rows.append(fields[0])
    if requested:
        if requested not in rows:
            raise RuntimeError(f"requested Android device {requested!r} is not online")
        return requested
    if len(rows) != 1:
        raise RuntimeError(f"expected exactly one online Android device, found {len(rows)}")
    return rows[0]


def restore_display_size(serial: str, original: str) -> None:
    override = re.search(r"Override size:\s*(\d+x\d+)", original)
    if override:
        adb(serial, "shell", "wm", "size", override.group(1), check=False)
    else:
        adb(serial, "shell", "wm", "size", "reset", check=False)


def wait_for_marker(serial: str, marker: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if marker in adb(serial, "logcat", "-d", "-s", "CanvasPOC01:I", "*:S"):
            return
        time.sleep(0.5)
    raise RuntimeError(f"timed out waiting for {marker}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--serial")
    parser.add_argument("--sample-seconds", type=float, default=5.0)
    args = parser.parse_args()
    if args.sample_seconds <= 0:
        raise RuntimeError("--sample-seconds must be positive")
    serial = choose_serial(args.serial)
    if adb(serial, "shell", "getprop", "ro.kernel.qemu") == "1":
        raise RuntimeError("physical gate refuses an Android emulator")
    args.output.mkdir(parents=True, exist_ok=True)
    apk = args.apk.resolve(strict=True)

    original_size = adb(serial, "shell", "wm", "size")
    original_stay_awake = adb(serial, "shell", "settings", "get", "global", "stay_on_while_plugged_in")
    try:
        adb(serial, "install", "-r", str(apk), capture=False)
        if application_debuggable(serial):
            raise RuntimeError("installed APK is debuggable; Release physical gate refused")
        adb(serial, "shell", "wm", "size", "800x600")
        adb(serial, "shell", "settings", "put", "global", "stay_on_while_plugged_in", "7")
        adb(serial, "shell", "rm", "-f", REMOTE_RGBA, check=False)
        adb(serial, "logcat", "-c")
        environment_before = environment(serial)
        adb(serial, "shell", "am", "force-stop", PACKAGE)
        adb(serial, "shell", "am", "start", "-W", "-n", ACTIVITY)
        wait_for_marker(serial, "CANVAS_POC01_SMOKE_BEGIN", 120)
        started = time.monotonic()
        samples: list[dict[str, int]] = [{"elapsed_ms": 0, "bytes": pss_bytes(serial)}]
        next_sample = started + args.sample_seconds
        while True:
            log = adb(serial, "logcat", "-d", "-s", "CanvasPOC01:I", "*:S")
            if "CANVAS_POC01_SMOKE_END" in log:
                break
            now = time.monotonic()
            if now >= next_sample:
                samples.append({"elapsed_ms": round((now - started) * 1000), "bytes": pss_bytes(serial)})
                next_sample += args.sample_seconds
            if now - started > 90:
                raise RuntimeError("Android smoke exceeded the evidence timeout")
            time.sleep(0.25)
        samples.append({"elapsed_ms": round((time.monotonic() - started) * 1000), "bytes": pss_bytes(serial)})
        wait_for_marker(serial, "CANVAS_POC01_RESULT", 30)
        log = adb(serial, "logcat", "-d", "-s", "CanvasPOC01:I", "*:S")
        result_lines = re.findall(r"CANVAS_POC01_RESULT\s+(\{.*\})", log)
        if not result_lines:
            raise RuntimeError("Android runner did not emit a structured result")
        result = json.loads(result_lines[-1])
        if result.get("digest") != EXPECTED_DIGEST:
            raise RuntimeError("Android digest differs from the reviewed fixture")
        if result.get("lifecycle") != 100 or result.get("smoke_seconds") != 60:
            raise RuntimeError("Android device did not complete the 100/60 gate")
        if result.get("smoke_frames", 0) <= 0:
            raise RuntimeError("Android device reported no measured smoke frames")
        if result.get("max_frame_ms", 101) > 100:
            raise RuntimeError("Android device frame exceeded 100 ms")
        result["build"] = {
            "configuration": "Release",
            "debuggable": False,
            "apk_sha256": sha256(apk),
        }
        result["memory_scope"] = "process-total-pss"
        result["memory_sampling_interval_ms"] = round(args.sample_seconds * 1000)
        result["memory_samples"] = samples
        result["memory_analysis"] = analyze(samples)
        if not result["memory_analysis"]["passed"]:
            raise RuntimeError("Android total-PSS series shows sustained growth")
        result["performance_environment"] = {
            "before": environment_before,
            "after": environment(serial),
        }
        result_path = args.output / "android-release-result.json"
        result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        rgba = args.output / "android-release-actual.rgba"
        adb(serial, "pull", REMOTE_RGBA, str(rgba), capture=False)
        subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "pocs/shared_engine/tools/visual_compare.py"),
                "--expected",
                str(REPO_ROOT / "pocs/shared_engine/goldens/reference.rgba"),
                "--actual",
                str(rgba),
                "--artifacts",
                str(args.output / "android-release-visual"),
                "--backend",
                "ganesh-gles3-physical",
                "--skia-commit",
                SKIA_COMMIT,
            ],
            check=True,
        )
        (args.output / "android-release-logcat.txt").write_text(log + "\n", encoding="utf-8")
        print(result_path)
        return 0
    finally:
        restore_display_size(serial, original_size)
        if original_stay_awake not in {"", "null"}:
            adb(serial, "shell", "settings", "put", "global", "stay_on_while_plugged_in", original_stay_awake, check=False)
        else:
            adb(serial, "shell", "settings", "delete", "global", "stay_on_while_plugged_in", check=False)
        (args.output / "host-observations.json").write_text(
            json.dumps({"original_wm_size": original_size, "serial_redacted": True}, indent=2) + "\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    raise SystemExit(main())
