#!/usr/bin/env python3
"""Aggregate seven verified SDK archives into an immutable release set."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import zipfile

from sdk import DEFAULT_PROFILE, canonical_sha256, file_sha256, load_profile, profile_hash
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
    output.mkdir(parents=True, exist_ok=True)
    targets = {}
    for target_name in sorted(profile["targets"]):
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
    set_id = canonical_sha256({name: value["sdk_id"] for name, value in targets.items()})
    index = {
        "schema_version": 1,
        "format": "canvas-skia-sdk-set-v1",
        "set_id": set_id,
        "profile": profile["profile"],
        "profile_hash": profile_hash(profile),
        "skia_commit": profile["skia_commit"],
        "source_repository": os.environ.get("GITHUB_REPOSITORY", "Mostorm-Labs/Axiom"),
        "source_commit": os.environ.get("GITHUB_SHA", "local"),
        "targets": targets,
    }
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
