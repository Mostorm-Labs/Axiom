#!/usr/bin/env python3
"""Materialize POC-01 dependencies into the ignored .deps directory.

The lock file is the source of truth. This script deliberately refuses to use a
moving branch after checkout and verifies every resulting Git revision. Skia's
own sync-deps/GN steps stay explicit because they are expensive and platform
specific.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "deps.lock.json"
DEPS = ROOT / ".deps"


def run(*args: str, cwd: Path | None = None) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=cwd, check=True)


def output(*args: str, cwd: Path | None = None) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def clone_pinned(name: str, url: str, revision: str) -> Path:
    destination = DEPS / name
    marker = destination / ".canvas-poc-revision"
    if marker.exists():
        actual = marker.read_text(encoding="utf-8").strip()
        if actual != revision:
            raise RuntimeError(f"{name}: expected {revision}, got archive {actual}")
        return destination
    if not destination.exists():
        run("git", "clone", "--filter=blob:none", "--no-checkout", url, str(destination))
    run("git", "fetch", "--depth=1", "origin", revision, cwd=destination)
    run("git", "checkout", "--detach", "FETCH_HEAD", cwd=destination)
    actual = output("git", "rev-parse", "HEAD", cwd=destination)
    if actual != revision:
        raise RuntimeError(f"{name}: expected {revision}, got {actual}")
    return destination


def github_archive(name: str, url: str, revision: str) -> Path:
    """Fetch via authenticated GitHub API when local HTTPS Git is unavailable."""
    destination = DEPS / name
    marker = destination / ".canvas-poc-revision"
    if marker.exists() and marker.read_text(encoding="utf-8").strip() == revision:
        return destination
    parts = url.removesuffix(".git").split("github.com/", 1)
    if len(parts) != 2:
        raise RuntimeError(f"archive fallback only supports GitHub URLs: {url}")
    repository = parts[1]
    with tempfile.TemporaryDirectory(prefix="canvas-poc01-") as temporary:
        archive = Path(temporary) / "source.tar.gz"
        with archive.open("wb") as output_file:
            print(f"+ gh api repos/{repository}/tarball/{revision}", flush=True)
            subprocess.run(
                ["gh", "api", f"repos/{repository}/tarball/{revision}"],
                stdout=output_file,
                check=True,
            )
        extract_root = Path(temporary) / "extract"
        extract_root.mkdir()
        with tarfile.open(archive, "r:gz") as source:
            source.extractall(extract_root)
        children = list(extract_root.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise RuntimeError(f"unexpected GitHub archive layout for {repository}")
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(children[0]), destination)
    marker.write_text(revision + "\n", encoding="utf-8")
    return destination


def github_codeload_archive(name: str, url: str, revision: str) -> Path:
    parts = url.removesuffix(".git").split("github.com/", 1)
    if len(parts) != 2:
        raise RuntimeError(f"codeload fallback only supports GitHub URLs: {url}")
    repository = parts[1]
    destination = DEPS / name
    marker = destination / ".canvas-poc-revision"
    if marker.exists() and marker.read_text(encoding="utf-8").strip() == revision:
        return destination
    archive = DEPS / "downloads" / f"{name}-{revision}.tar.gz"
    archive.parent.mkdir(parents=True, exist_ok=True)
    print(f"+ resumable codeload {repository}@{revision}", flush=True)
    subprocess.run(
        [
            "curl", "-L", "--fail", "--retry", "10", "--retry-all-errors",
            "--continue-at", "-", "--output", str(archive),
            f"https://codeload.github.com/{repository}/tar.gz/{revision}",
        ],
        check=True,
    )
    with tempfile.TemporaryDirectory(prefix="canvas-poc01-") as temporary:
        extract_root = Path(temporary) / "extract"
        extract_root.mkdir()
        with tarfile.open(archive, "r:gz") as source:
            source.extractall(extract_root)
        children = list(extract_root.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise RuntimeError(f"unexpected codeload layout for {repository}")
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(children[0]), destination)
    marker.write_text(revision + "\n", encoding="utf-8")
    return destination


def raw_core_sources(name: str, url: str, revision: str) -> Path:
    parts = url.removesuffix(".git").split("github.com/", 1)
    if len(parts) != 2:
        raise RuntimeError(f"raw fallback only supports GitHub URLs: {url}")
    repository = parts[1]
    destination = DEPS / name
    marker = destination / ".canvas-poc-revision"
    if marker.exists() and marker.read_text(encoding="utf-8").strip() == revision:
        return destination
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    if name == "nlohmann-json":
        files = ["single_include/nlohmann/json.hpp"]
    elif name == "xxhash":
        files = ["xxhash.c", "xxhash.h"]
    else:
        return github_archive(name, url, revision)
    for relative in files:
        download(
            f"https://raw.githubusercontent.com/{repository}/{revision}/{relative}",
            destination / relative,
        )
    marker.write_text(revision + "\n", encoding="utf-8")
    return destination


def verify_sha256(path: Path, expected: str) -> None:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected:
        raise RuntimeError(f"{path}: expected SHA-256 {expected}, got {digest}")


def download(url: str, path: Path, sha256: str | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and sha256:
        try:
            verify_sha256(path, sha256)
            return
        except RuntimeError:
            path.unlink()
    if not path.exists():
        print(f"+ download {url} -> {path}", flush=True)
        partial = path.with_suffix(path.suffix + ".partial")
        if shutil.which("curl"):
            subprocess.run(
                [
                    "curl", "-L", "--fail", "--retry", "10",
                    "--retry-all-errors", "--continue-at", "-", "--output",
                    str(partial), url,
                ],
                check=True,
            )
        else:
            with urllib.request.urlopen(url) as response, partial.open("wb") as target:
                shutil.copyfileobj(response, target)
        partial.replace(path)
    if sha256:
        verify_sha256(path, sha256)


def bootstrap_core(lock: dict, use_archives: bool, use_raw: bool) -> None:
    for key, directory in (
        ("googletest", "googletest"),
        ("nlohmann_json", "nlohmann-json"),
        ("xxhash", "xxhash"),
    ):
        dependency = lock["dependencies"][key]
        if use_raw:
            raw_core_sources(directory, dependency["url"], dependency["commit"])
        elif use_archives:
            github_archive(directory, dependency["url"], dependency["commit"])
        else:
            clone_pinned(directory, dependency["url"], dependency["commit"])


def bootstrap_skia(lock: dict, sync: bool) -> None:
    dependency = lock["dependencies"]["skia"]
    skia = clone_pinned("skia", dependency["url"], dependency["commit"])
    font = skia / lock["dependencies"]["roboto_regular"]["source"].split("skia/", 1)[1]
    if not font.exists():
        raise RuntimeError(f"Skia checkout did not contain {font}")
    verify_sha256(font, lock["dependencies"]["roboto_regular"]["sha256"])
    if sync:
        run(sys.executable, "tools/git-sync-deps", cwd=skia)
        # Skia's dependency sync checks out GN sources but does not materialize
        # the host executable expected by the documented GN/Ninja workflow.
        # fetch-gn pins its CIPD revision in the selected Skia commit.
        run(sys.executable, "bin/fetch-gn", cwd=skia)


def bootstrap_skia_archive(lock: dict, sync: bool) -> None:
    dependency = lock["dependencies"]["skia"]
    revision = dependency["commit"]
    skia = github_codeload_archive("skia", dependency["url"], revision)
    font = skia / lock["dependencies"]["roboto_regular"]["source"].split("skia/", 1)[1]
    verify_sha256(font, lock["dependencies"]["roboto_regular"]["sha256"])
    if sync:
        run(sys.executable, "tools/git-sync-deps", cwd=skia)
        run(sys.executable, "bin/fetch-gn", cwd=skia)


def bootstrap_font(lock: dict) -> None:
    skia = lock["dependencies"]["skia"]
    font = lock["dependencies"]["roboto_regular"]
    relative = font["source"].split("skia/", 1)[1]
    destination = DEPS / "assets" / "Roboto-Regular.ttf"
    download(
        f"https://raw.githubusercontent.com/google/skia/{skia['commit']}/{relative}",
        destination,
        font["sha256"],
    )


def bootstrap_web(lock: dict) -> None:
    dependency = lock["dependencies"]["emscripten"]
    emsdk = DEPS / "emsdk"
    tag = dependency["version"]
    if not emsdk.exists():
        run("git", "clone", "--depth=1", "--branch", tag, dependency["url"], str(emsdk))
    run("git", "fetch", "--tags", "origin", cwd=emsdk)
    run("git", "checkout", "--detach", tag, cwd=emsdk)
    launcher = emsdk / ("emsdk.bat" if platform.system() == "Windows" else "emsdk")
    run(str(launcher), "install", tag, cwd=emsdk)
    run(str(launcher), "activate", tag, cwd=emsdk)


def bootstrap_node(lock: dict) -> None:
    dependency = lock["dependencies"]["node"]
    system = platform.system().lower()
    machine = platform.machine().lower()
    if system == "darwin":
        platform_key = "darwin-arm64" if machine in ("arm64", "aarch64") else "darwin-x64"
    elif system == "linux":
        platform_key = "linux-x64" if machine in ("x86_64", "amd64") else ""
    elif system == "windows":
        platform_key = "windows-x64" if machine in ("x86_64", "amd64") else ""
    else:
        platform_key = ""
    if platform_key not in dependency["archives"]:
        raise RuntimeError(f"Node bootstrap does not support {system}/{machine}")
    archive_info = dependency["archives"][platform_key]
    version = dependency["version"]
    node_platform = "win-x64" if platform_key == "windows-x64" else platform_key
    filename = f"node-v{version}-{node_platform}.{archive_info['extension']}"
    archive = DEPS / "downloads" / filename
    download(
        f"https://nodejs.org/dist/v{version}/{filename}",
        archive,
        archive_info["sha256"],
    )
    destination = DEPS / "node"
    marker = destination / ".canvas-poc-version"
    if marker.exists() and marker.read_text(encoding="utf-8").strip() == version:
        return
    with tempfile.TemporaryDirectory(prefix="canvas-poc01-node-") as temporary:
        extracted = Path(temporary)
        if archive_info["extension"] == "zip":
            with zipfile.ZipFile(archive) as source:
                source.extractall(extracted)
        else:
            with tarfile.open(archive, "r:gz") as source:
                source.extractall(extracted)
        children = list(extracted.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise RuntimeError("unexpected Node archive layout")
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(children[0]), destination)
    marker.write_text(version + "\n", encoding="utf-8")


def bootstrap_windows_llvm(lock: dict) -> None:
    if platform.system() != "Windows":
        raise RuntimeError("--windows-llvm must run on Windows")
    dependency = lock["dependencies"]["windows_llvm"]
    installer = DEPS / "downloads" / f"LLVM-{dependency['version']}-win64.exe"
    download(dependency["url"], installer, dependency["sha256"])
    print(f"Verified installer: {installer}")
    print("Installation is intentionally explicit; CI invokes the verified installer silently.")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", action="store_true", help="Fetch test/header dependencies")
    parser.add_argument("--skia", action="store_true", help="Fetch the pinned Skia checkout")
    parser.add_argument("--font-only", action="store_true", help="Fetch and verify only pinned Roboto")
    parser.add_argument("--skia-archive", action="store_true", help="Use gh API archive fallback for Skia")
    parser.add_argument("--sync-skia", action="store_true", help="Also run Skia tools/git-sync-deps")
    parser.add_argument("--web", action="store_true", help="Install and activate pinned emsdk")
    parser.add_argument("--node", action="store_true", help="Install pinned Node into .deps/node")
    parser.add_argument("--windows-llvm", action="store_true", help="Download and verify LLVM on Windows")
    parser.add_argument(
        "--github-api-archives",
        action="store_true",
        help="Use gh API source archives for core deps when HTTPS Git is blocked",
    )
    parser.add_argument(
        "--raw-core-fallback",
        action="store_true",
        help="Use immutable raw GitHub files for header/source-only core deps",
    )
    args = parser.parse_args()
    if not any((args.core, args.skia, args.skia_archive, args.font_only, args.sync_skia, args.web, args.node, args.windows_llvm)):
        args.core = True
    DEPS.mkdir(parents=True, exist_ok=True)
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    if args.core:
        bootstrap_core(lock, args.github_api_archives, args.raw_core_fallback)
    if args.skia_archive:
        bootstrap_skia_archive(lock, args.sync_skia)
    elif args.skia or args.sync_skia:
        bootstrap_skia(lock, args.sync_skia)
    if args.font_only:
        bootstrap_font(lock)
    if args.web:
        bootstrap_web(lock)
    if args.node:
        bootstrap_node(lock)
    if args.windows_llvm:
        bootstrap_windows_llvm(lock)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
