#!/usr/bin/env python3
"""Strict metadata validation shared by the Skia SDK lock and fetch tools."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from sdk import (
    ROOT, SchemaError, TOOLCHAIN_FIELDS, canonical_sha256, require_fields,
)


LOCK_PATH = ROOT / "skia-sdk.lock.json"
LOCK_FIELDS = {
    "schema_version", "format", "repository", "tag", "set_id", "profile",
    "profile_hash", "skia_commit", "source_commit", "targets",
}
LOCK_TARGET_FIELDS = {"asset", "sdk_id", "sha256", "size", "toolchain"}
INDEX_FIELDS = {
    "schema_version", "format", "set_id", "profile", "profile_hash",
    "skia_commit", "source_repository", "source_commit", "targets",
}
INDEX_TARGET_FIELDS = LOCK_TARGET_FIELDS
EXPECTED_TARGETS = {
    "windows-x64-d3d12", "web-wasm-webgl2", "macos-arm64-metal",
    "ios-arm64-metal", "ios-simulator-arm64-metal",
    "android-arm64-v8a-gles3", "android-x86_64-gles3",
}


def _hex(value: Any, length: int, where: str) -> str:
    if not isinstance(value, str) or len(value) != length:
        raise SchemaError(f"{where} must be {length} lowercase hexadecimal characters")
    if value != value.lower() or any(character not in "0123456789abcdef" for character in value):
        raise SchemaError(f"{where} must be lowercase hexadecimal")
    return value


def validate_index(value: Any) -> dict[str, Any]:
    index = require_fields(value, INDEX_FIELDS, "SDK index")
    if index["schema_version"] != 1 or index["format"] != "canvas-skia-sdk-set-v1":
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
    if not isinstance(index["targets"], dict) or \
       set(index["targets"]) != EXPECTED_TARGETS:
        raise SchemaError("SDK index target set does not match the seven-target contract")
    sdk_ids: dict[str, str] = {}
    for target_name, target in index["targets"].items():
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
        "schema_version": 1,
        "format": "canvas-skia-sdk-lock-v1",
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
    if lock["schema_version"] != 1 or lock["format"] != "canvas-skia-sdk-lock-v1":
        raise SchemaError("unsupported SDK lock schema or format")
    if not isinstance(lock["repository"], str) or lock["repository"].count("/") != 1:
        raise SchemaError("SDK lock repository must be owner/name")
    if not isinstance(lock["tag"], str) or not lock["tag"]:
        raise SchemaError("SDK lock tag must be non-empty")
    synthetic_index = {
        "schema_version": 1,
        "format": "canvas-skia-sdk-set-v1",
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
