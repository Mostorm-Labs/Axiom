#!/usr/bin/env python3
"""Strictly verify a Canvas Skia SDK archive before it is consumed."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tempfile
import zipfile

from sdk import (
    DEFAULT_PROFILE, ROOT, file_sha256, load_profile, normalized_args_text,
    target_definition, validate_manifest,
)


def safe_name(name: str) -> PurePosixPath:
    if not name or "\\" in name or ":" in name:
        raise RuntimeError(f"unsafe ZIP path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise RuntimeError(f"unsafe ZIP path: {name!r}")
    return path


def archive_architectures(data: bytes) -> set[str]:
    """Return object architectures found in a regular Unix ar archive."""
    if not data.startswith(b"!<arch>\n"):
        return set()
    result: set[str] = set()
    offset = 8
    while offset + 60 <= len(data):
        header = data[offset:offset + 60]
        if header[58:60] != b"`\n":
            break
        try:
            size = int(header[48:58].decode("ascii").strip())
        except ValueError:
            break
        body = data[offset + 60:offset + 60 + size]
        name = header[:16].decode("ascii", errors="ignore").strip()
        if name.startswith("#1/"):
            name_size = int(name[3:])
            body = body[name_size:]
        if body.startswith(b"\x00asm"):
            result.add("wasm32")
        elif body.startswith(b"\xcf\xfa\xed\xfe") and len(body) >= 8:
            cpu = int.from_bytes(body[4:8], "little")
            result.add({0x0100000C: "arm64", 0x01000007: "x64"}.get(cpu, f"macho-{cpu}"))
        elif body.startswith(b"\x7fELF") and len(body) >= 20:
            machine = int.from_bytes(body[18:20], "little")
            result.add({183: "arm64-v8a", 62: "x86_64"}.get(machine, f"elf-{machine}"))
        elif len(body) >= 2:
            machine = int.from_bytes(body[:2], "little")
            # COFF bigobj and import-library members begin with the anonymous
            # object signature 0x0000, 0xffff and store Machine at offset 6.
            # Skia's Windows release archives predominantly use bigobj, while
            # smaller dependencies can still contain ordinary COFF objects.
            if body[:4] == b"\x00\x00\xff\xff" and len(body) >= 8:
                machine = int.from_bytes(body[6:8], "little")
            if machine == 0x8664:
                result.add("x64")
        offset += 60 + size + (size & 1)
    return result


def verify_archive(
    archive_path: Path, profile_path: Path, expected_target: str,
    extract_to: Path | None = None,
) -> dict:
    profile = load_profile(profile_path)
    target = target_definition(profile, expected_target)
    with zipfile.ZipFile(archive_path) as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        if len(names) != len(set(names)):
            raise RuntimeError("ZIP contains duplicate paths")
        if len(names) != len({name.casefold() for name in names}):
            raise RuntimeError("ZIP contains case-colliding paths")
        for info in infos:
            safe_name(info.filename)
            mode = info.external_attr >> 16
            if mode & 0o170000 == 0o120000:
                raise RuntimeError(f"ZIP contains a symbolic link: {info.filename}")
            if info.is_dir():
                raise RuntimeError(f"SDK ZIP must not contain directory entries: {info.filename}")
        if "manifest.json" not in names:
            raise RuntimeError("SDK ZIP has no manifest.json")
        manifest = validate_manifest(
            json.loads(archive.read("manifest.json")), profile, expected_target,
            profile_path,
        )
        listed = {entry["path"]: entry for entry in manifest["files"]}
        actual_payload = set(names) - {"manifest.json"}
        if actual_payload != set(listed):
            missing = sorted(set(listed) - actual_payload)
            extra = sorted(actual_payload - set(listed))
            raise RuntimeError(f"manifest payload mismatch; missing={missing}, extra={extra}")
        for path, entry in listed.items():
            data = archive.read(path)
            if len(data) != entry["size"]:
                raise RuntimeError(f"size mismatch: {path}")
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise RuntimeError(f"SHA-256 mismatch: {path}")

        required = {
            "args.gn", "include/core/SkCanvas.h",
            "modules/skcms/skcms.h", "modules/skcms/src/skcms_public.h",
            "resources/fonts/Roboto-Regular.ttf",
            "licenses/Skia.txt", "licenses/FreeType.txt",
            "licenses/libpng.txt", "licenses/zlib.txt",
            "lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake",
        }
        required.update(f"lib/{name}" for name in target["libraries"])
        absent = sorted(required - actual_payload)
        if absent:
            raise RuntimeError(f"SDK is incomplete: {absent}")
        if archive.read("args.gn").decode("utf-8") != normalized_args_text(profile, expected_target):
            raise RuntimeError("normalized args.gn does not match the profile")
        lock = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))
        expected_font = lock["dependencies"]["roboto_regular"]["sha256"]
        actual_font = hashlib.sha256(archive.read("resources/fonts/Roboto-Regular.ttf")).hexdigest()
        if actual_font != expected_font:
            raise RuntimeError("Roboto fixture checksum mismatch")
        expected_archive_arch = {
            "windows": "x64", "web": "wasm32", "macos": "arm64",
            "ios": "arm64", "ios-simulator": "arm64",
            "android": target["arch"],
        }[target["platform"]]
        for library in target["libraries"]:
            library_data = archive.read(f"lib/{library}")
            if not library_data.startswith(b"!<arch>\n"):
                raise RuntimeError(f"static library has invalid archive magic: {library}")
            architectures = archive_architectures(library_data)
            if expected_archive_arch not in architectures:
                raise RuntimeError(
                    f"static library architecture mismatch for {library}: "
                    f"expected {expected_archive_arch}, found {sorted(architectures)}"
                )

        if extract_to is not None:
            temporary = Path(tempfile.mkdtemp(prefix="canvas-skia-extract-", dir=extract_to.parent))
            try:
                for info in infos:
                    destination = temporary.joinpath(*safe_name(info.filename).parts)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(info) as source, destination.open("wb") as output:
                        shutil.copyfileobj(source, output)
                if extract_to.exists():
                    shutil.rmtree(extract_to)
                temporary.replace(extract_to)
            except BaseException:
                shutil.rmtree(temporary, ignore_errors=True)
                raise
    summary = {
        "archive": str(archive_path),
        "archive_sha256": file_sha256(archive_path),
        "sdk_id": manifest["sdk_id"],
        "target": expected_target,
    }
    print(json.dumps(summary, sort_keys=True))
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--expected-target", required=True)
    parser.add_argument("--extract-to", type=Path)
    args = parser.parse_args()
    if not args.archive.is_file():
        raise RuntimeError(f"archive does not exist: {args.archive}")
    verify_archive(
        args.archive.resolve(), args.profile.resolve(), args.expected_target,
        args.extract_to.resolve() if args.extract_to else None,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
