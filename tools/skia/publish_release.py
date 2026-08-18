#!/usr/bin/env python3
"""Publish an immutable prerelease, or prove an existing one is byte-identical."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from sdk import file_sha256


def gh(*arguments: str, capture: bool = False) -> str:
    command = ["gh", *arguments]
    if capture:
        return subprocess.check_output(command, text=True).strip()
    subprocess.run(command, check=True)
    return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--target-commit", required=True)
    args = parser.parse_args()
    directory = args.directory.resolve()
    release = json.loads((directory / "release.json").read_text(encoding="utf-8"))
    index = json.loads((directory / "skia-sdk-index.json").read_text(encoding="utf-8"))
    tag = release["tag"]
    assets = sorted([
        *directory.glob("skia-sdk-*.zip"),
        directory / "skia-sdk-index.json",
        directory / "SHA256SUMS",
    ], key=lambda path: path.name)
    if len(list(directory.glob("skia-sdk-*.zip"))) != len(index["targets"]):
        raise RuntimeError("release archive count must match the profile target set")

    exists = subprocess.run(
        ["gh", "release", "view", tag, "--repo", args.repository],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0
    if exists:
        metadata = json.loads(gh(
            "release", "view", tag, "--repo", args.repository,
            "--json", "isPrerelease,targetCommitish,assets", capture=True,
        ))
        if not metadata["isPrerelease"]:
            raise RuntimeError(f"existing {tag} is not a prerelease")
        if metadata["targetCommitish"] != args.target_commit:
            raise RuntimeError(
                f"existing {tag} targets {metadata['targetCommitish']}, "
                f"not {args.target_commit}"
            )
        expected_names = [path.name for path in assets]
        existing_names = sorted(asset["name"] for asset in metadata["assets"])
        if existing_names != expected_names:
            raise RuntimeError("existing release asset set differs; assets are immutable")
        with tempfile.TemporaryDirectory(prefix="canvas-skia-release-") as temporary:
            gh("release", "download", tag, "--repo", args.repository, "--dir", temporary)
            for path in assets:
                remote = Path(temporary) / path.name
                if file_sha256(remote) != file_sha256(path):
                    raise RuntimeError(
                        f"existing release asset differs byte-for-byte: {path.name}"
                    )
        print(f"existing immutable release is byte-identical: {tag}")
        return 0

    notes = (
        f"Immutable prebuilt Skia SDK set `{release['set_id']}` for the "
        f"{index['profile']} profile. This is a dependency prerelease, not "
        "a Canvas product release."
    )
    command = [
        "release", "create", tag, "--repo", args.repository, "--prerelease",
        "--target", args.target_commit, "--title", tag, "--notes", notes,
        *[str(path) for path in assets],
    ]
    gh(*command)
    print(f"published immutable prerelease: {tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
