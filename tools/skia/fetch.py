#!/usr/bin/env python3
"""Download, strictly verify, and atomically install one locked Skia SDK."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import time
import urllib.request

from consumer import LOCK_PATH, load_lock
from sdk import (
    DEFAULT_PROFILE, ROOT, canonical_sha256, file_sha256, load_profile,
    normalized_gn_args, profile_hash, validate_manifest, validate_toolchain,
)
from verify import safe_name, verify_archive


INSTALL_ROOT = ROOT / ".deps/skia-sdk"


def asset_url(lock: dict, asset: str, base_url: str | None = None) -> str:
    mirror = base_url or os.environ.get("CANVAS_SKIA_SDK_BASE_URL")
    if mirror:
        return f"{mirror.rstrip('/')}/{lock['tag']}/{asset}"
    return f"https://github.com/{lock['repository']}/releases/download/{lock['tag']}/{asset}"


def download(url: str, destination: Path) -> int:
    request = urllib.request.Request(url, headers={"User-Agent": "Canvas-Skia-SDK-Consumer/1"})
    with urllib.request.urlopen(request) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)
    return destination.stat().st_size


def read_installed_manifest(root: Path) -> dict | None:
    path = root / "manifest.json"
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def verify_locked_identity(
    manifest: dict, lock: dict, target_name: str, profile_path: Path,
    variant: str = "release",
) -> None:
    target_entry = lock["targets"][target_name]
    target = target_entry.get("variants", {}).get(variant) if "variants" in target_entry else target_entry
    if target is None:
        raise RuntimeError(f"variant is not present in SDK lock: {variant}")
    identity = manifest["identity"]
    profile = load_profile(profile_path)
    current = {
        "profile": profile["profile"],
        "profile_hash": profile_hash(profile),
        "skia_commit": profile["skia_commit"],
    }
    for key, value in current.items():
        if lock[key] != value:
            raise RuntimeError(f"SDK lock does not match current {key}")
    validate_toolchain(profile, target_name, target["toolchain"])
    expected = {
        "profile": lock["profile"],
        "profile_hash": lock["profile_hash"],
        "skia_commit": lock["skia_commit"],
        "target": target_name,
        **({"variant": variant} if manifest.get("schema_version") == 2 else {}),
        "toolchain": target["toolchain"],
    }
    for key, value in expected.items():
        if identity.get(key) != value:
            raise RuntimeError(f"locked identity mismatch for {key}")
    if manifest["sdk_id"] != target["sdk_id"]:
        raise RuntimeError("manifest SDK ID does not match lock")
    if canonical_sha256(identity) != target["sdk_id"]:
        raise RuntimeError("manifest SDK ID does not match canonical identity")
    if identity.get("gn_args") != normalized_gn_args(profile, target_name, variant):
        raise RuntimeError("manifest GN identity does not match the locked profile")
    if manifest.get("schema_version") == 2:
        if manifest.get("capabilities") != target.get("capabilities") or \
           identity.get("capabilities") != target.get("capabilities"):
            raise RuntimeError("manifest capabilities do not match the SDK lock")
        if identity.get("variant_metadata") != target.get("variant_metadata"):
            raise RuntimeError("manifest variant metadata does not match the SDK lock")


def verify_installed(
    root: Path, lock: dict, target_name: str, profile_path: Path = DEFAULT_PROFILE,
    variant: str = "release",
) -> dict:
    manifest = read_installed_manifest(root)
    if manifest is None:
        raise RuntimeError("installed SDK has no valid manifest")
    validate_manifest(manifest, expected_target=target_name)
    verify_locked_identity(manifest, lock, target_name, profile_path, variant)
    listed = manifest["files"]
    expected_paths = {"manifest.json", *(entry["path"] for entry in listed)}
    actual_paths = {
        path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()
    }
    if actual_paths != expected_paths:
        raise RuntimeError("installed SDK file set does not match manifest")
    for entry in listed:
        path = root.joinpath(*safe_name(entry["path"]).parts)
        if path.stat().st_size != entry["size"] or file_sha256(path) != entry["sha256"]:
            raise RuntimeError(f"installed SDK file mismatch: {entry['path']}")
    return manifest


def install(
    target_name: str, lock_path: Path = LOCK_PATH, *,
    install_root: Path = INSTALL_ROOT, base_url: str | None = None,
    profile_path: Path = DEFAULT_PROFILE, variant: str = "release",
) -> dict:
    started = time.monotonic()
    lock = load_lock(lock_path)
    if target_name not in lock["targets"]:
        raise RuntimeError(f"target is not present in SDK lock: {target_name}")
    target_entry = lock["targets"][target_name]
    target = target_entry.get("variants", {}).get(variant) if "variants" in target_entry else target_entry
    if target is None:
        raise RuntimeError(f"variant is not present in SDK lock: {variant}")
    if "variants" not in target_entry and variant != "release":
        raise RuntimeError("legacy SDK locks only support the release variant")
    install_name = f"{target_name}/{variant}" if "variants" in target_entry else target_name
    destination = install_root / install_name
    previous = destination.parent / f".{destination.name}.previous"
    if not destination.exists() and previous.exists():
        previous.replace(destination)
    try:
        manifest = verify_installed(destination, lock, target_name, profile_path, variant)
        return {
            "target": target_name, "sdk_id": manifest["sdk_id"], "bytes": 0,
            "seconds": round(time.monotonic() - started, 3), "source": "installed",
            "path": str(destination),
        }
    except (RuntimeError, OSError, KeyError, TypeError):
        pass

    install_root.mkdir(parents=True, exist_ok=True)
    # The destination is nested below the install root (target/variant).  Make
    # sure its parent exists before the final atomic directory replacement.
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_root = Path(tempfile.mkdtemp(prefix=f".{target_name}-", dir=install_root))
    archive = temporary_root / target["asset"]
    extract = temporary_root / "verified"
    try:
        url = asset_url(lock, target["asset"], base_url)
        size = download(url, archive)
        if size != target["size"]:
            raise RuntimeError(f"download size mismatch: expected {target['size']}, got {size}")
        if file_sha256(archive) != target["sha256"]:
            raise RuntimeError("download SHA-256 does not match SDK lock")
        summary = verify_archive(
            archive, profile_path, target_name, extract,
            enforce_current_recipe=False, expected_variant=variant,
        )
        if summary["sdk_id"] != target["sdk_id"]:
            raise RuntimeError("verified archive SDK ID does not match lock")
        verify_installed(extract, lock, target_name, profile_path, variant)
        if previous.exists():
            shutil.rmtree(previous)
        if destination.exists():
            destination.replace(previous)
        try:
            extract.replace(destination)
        except BaseException:
            if previous.exists() and not destination.exists():
                previous.replace(destination)
            raise
        shutil.rmtree(previous, ignore_errors=True)
    except BaseException:
        shutil.rmtree(temporary_root, ignore_errors=True)
        raise
    shutil.rmtree(temporary_root, ignore_errors=True)
    return {
        "target": target_name, "sdk_id": target["sdk_id"], "bytes": target["size"],
        "seconds": round(time.monotonic() - started, 3), "source": url,
        "path": str(destination),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--variant", choices=("release", "debug", "asan"), default="release")
    parser.add_argument("--lock", type=Path, default=LOCK_PATH)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--install-root", type=Path, default=INSTALL_ROOT)
    parser.add_argument("--summary-file", type=Path)
    args = parser.parse_args()
    result = install(
        args.target, args.lock.resolve(), install_root=args.install_root.resolve(),
        profile_path=args.profile.resolve(), variant=args.variant,
    )
    if args.summary_file:
        with args.summary_file.open("a", encoding="utf-8") as summary:
            summary.write(
                f"### Skia SDK `{result['target']}`\n\n"
                f"- SDK ID: `{result['sdk_id']}`\n"
                f"- Download: {result['bytes']} bytes in {result['seconds']} s\n\n"
            )
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
