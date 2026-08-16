#!/usr/bin/env python3
"""Run the universal POC bundle once as iOS and once as iPadOS."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time


BUNDLE_ID = "dev.mostorm.canvas.poc01"


def run(*args: str, capture: bool = False) -> str:
    completed = subprocess.run(
        args, check=True, text=True, capture_output=capture
    )
    return completed.stdout.strip() if capture else ""


def available_devices() -> list[dict]:
    listing = json.loads(run("xcrun", "simctl", "list", "devices", "available", "-j", capture=True))
    devices: list[dict] = []
    for runtime_devices in listing["devices"].values():
        devices.extend(device for device in runtime_devices if device.get("isAvailable"))
    return devices


def pick(devices: list[dict], token: str) -> dict:
    for device in devices:
        if token in device["name"]:
            return device
    raise RuntimeError(f"no available {token} simulator")


def execute(device: dict, app: Path, output: Path, platform: str) -> None:
    udid = device["udid"]
    subprocess.run(["xcrun", "simctl", "boot", udid], check=False)
    try:
        run("xcrun", "simctl", "bootstatus", udid, "-b")
        # A clean install prevents a previous Documents directory from satisfying
        # the artifact poll before the current acceptance run has completed.
        subprocess.run(
            ["xcrun", "simctl", "uninstall", udid, BUNDLE_ID], check=False
        )
        run("xcrun", "simctl", "install", udid, str(app))
        run("xcrun", "simctl", "launch", udid, BUNDLE_ID)
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            try:
                container = run(
                    "xcrun",
                    "simctl",
                    "get_app_container",
                    udid,
                    BUNDLE_ID,
                    "data",
                    capture=True,
                )
                result = Path(container) / "Documents" / "poc01-result.json"
                rgba = Path(container) / "Documents" / "apple-actual.rgba"
                failure = Path(container) / "Documents" / "poc01-failure.txt"
                if failure.exists():
                    raise RuntimeError(
                        f"{platform} runner failed: "
                        f"{failure.read_text(encoding='utf-8')}"
                    )
                if result.exists() and rgba.exists():
                    output.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(result, output / f"{platform}-result.json")
                    shutil.copy2(rgba, output / f"{platform}-actual.rgba")
                    break
            except subprocess.CalledProcessError:
                pass
            time.sleep(1)
        else:
            raise RuntimeError(f"{platform} runner did not produce artifacts")
    finally:
        subprocess.run(
            ["xcrun", "simctl", "terminate", udid, BUNDLE_ID], check=False
        )
        subprocess.run(["xcrun", "simctl", "shutdown", udid], check=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    devices = available_devices()
    execute(pick(devices, "iPhone"), args.app, args.output, "ios")
    execute(pick(devices, "iPad"), args.app, args.output, "ipados")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
