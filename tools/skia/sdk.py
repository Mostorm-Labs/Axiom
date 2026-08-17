#!/usr/bin/env python3
"""Shared, dependency-free primitives for the Canvas Skia SDK producer."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PROFILE = ROOT / "tools/skia/profiles/poc01-minimal-v1.json"
SKIA_ROOT = ROOT / ".deps/skia"
SDK_FORMAT = "canvas-skia-sdk-v1"
PROFILE_FIELDS = {
    "schema_version", "profile", "description", "skia_commit",
    "common_gn_args", "targets",
}
TARGET_FIELDS = {
    "platform", "arch", "backend", "output_name", "gn_args", "libraries",
    "toolchain",
}
MANIFEST_FIELDS = {
    "schema_version", "format", "sdk_id", "identity", "files",
}
IDENTITY_FIELDS = {
    "profile", "profile_hash", "skia_commit", "target", "platform", "arch",
    "backend", "gn_args", "toolchain", "recipe_hash",
}
FILE_FIELDS = {"path", "sha256", "size", "role"}
TOOLCHAIN_FIELDS = {
    "emscripten", "llvm", "clang", "msvc_toolset", "windows_sdk", "xcode",
    "sdk", "sdk_version", "deployment_target", "android_ndk", "api_level",
    "pthread",
}


class SchemaError(RuntimeError):
    """Raised when a producer or consumer metadata document is not exact."""


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=True, sort_keys=True, separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalized_recipe_bytes(data: bytes) -> bytes:
    """Canonicalize text checkouts before hashing the portable recipe."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def require_fields(value: Any, required: set[str], where: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SchemaError(f"{where} must be an object")
    actual = set(value)
    unknown = actual - required
    missing = required - actual
    if unknown:
        raise SchemaError(f"{where} has unknown fields: {', '.join(sorted(unknown))}")
    if missing:
        raise SchemaError(f"{where} is missing fields: {', '.join(sorted(missing))}")
    return value


def load_profile(path: Path = DEFAULT_PROFILE) -> dict[str, Any]:
    profile = require_fields(
        json.loads(path.read_text(encoding="utf-8")), PROFILE_FIELDS, "profile",
    )
    if profile["schema_version"] != 1:
        raise SchemaError("profile schema_version must be 1")
    if profile["profile"] != path.stem:
        raise SchemaError("profile name must match its filename")
    dependency_lock = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))
    dependencies = dependency_lock["dependencies"]
    if profile["skia_commit"] != dependencies["skia"]["commit"]:
        raise SchemaError("profile Skia commit does not match deps.lock.json")
    if not isinstance(profile["common_gn_args"], dict):
        raise SchemaError("common_gn_args must be an object")
    targets = profile["targets"]
    if not isinstance(targets, dict) or not targets:
        raise SchemaError("targets must be a non-empty object")
    for target_name, target in targets.items():
        require_fields(target, TARGET_FIELDS, f"target {target_name}")
        if not isinstance(target["libraries"], list) or not target["libraries"]:
            raise SchemaError(f"target {target_name} libraries must be non-empty")
        if len(target["libraries"]) != len(set(target["libraries"])):
            raise SchemaError(f"target {target_name} libraries contain duplicates")
        if not isinstance(target["gn_args"], dict) or not isinstance(target["toolchain"], dict):
            raise SchemaError(f"target {target_name} args/toolchain must be objects")
        unknown_toolchain = set(target["toolchain"]) - TOOLCHAIN_FIELDS
        if unknown_toolchain:
            raise SchemaError(
                f"target {target_name} has unknown toolchain fields: "
                + ", ".join(sorted(unknown_toolchain))
            )
    expected = {
        "windows-x64-d3d12", "web-wasm-webgl2", "macos-arm64-metal",
        "ios-arm64-metal", "ios-simulator-arm64-metal",
        "android-arm64-v8a-gles3", "android-x86_64-gles3",
    }
    if set(targets) != expected:
        raise SchemaError(f"profile target set mismatch: {sorted(targets)}")
    web_toolchain = targets["web-wasm-webgl2"]["toolchain"]
    if web_toolchain["emscripten"] != dependencies["emscripten"]["version"] or \
       web_toolchain["llvm"] != dependencies["emscripten"]["llvm_version"]:
        raise SchemaError("Web toolchain does not match deps.lock.json")
    if targets["windows-x64-d3d12"]["toolchain"]["llvm"] != \
       dependencies["windows_llvm"]["version"]:
        raise SchemaError("Windows LLVM does not match deps.lock.json")
    for target_name in ("android-arm64-v8a-gles3", "android-x86_64-gles3"):
        toolchain = targets[target_name]["toolchain"]
        if toolchain["android_ndk"] != dependencies["android_ndk"]["version"] or \
           toolchain["api_level"] != dependencies["android_ndk"]["api_level"]:
            raise SchemaError(f"{target_name} toolchain does not match deps.lock.json")
    return profile


def profile_hash(profile: dict[str, Any]) -> str:
    return canonical_sha256(profile)


def target_definition(profile: dict[str, Any], target: str) -> dict[str, Any]:
    try:
        return profile["targets"][target]
    except KeyError as error:
        raise SchemaError(f"unknown SDK target: {target}") from error


def normalized_gn_args(profile: dict[str, Any], target: str) -> dict[str, Any]:
    result = dict(profile["common_gn_args"])
    result.update(target_definition(profile, target)["gn_args"])
    if target == "windows-x64-d3d12":
        result.update(cc="clang-cl", cxx="clang-cl")
    elif target == "web-wasm-webgl2":
        result.update(cc="emcc", cxx="em++")
    elif target.startswith("android-"):
        result["ndk"] = "${ANDROID_NDK_ROOT}"
    return result


