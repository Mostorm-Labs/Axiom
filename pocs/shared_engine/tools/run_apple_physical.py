#!/usr/bin/env python3
"""Collect privacy-filtered POC-01 Apple physical-device evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import time

from memory_series import analyze


BUNDLE_ID = "dev.mostorm.canvas.poc01"
EXPECTED_DIGEST = "47826449b895ac4f4a57b4f386379775"
REPO_ROOT = Path(__file__).resolve().parents[3]
SKIA_COMMIT = "b6d106297ff9ef2ff8094033695d045e87775581"


def run(*args: str, capture: bool = False, check: bool = True) -> str:
    completed = subprocess.run(
        args, check=check, text=True, capture_output=capture
    )
    return completed.stdout.strip() if capture else ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def devicectl_json(device: str, command: list[str]) -> dict:
    with tempfile.TemporaryDirectory(prefix="canvas-poc01-devicectl-") as directory:
        output = Path(directory) / "result.json"
        run(
            "xcrun",
            "devicectl",
            *command,
            "--device",
            device,
            "--json-output",
            str(output),
            "--quiet",
        )
        return json.loads(output.read_text(encoding="utf-8"))["result"]


def copy_from(device: str, source: str, destination: Path, check: bool = True) -> bool:
    completed = subprocess.run(
        [
            "xcrun",
            "devicectl",
            "device",
            "copy",
            "from",
            "--device",
            device,
            "--domain-type",
            "appDataContainer",
            "--domain-identifier",
            BUNDLE_ID,
            "--source",
            source,
            "--destination",
            str(destination),
            "--quiet",
        ],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if completed.returncode != 0 and check:
        raise RuntimeError(f"failed to copy {source} from Apple device")
    return completed.returncode == 0


def sanitized_environment(device: str) -> dict:
    details = devicectl_json(device, ["device", "info", "details"])
    displays = devicectl_json(device, ["device", "info", "displays"])
    hardware = details["hardwareProperties"]
    properties = details["deviceProperties"]
    connection = details.get("connectionProperties", {})
    return {
        "reality": hardware.get("reality"),
        "platform": hardware.get("platform"),
        "marketing_name": hardware.get("marketingName"),
        "product_type": hardware.get("productType"),
        "cpu": hardware.get("cpuType", {}).get("name"),
        "os_version": properties.get("osVersionNumber"),
        "os_build": properties.get("osBuildUpdate"),
        "developer_mode": properties.get("developerModeStatus"),
        "transport": connection.get("transportType"),
        "display": {
            "backlight_state": displays.get("backlightState"),
            "panels": [
                {
                    "name": panel.get("name"),
                    "native_size": panel.get("nativeSize"),
                    "point_scale": panel.get("pointScale"),
                    "primary": panel.get("primary"),
                }
                for panel in displays.get("displays", [])
            ],
            "refresh_observation": "maximumFramesPerSecond is recorded by the in-app runner",
        },
        "privacy": {
            "redacted": True,
            "excluded": [
                "device identifier",
                "UDID",
                "ECID",
                "serial number",
                "user-assigned name",
                "hostnames and IP addresses",
                "signing team and identity",
            ],
        },
    }


def assert_release_app(app: Path) -> None:
    info = plistlib.loads((app / "Info.plist").read_bytes())
    if info.get("CFBundleIdentifier") != BUNDLE_ID:
        raise RuntimeError("Apple app has an unexpected bundle identifier")
    executable = app / info.get("CFBundleExecutable", "")
    if not executable.is_file():
        raise RuntimeError("Apple app executable is missing")
    sdk_name = subprocess.run(
        ["xcrun", "vtool", "-show-build", str(executable)],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    if "platform IOS" not in sdk_name:
        raise RuntimeError("Apple physical gate requires an iPhoneOS device build")
    verification = subprocess.run(
        ["codesign", "--verify", "--deep", "--strict", str(app)],
        text=True,
        capture_output=True,
    )
    if verification.returncode != 0:
        raise RuntimeError("Apple physical app is not validly signed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--platform", choices=("ios", "ipados"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    app = args.app.resolve(strict=True)
    assert_release_app(app)
    args.output.mkdir(parents=True, exist_ok=True)
    environment = sanitized_environment(args.device)
    if environment["reality"] != "physical":
        raise RuntimeError("Apple physical gate refuses a simulator")
    if environment["platform"] not in {"iOS", "iPadOS"}:
        raise RuntimeError("selected device is not an Apple mobile device")

    subprocess.run(
        [
            "xcrun",
            "devicectl",
            "device",
            "uninstall",
            "app",
            "--device",
            args.device,
            BUNDLE_ID,
            "--quiet",
        ],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    run(
        "xcrun",
        "devicectl",
        "device",
        "install",
        "app",
        "--device",
        args.device,
        str(app),
        "--quiet",
    )
    run(
        "xcrun",
        "devicectl",
        "device",
        "process",
        "launch",
        "--device",
        args.device,
        "--terminate-existing",
        BUNDLE_ID,
        "--quiet",
    )

    with tempfile.TemporaryDirectory(prefix="canvas-poc01-apple-result-") as directory:
        temporary = Path(directory)
        result_file = temporary / "poc01-result.json"
        failure_file = temporary / "poc01-failure.txt"
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            if copy_from(args.device, "Documents/poc01-failure.txt", failure_file, check=False):
                retained_failure = args.output / f"{args.platform}-physical-failure.txt"
                retained_failure.write_text(
                    failure_file.read_text(encoding="utf-8"), encoding="utf-8"
                )
                failure_record = {
                    "schema_version": 1,
                    "status": "failed",
                    "platform": args.platform,
                    "error": retained_failure.read_text(encoding="utf-8").strip(),
                    "physical_environment": environment,
                    "build": {
                        "configuration": "Release",
                        "app_executable_sha256": sha256(
                            app / "canvas_poc01_ios_runner"
                        ),
                    },
                }
                (args.output / f"{args.platform}-physical-failure.json").write_text(
                    json.dumps(failure_record, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                raise RuntimeError(failure_file.read_text(encoding="utf-8"))
            if copy_from(args.device, "Documents/poc01-result.json", result_file, check=False):
                break
            time.sleep(1)
        else:
            raise RuntimeError("Apple physical runner did not produce a result")

        result = json.loads(result_file.read_text(encoding="utf-8"))
        (args.output / f"{args.platform}-physical-raw-result.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        if result.get("platform") != args.platform:
            raise RuntimeError(
                f"expected {args.platform}, device runner reported {result.get('platform')}"
            )
        if result.get("digest") != EXPECTED_DIGEST:
            raise RuntimeError("Apple device digest differs from the reviewed fixture")
        if result.get("lifecycle") != 100 or result.get("smoke_seconds") != 60:
            raise RuntimeError("Apple device did not complete the 100/60 gate")
        if result.get("max_frame_ms", 101) > 100:
            raise RuntimeError("Apple device frame exceeded 100 ms")
        result["memory_analysis"] = analyze(result["memory_samples"])
        result["physical_environment"] = environment
        result["build"] = {
            "configuration": "Release",
            "app_executable_sha256": sha256(app / "canvas_poc01_ios_runner"),
        }
        normalized = args.output / f"{args.platform}-physical-result.json"
        normalized.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        if not result["memory_analysis"]["passed"]:
            raise RuntimeError(
                f"{args.platform} physical-footprint series shows sustained growth"
            )
        rgba = args.output / f"{args.platform}-physical-actual.rgba"
        copy_from(
            args.device,
            "Documents/apple-actual.rgba",
            rgba,
        )
        copy_from(
            args.device,
            "Documents/poc01-smoke-phase.txt",
            args.output / f"{args.platform}-smoke-phase.txt",
        )
        subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "pocs/shared_engine/tools/visual_compare.py"),
                "--expected",
                str(REPO_ROOT / "pocs/shared_engine/goldens/reference.rgba"),
                "--actual",
                str(rgba),
                "--artifacts",
                str(args.output / f"{args.platform}-physical-visual"),
                "--backend",
                f"ganesh-metal-{args.platform}-physical",
                "--skia-commit",
                SKIA_COMMIT,
            ],
            check=True,
        )
        print(normalized)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
