#!/usr/bin/env python3
"""Reuse a verified SDK artifact from an earlier Full Producer run.

The artifact is accepted only after the exact current profile/recipe and the
current target toolchain identity validate its manifest.  A cache hit is
therefore an optimization, never a relaxed supply-chain path.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import urllib.error
import urllib.request
import zipfile

from sdk import load_profile, validate_symbols_manifest
from verify import safe_name, verify_archive


def request_json(url: str, token: str) -> dict:
    request = urllib.request.Request(
        url, headers={"Accept": "application/vnd.github+json", "Authorization": f"Bearer {token}"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def download(url: str, token: str, destination: Path) -> str:
    request = urllib.request.Request(
        url, headers={"Accept": "application/octet-stream", "Authorization": f"Bearer {token}"},
    )
    digest = hashlib.sha256()
    with urllib.request.urlopen(request, timeout=60) as response:
        with destination.open("wb") as stream:
            while chunk := response.read(1024 * 1024):
                digest.update(chunk)
                stream.write(chunk)
    return "sha256:" + digest.hexdigest()


def emit_output(hit: bool) -> None:
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"hit={'true' if hit else 'false'}\n")


def trusted_run(
    run: dict, current_run: str, trusted_head: str,
    trusted_branches: set[str],
) -> bool:
    """Limit reuse to the same revision or the protected publication branch."""
    return (
        str(run.get("id")) != str(current_run)
        and run.get("status") == "completed"
        and (
            run.get("head_sha") == trusted_head
            or run.get("head_branch") in trusted_branches
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--workflow", default="skia-sdk-r1-full-producer.yml")
    parser.add_argument("--current-run", default=os.environ.get("GITHUB_RUN_ID", ""))
    parser.add_argument("--trusted-head", default=os.environ.get("GITHUB_SHA", ""))
    parser.add_argument("--trusted-branch", action="append", default=[])
    parser.add_argument("--target", required=True)
    parser.add_argument("--variant", choices=("release", "debug", "asan"), required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--toolchain-json", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if not args.repository or not token:
        emit_output(False)
        print("artifact reuse unavailable: GitHub repository/token is missing")
        return 0

    profile = load_profile(args.profile.resolve())
    expected_toolchain = json.loads(args.toolchain_json.read_text(encoding="utf-8"))
    asset_name = f"skia-sdk-{profile['profile']}-{args.target}-{args.variant}.zip"
    symbols_name = (
        f"skia-sdk-{profile['profile']}-{args.target}-{args.variant}-symbols.zip"
        if args.variant != "release" else None
    )
    api_root = f"https://api.github.com/repos/{args.repository}"
    runs_url = f"{api_root}/actions/workflows/{args.workflow}/runs?per_page=50"
    try:
        runs = request_json(runs_url, token).get("workflow_runs", [])
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError) as error:
        print(f"artifact reuse lookup failed: {error}")
        emit_output(False)
        return 0

    for run in runs:
        run_id = run.get("id")
        if not run_id or not trusted_run(
            run, args.current_run, args.trusted_head,
            set(args.trusted_branch or ["main"]),
        ):
            continue
        try:
            artifacts = request_json(
                f"{api_root}/actions/runs/{run_id}/artifacts?per_page=100", token,
            ).get("artifacts", [])
            artifact = next(
                (item for item in artifacts
                 if item.get("name") == f"r1-full-skia-{args.target}-{args.variant}"
                 and not item.get("expired")),
                None,
            )
            if not artifact:
                continue
            with tempfile.TemporaryDirectory(prefix="canvas-skia-reuse-") as temporary:
                artifact_zip = Path(temporary) / "artifact.zip"
                actual_digest = download(
                    artifact["archive_download_url"], token, artifact_zip,
                )
                expected_digest = artifact.get("digest")
                if expected_digest and expected_digest != actual_digest:
                    raise RuntimeError("GitHub artifact digest mismatch")
                with zipfile.ZipFile(artifact_zip) as outer:
                    names = outer.namelist()
                    if len(names) != len(set(names)):
                        raise RuntimeError("artifact contains duplicate paths")
                    for info in outer.infolist():
                        name = info.filename
                        safe_name(name)
                        mode = info.external_attr >> 16
                        if info.is_dir() or mode & 0o170000 == 0o120000:
                            raise RuntimeError("artifact contains a directory or symlink")
                    expected_names = {asset_name} | (
                        {symbols_name} if symbols_name else set()
                    )
                    if set(names) != expected_names:
                        raise RuntimeError("artifact contains an unexpected asset set")
                    candidate = args.output.resolve() / asset_name
                    candidate.parent.mkdir(parents=True, exist_ok=True)
                    with outer.open(asset_name) as source, candidate.open("wb") as output:
                        shutil.copyfileobj(source, output)
                    if symbols_name:
                        symbols = args.output.resolve() / symbols_name
                        with outer.open(symbols_name) as source, symbols.open("wb") as output:
                            shutil.copyfileobj(source, output)
            summary = verify_archive(
                candidate, args.profile.resolve(), args.target,
                expected_variant=args.variant,
            )
            with zipfile.ZipFile(candidate) as archive:
                manifest = json.loads(archive.read("manifest.json"))
            if manifest["identity"]["toolchain"] != expected_toolchain:
                raise RuntimeError("toolchain identity differs from this runner")
            if symbols_name:
                symbols = args.output.resolve() / symbols_name
                with zipfile.ZipFile(symbols) as archive:
                    symbol_names = archive.namelist()
                    if len(symbol_names) != len(set(symbol_names)):
                        raise RuntimeError("symbols archive contains duplicate paths")
                    for name in symbol_names:
                        safe_name(name)
                    metadata = json.loads(archive.read("symbols.json"))
                    validate_symbols_manifest(
                        metadata, expected_sdk_id=summary["sdk_id"],
                        expected_target=args.target, expected_variant=args.variant,
                    )
                    listed = {entry["path"]: entry for entry in metadata["files"]}
                    if set(symbol_names) - {"symbols.json"} != set(listed):
                        raise RuntimeError(
                            "symbols archive payload does not match its manifest"
                        )
                    for path, entry in listed.items():
                        payload = archive.read(path)
                        if len(payload) != entry["size"] or \
                           hashlib.sha256(payload).hexdigest() != entry["sha256"]:
                            raise RuntimeError(
                                f"symbols archive checksum mismatch: {path}"
                            )
            print(f"reused verified artifact from run {run_id}: {asset_name}")
            emit_output(True)
            return 0
        except (OSError, KeyError, RuntimeError, ValueError, zipfile.BadZipFile) as error:
            print(f"rejected artifact from run {run_id}: {error}")
            if args.output.exists():
                for path in args.output.glob(f"*{args.target}*{args.variant}*"):
                    path.unlink(missing_ok=True)
            continue
    print(f"no reusable verified artifact found for {args.target}/{args.variant}")
    emit_output(False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