def actual_gn_args(
    profile: dict[str, Any], target: str, *, cc: str | None = None,
    cxx: str | None = None, ndk: str | None = None,
) -> dict[str, Any]:
    result = normalized_gn_args(profile, target)
    if target == "windows-x64-d3d12":
        result["cc"] = cc or "clang-cl"
        result["cxx"] = cxx or "clang-cl"
    elif target == "web-wasm-webgl2":
        result["cc"] = cc or "emcc"
        result["cxx"] = cxx or "em++"
    elif target.startswith("android-"):
        if not ndk:
            raise RuntimeError("Android SDK build requires --ndk or ANDROID_NDK_ROOT")
        result["ndk"] = str(Path(ndk).resolve())
    return result


def gn_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return json.dumps(value)
    raise SchemaError(f"unsupported GN value: {value!r}")


def gn_args_line(args: dict[str, Any]) -> str:
    return " ".join(f"{key}={gn_value(args[key])}" for key in sorted(args))


def normalized_args_text(profile: dict[str, Any], target: str) -> str:
    args = normalized_gn_args(profile, target)
    return "\n".join(f"{key}={gn_value(args[key])}" for key in sorted(args)) + "\n"


def validate_toolchain(
    profile: dict[str, Any], target: str, toolchain: dict[str, Any],
) -> None:
    if not isinstance(toolchain, dict):
        raise SchemaError("toolchain identity must be an object")
    unknown = set(toolchain) - TOOLCHAIN_FIELDS
    if unknown:
        raise SchemaError(f"unknown toolchain fields: {', '.join(sorted(unknown))}")
    expected = target_definition(profile, target)["toolchain"]
    for key, expected_value in expected.items():
        if toolchain.get(key) != expected_value:
            raise SchemaError(
                f"toolchain identity mismatch for {key}: expected "
                f"{expected_value!r}, got {toolchain.get(key)!r}"
            )
    forbidden = (str(ROOT), str(SKIA_ROOT))
    for key, value in toolchain.items():
        if isinstance(value, str) and any(path in value for path in forbidden):
            raise SchemaError(f"toolchain {key} contains an absolute build path")


def recipe_hash(profile_path: Path = DEFAULT_PROFILE) -> str:
    digest = hashlib.sha256()
    recipe_files = [
        profile_path,
        ROOT / "tools/skia/sdk.py",
        ROOT / "tools/skia/build.py",
        ROOT / "tools/skia/identify_toolchain.py",
        ROOT / "tools/skia/package.py",
        ROOT / "tools/skia/verify.py",
    ]
    for path in recipe_files:
        digest.update(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0")
        digest.update(normalized_recipe_bytes(path.read_bytes()))
        digest.update(b"\0")
    return digest.hexdigest()


def make_identity(
    profile: dict[str, Any], target: str, toolchain: dict[str, Any],
    *, profile_path: Path = DEFAULT_PROFILE,
) -> dict[str, Any]:
    validate_toolchain(profile, target, toolchain)
    definition = target_definition(profile, target)
    return {
        "profile": profile["profile"],
        "profile_hash": profile_hash(profile),
        "skia_commit": profile["skia_commit"],
        "target": target,
        "platform": definition["platform"],
        "arch": definition["arch"],
        "backend": definition["backend"],
        "gn_args": normalized_gn_args(profile, target),
        "toolchain": toolchain,
        "recipe_hash": recipe_hash(profile_path),
    }


def validate_manifest(
    manifest: Any, profile: dict[str, Any] | None = None,
    expected_target: str | None = None, profile_path: Path = DEFAULT_PROFILE,
) -> dict[str, Any]:
    result = require_fields(manifest, MANIFEST_FIELDS, "manifest")
    if result["schema_version"] != 1 or result["format"] != SDK_FORMAT:
        raise SchemaError("unsupported SDK manifest schema or format")
    identity = require_fields(result["identity"], IDENTITY_FIELDS, "manifest identity")
    if not isinstance(identity["toolchain"], dict):
        raise SchemaError("manifest toolchain identity must be an object")
    unknown_toolchain = set(identity["toolchain"]) - TOOLCHAIN_FIELDS
    if unknown_toolchain:
        raise SchemaError(f"unknown toolchain fields: {sorted(unknown_toolchain)}")
    if result["sdk_id"] != canonical_sha256(identity):
        raise SchemaError("manifest sdk_id does not match canonical identity")
    if expected_target and identity["target"] != expected_target:
        raise SchemaError(
            f"target mismatch: expected {expected_target}, got {identity['target']}"
        )
    if profile is not None:
        target = identity["target"]
        expected_identity = make_identity(
            profile, target, identity["toolchain"], profile_path=profile_path,
        )
        if identity != expected_identity:
            raise SchemaError("manifest identity does not match the selected profile")
    if not isinstance(result["files"], list) or not result["files"]:
        raise SchemaError("manifest files must be non-empty")
    paths: list[str] = []
    for index, entry in enumerate(result["files"]):
        require_fields(entry, FILE_FIELDS, f"manifest file {index}")
        if not isinstance(entry["size"], int) or entry["size"] < 0:
            raise SchemaError(f"manifest file {index} has invalid size")
        paths.append(entry["path"])
    if paths != sorted(paths) or len(paths) != len(set(paths)):
        raise SchemaError("manifest file paths must be sorted and unique")
    return result
