#!/usr/bin/env python3
"""Generate the committed consumer lock from an exact immutable release tag."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from consumer import LOCK_PATH, lock_from_index
from sdk import file_sha256


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", required=True)
    parser.add_argument("--repository", default="Mostorm-Labs/Axiom")
    parser.add_argument("--output", type=Path, default=LOCK_PATH)
    args = parser.parse_args()

    metadata = json.loads(subprocess.check_output([
        "gh", "release", "view", args.tag, "--repo", args.repository,
        "--json", "isPrerelease,targetCommitish,assets,tagName",
    ], text=True))
    if not metadata["isPrerelease"] or metadata["tagName"] != args.tag:
        raise RuntimeError("SDK release must be the exact prerelease tag")
    assets = {asset["name"]: asset for asset in metadata["assets"]}
    with tempfile.TemporaryDirectory(prefix="canvas-skia-lock-") as temporary:
        subprocess.run([
            "gh", "release", "download", args.tag, "--repo", args.repository,
            "--pattern", "skia-sdk-index.json", "--pattern", "SHA256SUMS",
            "--dir", temporary,
        ], check=True)
        root = Path(temporary)
        index_path = root / "skia-sdk-index.json"
        sums_path = root / "SHA256SUMS"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        sums = {}
        for line in sums_path.read_text(encoding="utf-8").splitlines():
            digest, name = line.split("  ", 1)
            if name in sums or len(digest) != 64 or any(
                character not in "0123456789abcdef" for character in digest
            ):
                raise RuntimeError("SHA256SUMS contains invalid or duplicate entries")
            sums[name] = digest
        if sums.get(index_path.name) != file_sha256(index_path):
            raise RuntimeError("release index does not match SHA256SUMS")
        lock = lock_from_index(args.repository, args.tag, index)
        if lock["source_commit"] != metadata["targetCommitish"]:
            raise RuntimeError("release target commit does not match SDK index")
        expected_assets = {"SHA256SUMS", "skia-sdk-index.json"}
        for target in lock["targets"].values():
            expected_assets.add(target["asset"])
            if sums.get(target["asset"]) != target["sha256"]:
                raise RuntimeError(f"SHA256SUMS mismatch: {target['asset']}")
            digest = assets.get(target["asset"], {}).get("digest")
            if digest and digest != f"sha256:{target['sha256']}":
                raise RuntimeError(f"GitHub asset digest mismatch: {target['asset']}")
        if set(sums) != expected_assets - {"SHA256SUMS"}:
            raise RuntimeError("SHA256SUMS asset set does not match index and SDK targets")
        index_digest = assets.get("skia-sdk-index.json", {}).get("digest")
        if index_digest and index_digest != f"sha256:{file_sha256(index_path)}":
            raise RuntimeError("GitHub index asset digest mismatch")
        sums_digest = assets.get("SHA256SUMS", {}).get("digest")
        if sums_digest and sums_digest != f"sha256:{file_sha256(sums_path)}":
            raise RuntimeError("GitHub SHA256SUMS asset digest mismatch")
        if set(assets) != expected_assets:
            raise RuntimeError("release asset set is not exactly index, sums, and SDK targets")
    output = args.output.resolve()
    output.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"lock": str(output), "set_id": lock["set_id"], "tag": args.tag}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
