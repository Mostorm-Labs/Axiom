#!/usr/bin/env python3
"""Strict metadata validation shared by the Skia SDK lock and fetch tools."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from sdk import (
    ROOT, SDK_VARIANTS, SchemaError, TOOLCHAIN_FIELDS, canonical_sha256,
    require_fields,
)


LOCK_PATH = ROOT / "skia-sdk.lock.json"
LOCK_FIELDS = {
    "schema_version", "format", "repository", "tag", "set_id", "profile",
    "profile_hash", "skia_commit", "source_commit", "targets",
}
LOCK_TARGET_FIELDS = {"asset", "sdk_id", "sha256", "size", "toolchain"}
FULL_LOCK_TARGET_FIELDS = {"variants"}
FULL_LOCK_VARIANT_FIELDS = LOCK_TARGET_FIELDS | {
    "capabilities", "variant_metadata", "symbols_asset", "symbols_sha256",
    "symbols_size",
}
INDEX_FIELDS = {
    "schema_version", "format", "set_id", "profile", "profile_hash",
    "skia_commit", "source_repository", "source_commit", "targets",
}
INDEX_TARGET_FIELDS = LOCK_TARGET_FIELDS
POC01_EXPECTED_TARGETS = {
    "windows-x64-d3d12", "web-wasm-webgl2", "macos-arm64-metal",
    "ios-arm64-metal", "ios-simulator-arm64-metal",
    "android-arm64-v8a-gles3", "android-x86_64-gles3",
}
FULL_EXPECTED_TARGETS = POC01_EXPECTED_TARGETS | {"macos-x64-metal"}


def _hex(value: Any, length: int, where: str) -> str:
    if not isinstance(value, str) or len(value) != length:
        raise SchemaError(f"{where} must be {length} lowercase hexadecimal characters")
    if value != value.lower() or any(character not in "0123456789abcdef" for character in value):
        raise SchemaError(f"{where} must be lowercase hexadecimal")
    return value


def validate_index(value: Any) -> dict[str, Any]:
    index = require_fields(value, INDEX_FIELDS, "SDK index")
    full = index["schema_version"] == 2 or index["format"] == "canvas-skia-sdk-set-v2"
    if full and (index["schema_version"] != 2 or index["format"] != "canvas-skia-sdk-set-v2"):
        raise SchemaError("unsupported full SDK index schema or format")
    if not full and (index["schema_version"] != 1 or index["format"] != "canvas-skia-sdk-set-v1"):
        raise SchemaError("unsupported SDK index schema or format")
    _hex(index["set_id"], 64, "index set_id")
    _hex(index["profile_hash"], 64, "index profile_hash")
    _hex(index["skia_commit"], 40, "index Skia commit")
    _hex(index["source_commit"], 40, "index source commit")
    if not isinstance(index["profile"], str) or not index["profile"]:
        raise SchemaError("index profile must be non-empty")
    if not isinstance(index["source_repository"], str) or \
       index["source_repository"].count("/") != 1:
        raise SchemaError("index source_repository must be owner/name")
    if index["source_commit"] == "0" * 40:
        raise SchemaError("SDK index source_commit must identify the producer commit")
    if not isinstance(index["targets"], dict) or not index["targets"]:
        raise SchemaError("SDK index must contain at least one target")
    if full and set(index["targets"]) != FULL_EXPECTED_TARGETS:
        raise SchemaError("full SDK index must contain the exact eight-target matrix")
    sdk_ids: dict[str, Any] = {}
    for target_name, target in index["targets"].items():
        if full:
            require_fields(target, FULL_LOCK_TARGET_FIELDS, f"index target {target_name}")
            variants = target["variants"]
            if set(variants) != set(SDK_VARIANTS):
                raise SchemaError(f"index target {target_name} must contain three variants")
            sdk_ids[target_name] = {}
            for variant, metadata in variants.items():
                require_fields(metadata, FULL_LOCK_VARIANT_FIELDS,
                               f"index target {target_name}/{variant}")
                expected_asset = f"skia-sdk-{index['profile']}-{target_name}-{variant}.zip"
                if metadata["asset"] != expected_asset:
                    raise SchemaError(f"index target {target_name}/{variant} asset mismatch")
                sdk_ids[target_name][variant] = _hex(
                    metadata["sdk_id"], 64, f"{target_name}/{variant} sdk_id",
                )
                _hex(metadata["sha256"], 64, f"{target_name}/{variant} SHA-256")
                if not isinstance(metadata["size"], int) or metadata["size"] <= 0:
                    raise SchemaError(f"index target {target_name}/{variant} has invalid size")
                if not isinstance(metadata["toolchain"], dict) or \
                   not isinstance(metadata["capabilities"], dict) or \
                   not isinstance(metadata["variant_metadata"], dict):
                    raise SchemaError(f"index target {target_name}/{variant} metadata invalid")
                unknown_toolchain = set(metadata["toolchain"]) - TOOLCHAIN_FIELDS
                if unknown_toolchain:
                    raise SchemaError(
                        f"index target {target_name}/{variant} has unknown toolchain fields"
                    )
                variant_metadata = metadata["variant_metadata"]
                if variant_metadata.get("sanitize") != ("ASAN" if variant == "asan" else ""):
                    raise SchemaError(
                        f"index target {target_name}/{variant} sanitizer metadata mismatch"
                    )
                if variant_metadata.get("is_debug") != (variant != "release") or \
                   variant_metadata.get("is_official_build") != (variant == "release"):
                    raise SchemaError(
                        f"index target {target_name}/{variant} build metadata mismatch"
                    )
                if variant == "asan" and not variant_metadata.get(
                    "requires_instrumented_consumer"
                ):
                    raise SchemaError("ASan lock entry requires an instrumented consumer")
                if metadata["capabilities"].get("raw_dng") is not False:
                    raise SchemaError("full SDK lock must keep raw_dng disabled")
                if variant == "release" and metadata["variant_metadata"].get(
                    "consumer_default"
                ) is not True:
                    raise SchemaError("release must be the default Full SDK variant")
                if variant != "release" and metadata["variant_metadata"].get(
                    "consumer_default"
                ) is not False:
                    raise SchemaError(f"{variant} must not be the default Full SDK variant")
                if variant == "release" and metadata["symbols_asset"] is not None:
                    raise SchemaError("release variant must not have a symbols asset")
                if variant == "release" and (
                    metadata["symbols_sha256"] is not None or
                    metadata["symbols_size"] is not None
                ):
                    raise SchemaError("release variant must not have symbols metadata")
                if variant != "release":
                    if metadata["symbols_asset"] != \
                       f"skia-sdk-{index['profile']}-{target_name}-{variant}-symbols.zip":
                        raise SchemaError("debug/asan symbols asset name mismatch")
                    _hex(metadata["symbols_sha256"], 64, "symbols SHA-256")
                    if not isinstance(metadata["symbols_size"], int) or metadata["symbols_size"] <= 0:
                        raise SchemaError("symbols asset has invalid size")
            continue
        require_fields(target, INDEX_TARGET_FIELDS, f"index target {target_name}")
        if target["asset"] != f"skia-sdk-{target_name}.zip":
            raise SchemaError(f"index target {target_name} asset name mismatch")
        sdk_ids[target_name] = _hex(target["sdk_id"], 64, f"{target_name} sdk_id")
        _hex(target["sha256"], 64, f"{target_name} SHA-256")
        if not isinstance(target["size"], int) or target["size"] <= 0:
            raise SchemaError(f"index target {target_name} has invalid size")
        if not isinstance(target["toolchain"], dict):
            raise SchemaError(f"index target {target_name} toolchain must be an object")
        unknown_toolchain = set(target["toolchain"]) - TOOLCHAIN_FIELDS
        if unknown_toolchain:
            raise SchemaError(
                f"index target {target_name} has unknown toolchain fields: "
                + ", ".join(sorted(unknown_toolchain))
            )
    if index["set_id"] != canonical_sha256(sdk_ids):
        raise SchemaError("index set_id does not match target SDK IDs")
    return index


def lock_from_index(repository: str, tag: str, index: dict[str, Any]) -> dict[str, Any]:
    validate_index(index)
    if index["source_repository"] != repository:
        raise SchemaError("release repository does not match index source_repository")
    return {
        "schema_version": index["schema_version"],
        "format": "canvas-skia-sdk-lock-v2" if index["schema_version"] == 2 else "canvas-skia-sdk-lock-v1",
        "repository": repository,
        "tag": tag,
        "set_id": index["set_id"],
        "profile": index["profile"],
        "profile_hash": index["profile_hash"],
        "skia_commit": index["skia_commit"],
        "source_commit": index["source_commit"],
        "targets": index["targets"],
    }


def validate_lock(value: Any) -> dict[str, Any]:
    lock = require_fields(value, LOCK_FIELDS, "SDK lock")
    full = lock["schema_version"] == 2 or lock["format"] == "canvas-skia-sdk-lock-v2"
    if full and (lock["schema_version"] != 2 or lock["format"] != "canvas-skia-sdk-lock-v2"):
        raise SchemaError("unsupported full SDK lock schema or format")
    if not full and (lock["schema_version"] != 1 or lock["format"] != "canvas-skia-sdk-lock-v1"):
        raise SchemaError("unsupported SDK lock schema or format")
    if not isinstance(lock["repository"], str) or lock["repository"].count("/") != 1:
        raise SchemaError("SDK lock repository must be owner/name")
    if not isinstance(lock["tag"], str) or not lock["tag"]:
        raise SchemaError("SDK lock tag must be non-empty")
    synthetic_index = {
        "schema_version": 2 if full else 1,
        "format": "canvas-skia-sdk-set-v2" if full else "canvas-skia-sdk-set-v1",
        "set_id": lock["set_id"],
        "profile": lock["profile"],
        "profile_hash": lock["profile_hash"],
        "skia_commit": lock["skia_commit"],
        "source_repository": lock["repository"],
        "source_commit": lock["source_commit"],
        "targets": lock["targets"],
    }
    validate_index(synthetic_index)
    return lock


def load_lock(path: Path = LOCK_PATH) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"missing Skia SDK lock: {path}")
    return validate_lock(json.loads(path.read_text(encoding="utf-8")))
