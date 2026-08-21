#!/usr/bin/env python3
"""Aggregate one profile's verified SDK archives into an immutable release set."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

from sdk import (
    DEFAULT_PROFILE, ROOT, SDK_VARIANTS, canonical_sha256, file_sha256,
    load_profile, profile_hash, validate_symbols_manifest,
)
from consumer import validate_index
from verify import verify_archive


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    args = parser.parse_args()
    profile_path = args.profile.resolve()
    profile = load_profile(profile_path)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise RuntimeError(f"aggregate output must be empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    targets = {}
    full = profile.get("schema_version") == 2
    for target_name in sorted(profile["targets"]):
        if full:
            variants = {}
            for variant in SDK_VARIANTS:
                asset_name = f"skia-sdk-{profile['profile']}-{target_name}-{variant}.zip"
                matches = list(args.assets.resolve().rglob(asset_name))
                if len(matches) != 1:
                    raise RuntimeError(f"expected exactly one {asset_name}, found {len(matches)}")
                destination = output / asset_name
                shutil.copyfile(matches[0], destination)
                summary = verify_archive(
                    destination, profile_path, target_name, expected_variant=variant,
                )
                with zipfile.ZipFile(destination) as archive:
                    manifest = json.loads(archive.read("manifest.json"))
                symbols_asset = None
                symbols_sha256 = None
                symbols_size = None
                if variant != "release":
                    symbols_asset = f"skia-sdk-{profile['profile']}-{target_name}-{variant}-symbols.zip"
                    symbol_matches = list(args.assets.resolve().rglob(symbols_asset))
                    if len(symbol_matches) != 1:
                        raise RuntimeError(f"expected exactly one {symbols_asset}")
                    symbol_destination = output / symbols_asset
                    shutil.copyfile(symbol_matches[0], symbol_destination)
                    with zipfile.ZipFile(symbol_destination) as symbol_archive:
                        names = symbol_archive.namelist()
                        if names.count("symbols.json") != 1:
                            raise RuntimeError(
                                f"symbols archive metadata is missing: {symbols_asset}"
                            )
                        symbols_metadata = json.loads(
                            symbol_archive.read("symbols.json")
                        )
                        symbol_payloads = {
                            name: symbol_archive.read(name)
                            for name in names if name != "symbols.json"
                        }
                    expected_symbols_metadata = {
                        "schema_version": 1,
                        "sdk_id": summary["sdk_id"],
                        "target": target_name,
                        "variant": variant,
                    }
                    for key, value in expected_symbols_metadata.items():
                        if symbols_metadata.get(key) != value:
                            raise RuntimeError(
                                f"symbols archive identity mismatch: {symbols_asset}"
                            )
                    validate_symbols_manifest(
                        symbols_metadata, expected_sdk_id=summary["sdk_id"],
                        expected_target=target_name, expected_variant=variant,
                    )
                    actual_symbol_files = {
                        name for name in names if name != "symbols.json"
                    }
                    listed_symbol_files = {
                        entry["path"] for entry in symbols_metadata["files"]
                    }
                    if actual_symbol_files != listed_symbol_files:
                        raise RuntimeError(
                            f"symbols archive payload mismatch: {symbols_asset}"
                        )
                    for entry in symbols_metadata["files"]:
                        payload = symbol_payloads[entry["path"]]
                        if len(payload) != entry["size"] or \
                           hashlib.sha256(payload).hexdigest() != entry["sha256"]:
                            raise RuntimeError(
                                f"symbols archive checksum mismatch: {entry['path']}"
                            )
                    symbols_sha256 = file_sha256(symbol_destination)
                    symbols_size = symbol_destination.stat().st_size
                variants[variant] = {
                    "asset": asset_name, "sdk_id": summary["sdk_id"],
                    "sha256": summary["archive_sha256"], "size": destination.stat().st_size,
                    "toolchain": manifest["identity"]["toolchain"],
                    "capabilities": manifest["capabilities"],
                    "variant_metadata": manifest["identity"]["variant_metadata"],
                    "symbols_asset": symbols_asset, "symbols_sha256": symbols_sha256,
                    "symbols_size": symbols_size,
                }
            targets[target_name] = {"variants": variants}
            continue
        asset_name = f"skia-sdk-{target_name}.zip"
        matches = list(args.assets.resolve().rglob(asset_name))
        if len(matches) != 1:
            raise RuntimeError(f"expected exactly one {asset_name}, found {len(matches)}")
        destination = output / asset_name
        shutil.copyfile(matches[0], destination)
        summary = verify_archive(destination, profile_path, target_name)
        with zipfile.ZipFile(destination) as archive:
            manifest = json.loads(archive.read("manifest.json"))
        targets[target_name] = {
            "asset": asset_name,
            "sdk_id": summary["sdk_id"],
            "sha256": summary["archive_sha256"],
            "size": destination.stat().st_size,
            "toolchain": manifest["identity"]["toolchain"],
        }
    identity_set = (
        {name: {variant: data["sdk_id"] for variant, data in value["variants"].items()}
         for name, value in targets.items()}
        if full else {name: value["sdk_id"] for name, value in targets.items()}
    )
    set_id = canonical_sha256(identity_set)
    source_commit = os.environ.get("GITHUB_SHA")
    if not source_commit:
        source_commit = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
        ).strip()
    index = {
        "schema_version": 2 if full else 1,
        "format": "canvas-skia-sdk-set-v2" if full else "canvas-skia-sdk-set-v1",
        "set_id": set_id,
        "profile": profile["profile"],
        "profile_hash": profile_hash(profile),
        "skia_commit": profile["skia_commit"],
        "source_repository": os.environ.get("GITHUB_REPOSITORY", "Mostorm-Labs/Axiom"),
        "source_commit": source_commit,
        "targets": targets,
    }
    validate_index(index)
    index_path = output / "skia-sdk-index.json"
    index_path.write_text(
        json.dumps(index, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )
    sums = []
    for path in sorted(output.glob("*.zip")) + [index_path]:
        sums.append(f"{file_sha256(path)}  {path.name}")
    (output / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")
    summary = {"set_id": set_id, "tag": f"skia-sdk-{profile['profile']}-{set_id[:16]}"}
    (output / "release.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
