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
FULL_PROFILE_FORMAT = "canvas-skia-sdk-profile-v2"
FULL_SDK_FORMAT = "canvas-skia-sdk-v2"
SDK_VARIANTS = ("release", "debug", "asan")
PROFILE_REQUIRED_FIELDS = {
    "schema_version", "profile", "description", "skia_commit",
    "common_gn_args", "targets",
}
PROFILE_OPTIONAL_FIELDS = {
    "build_targets", "module_header_dirs", "module_headers", "licenses",
    "license_dependencies", "fixture_fonts", "runtime_files",
}
FULL_PROFILE_REQUIRED_FIELDS = {
    "schema_version", "format", "profile", "description", "skia_commit",
    "common_gn_args", "variants", "targets", "capabilities",
}
FULL_PROFILE_OPTIONAL_FIELDS = {
    "build_targets", "module_header_dirs", "module_headers", "licenses",
    "license_dependencies", "fixture_fonts", "runtime_files",
    "capability_library_prefixes",
    "unsupported_reasons",
}
TARGET_FIELDS = {
    "platform", "arch", "backend", "output_name", "gn_args", "libraries",
    "toolchain",
}
MANIFEST_FIELDS = {
    "schema_version", "format", "sdk_id", "identity", "files",
}
FULL_MANIFEST_FIELDS = {
    "schema_version", "format", "sdk_id", "identity", "capabilities",
    "unsupported_reasons", "archive_closure", "files",
}
SYMBOLS_MANIFEST_FIELDS = {
    "schema_version", "format", "sdk_id", "target", "variant",
    "embedded_symbols", "files",
}
SYMBOL_FILE_FIELDS = {"path", "sha256", "size"}
IDENTITY_FIELDS = {
    "profile", "profile_hash", "skia_commit", "target", "platform", "arch",
    "backend", "gn_args", "toolchain", "recipe_hash",
}
FULL_IDENTITY_FIELDS = IDENTITY_FIELDS | {
    "variant", "variant_metadata", "capabilities",
}
FILE_FIELDS = {"path", "sha256", "size", "role"}
ARCHIVE_CLOSURE_FIELDS = {
    "source_path", "packaged_path", "sha256", "size",
}
TOOLCHAIN_FIELDS = {
    "emscripten", "llvm", "clang", "msvc_toolset", "windows_sdk", "xcode",
    "sdk", "sdk_version", "deployment_target", "android_ndk", "api_level",
    "pthread", "linker",
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
    profile = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(profile, dict):
        raise SchemaError("profile must be an object")
    is_full = profile.get("schema_version") == 2 or profile.get("format") == FULL_PROFILE_FORMAT
    required_fields = FULL_PROFILE_REQUIRED_FIELDS if is_full else PROFILE_REQUIRED_FIELDS
    optional_fields = FULL_PROFILE_OPTIONAL_FIELDS if is_full else PROFILE_OPTIONAL_FIELDS
    unknown = set(profile) - required_fields - optional_fields
    missing = required_fields - set(profile)
    if unknown:
        raise SchemaError(f"profile has unknown fields: {', '.join(sorted(unknown))}")
    if missing:
        raise SchemaError(f"profile is missing fields: {', '.join(sorted(missing))}")
    if is_full:
        if profile["schema_version"] != 2 or profile.get("format") != FULL_PROFILE_FORMAT:
            raise SchemaError("full profile must use canvas-skia-sdk-profile-v2")
    elif profile["schema_version"] != 1:
        raise SchemaError("profile schema_version must be 1")
    if profile["profile"] != path.stem:
        raise SchemaError("profile name must match its filename")
    dependency_lock = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))
    dependencies = dependency_lock["dependencies"]
    if profile["skia_commit"] != dependencies["skia"]["commit"]:
        raise SchemaError("profile Skia commit does not match deps.lock.json")
    if not isinstance(profile["common_gn_args"], dict):
        raise SchemaError("common_gn_args must be an object")
    if is_full:
        _validate_full_profile(profile)
    targets = profile["targets"]
    if not isinstance(targets, dict) or not targets:
        raise SchemaError("targets must be a non-empty object")
    for target_name, target in targets.items():
        require_fields(target, TARGET_FIELDS, f"target {target_name}")
        if is_full:
            if target["libraries"] != "discover":
                raise SchemaError(f"full target {target_name} libraries must use discovery")
        else:
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
    if "web-wasm-webgl2" in targets:
        web_toolchain = targets["web-wasm-webgl2"]["toolchain"]
        if web_toolchain["emscripten"] != dependencies["emscripten"]["version"] or \
           web_toolchain["llvm"] != dependencies["emscripten"]["llvm_version"]:
            raise SchemaError("Web toolchain does not match deps.lock.json")
    if "windows-x64-d3d12" in targets and \
       targets["windows-x64-d3d12"]["toolchain"]["llvm"] != \
       dependencies["windows_llvm"]["version"]:
        raise SchemaError("Windows LLVM does not match deps.lock.json")
    for target_name in targets:
        if not target_name.startswith("android-"):
            continue
        toolchain = targets[target_name]["toolchain"]
        if toolchain["android_ndk"] != dependencies["android_ndk"]["version"] or \
           toolchain["api_level"] != dependencies["android_ndk"]["api_level"]:
            raise SchemaError(f"{target_name} toolchain does not match deps.lock.json")
    for field in ("build_targets", "module_header_dirs", "module_headers"):
        value = profile.get(field, [])
        if not isinstance(value, list) or any(
            not isinstance(item, str) or not item for item in value
        ) or len(value) != len(set(value)):
            raise SchemaError(f"profile {field} must be a unique string array")
        if any(
            Path(item).is_absolute() or ".." in Path(item).parts
            for item in value
        ):
            raise SchemaError(f"profile {field} contains an unsafe path")
    licenses = profile.get("licenses", {})
    if not isinstance(licenses, dict) or any(
        not isinstance(name, str) or not name or not isinstance(source, str) or not source
        for name, source in licenses.items()
    ):
        raise SchemaError("profile licenses must map output names to source paths")
    if any(
        Path(source).is_absolute() or ".." in Path(source).parts
        for source in licenses.values()
    ):
        raise SchemaError("profile licenses contain an unsafe source path")
    license_dependencies = profile.get("license_dependencies", {})
    if not isinstance(license_dependencies, dict) or any(
        not isinstance(name, str) or not name or
        not isinstance(dependency, str) or dependency not in dependencies
        for name, dependency in license_dependencies.items()
    ):
        raise SchemaError(
            "profile license_dependencies must map output names to locked dependencies"
        )
    fixture_fonts = profile.get("fixture_fonts", {})
    if not isinstance(fixture_fonts, dict) or any(
        not isinstance(destination, str) or not destination or
        not destination.startswith("resources/fonts/") or
        Path(destination).is_absolute() or ".." in Path(destination).parts or
        not isinstance(dependency, str) or dependency not in dependencies or
        "source" not in dependencies[dependency] or
        "sha256" not in dependencies[dependency]
        for destination, dependency in fixture_fonts.items()
    ):
        raise SchemaError(
            "profile fixture_fonts must map safe font destinations to locked dependencies"
        )
    runtime_files = profile.get("runtime_files", [])
    if not isinstance(runtime_files, list) or any(
        not isinstance(item, dict) or
        set(item) != {"target", "source", "destination"} or
        not all(isinstance(item[key], str) and item[key] for key in item)
        for item in runtime_files
    ):
        raise SchemaError(
            "profile runtime_files must contain target/source/destination objects"
        )
    if any(item["target"] not in targets for item in runtime_files):
        raise SchemaError("profile runtime_files contains an unknown target")
    runtime_destinations = [item["destination"] for item in runtime_files]
    if len(runtime_destinations) != len(set(runtime_destinations)) or any(
        Path(item["source"]).is_absolute() or
        Path(item["destination"]).is_absolute() or
        ".." in Path(item["source"]).parts or
        ".." in Path(item["destination"]).parts
        for item in runtime_files
    ):
        raise SchemaError("profile runtime_files paths must be unique and relative")
    return profile


def _validate_full_profile(profile: dict[str, Any]) -> None:
    expected_targets = {
        "windows-x64-d3d12", "android-arm64-v8a-gles3", "android-x86_64-gles3",
        "macos-arm64-metal", "macos-x64-metal", "ios-arm64-metal",
        "ios-simulator-arm64-metal", "web-wasm-webgl2",
    }
    if set(profile["targets"]) != expected_targets:
        raise SchemaError("r1-full-v1 must contain the exact eight-target matrix")
    variants = profile["variants"]
    if set(variants) != set(SDK_VARIANTS):
        raise SchemaError("full profile variants must be exactly release, debug, asan")
    variant_fields = {"is_official_build", "is_debug", "sanitize", "symbols", "consumer_default"}
    for name, variant in variants.items():
        if not isinstance(variant, dict):
            raise SchemaError(f"variant {name} must be an object")
        unknown = set(variant) - variant_fields - {"gn_args", "runtime_validation"}
        missing = variant_fields - set(variant)
        if unknown or missing:
            raise SchemaError(f"variant {name} fields are invalid")
        if not isinstance(variant.get("gn_args", {}), dict):
            raise SchemaError(f"variant {name} gn_args must be an object")
        if name == "release" and (not variant["is_official_build"] or variant["is_debug"] or variant["sanitize"]):
            raise SchemaError("release variant must be official, non-debug, and unsanitized")
        if name in ("debug", "asan") and (variant["is_official_build"] or not variant["is_debug"]):
            raise SchemaError(f"{name} variant must be non-official debug")
        if name == "asan" and variant["sanitize"] != "ASAN":
            raise SchemaError("asan variant must use sanitize=ASAN")
        if name != "asan" and variant["sanitize"]:
            raise SchemaError(f"{name} variant must not be sanitized")
        if not isinstance(variant["symbols"], str) or not isinstance(variant["consumer_default"], bool):
            raise SchemaError(f"variant {name} metadata is invalid")
        if variant["symbols"] not in {"none", "embedded"}:
            raise SchemaError(f"variant {name} symbols policy is invalid")
        if name == "release" and variant["symbols"] != "none":
            raise SchemaError("release variant must not request diagnostic symbols")
        if name != "release" and variant["symbols"] != "embedded":
            raise SchemaError(f"{name} variant must retain embedded symbols")
        if not isinstance(variant.get("runtime_validation"), str) or not variant["runtime_validation"]:
            raise SchemaError(f"variant {name} runtime_validation must be non-empty")
    defaults = [name for name, variant in variants.items() if variant["consumer_default"]]
    if defaults != ["release"]:
        raise SchemaError("release must be the only default Full SDK variant")
    args = profile["common_gn_args"]
    if args.get("skia_use_dng_sdk") is not False or args.get("skia_use_piex") is not False:
        raise SchemaError("r1-full-v1 must disable DNG and PIEX on every target")
    if args.get("skia_enable_skottie") is not True or args.get("skia_enable_pdf") is not True or args.get("skia_enable_svg") is not True:
        raise SchemaError("r1-full-v1 must enable Skottie, PDF, and SVG")
    runtime_disabled = {
        "skia_enable_tools": False,
        "skia_use_perfetto": False,
        "skia_use_lua": False,
        "skia_use_xps": False,
    }
    if any(args.get(name) is not expected for name, expected in runtime_disabled.items()):
        raise SchemaError("r1-full-v1 must exclude Skia tools and optional tracing/tool runtimes")
    for target_name, target in profile["targets"].items():
        target_args = target["gn_args"]
        if any(target_args.get(name) is True for name in (
            "skia_use_dng_sdk", "skia_use_piex", "skia_enable_graphite",
            "skia_use_dawn", "skia_use_vulkan",
        )):
            raise SchemaError(f"{target_name} enables a forbidden Full SDK backend or codec")
    capabilities = profile["capabilities"]
    if not isinstance(capabilities, dict) or not capabilities:
        raise SchemaError("full profile capabilities must be a non-empty object")
    for name, enabled in capabilities.items():
        if not isinstance(name, str) or not name or not isinstance(enabled, bool):
            raise SchemaError("capabilities must map names to booleans")
    if capabilities.get("raw_dng") is not False:
        raise SchemaError("r1-full-v1 raw_dng capability must be false")
    reasons = profile.get("unsupported_reasons", {})
    if not isinstance(reasons, dict) or any(
        not isinstance(name, str) or not isinstance(reason, str) or not reason
        for name, reason in reasons.items()
    ):
        raise SchemaError("unsupported_reasons must map capability names to strings")
    prefixes = profile.get("capability_library_prefixes", {})
    if not isinstance(prefixes, dict) or any(
        not isinstance(name, str) or not isinstance(values, list) or
        any(not isinstance(value, str) or not value for value in values)
        for name, values in prefixes.items()
    ):
        raise SchemaError("capability_library_prefixes must map names to string arrays")
    expected_prefix_groups = {"paragraph", "skottie", "svg", "pathops", "media"}
    if set(prefixes) != expected_prefix_groups:
        raise SchemaError(
            "capability_library_prefixes must define paragraph, skottie, svg, "
            "pathops, and media"
        )


def profile_hash(profile: dict[str, Any]) -> str:
    return canonical_sha256(profile)


def target_definition(profile: dict[str, Any], target: str) -> dict[str, Any]:
    try:
        return profile["targets"][target]
    except KeyError as error:
        raise SchemaError(f"unknown SDK target: {target}") from error


def variant_definition(profile: dict[str, Any], variant: str) -> dict[str, Any]:
    if not profile.get("schema_version") == 2:
        if variant != "release":
            raise SchemaError("legacy profiles only support the release variant")
        return {"is_official_build": True, "is_debug": False, "sanitize": ""}
    if variant not in profile["variants"]:
        raise SchemaError(f"unknown SDK variant: {variant}")
    return profile["variants"][variant]


def variant_metadata(
    profile: dict[str, Any], target: str, variant: str,
    toolchain: dict[str, Any] | None = None,
) -> dict[str, Any]:
    metadata = dict(variant_definition(profile, variant))
    metadata.pop("gn_args", None)
    if profile.get("schema_version") != 2 or variant != "asan":
        return metadata
    platform = target_definition(profile, target)["platform"]
    validation = {
        "web": "link-only",
        "ios": "instrumented-link",
        "android": "instrumented-link",
    }.get(platform, "runtime-smoke")
    metadata["runtime_validation"] = validation
    metadata["sanitizer_runtime"] = "compiler-provided"
    metadata["instrumentation"] = "address"
    toolchain = toolchain or {}
    compiler = toolchain.get("clang") or toolchain.get("emscripten") or toolchain.get("llvm")
    linker = toolchain.get("linker") or compiler
    metadata["compiler_identity"] = compiler or "unspecified"
    metadata["linker_identity"] = linker or "unspecified"
    metadata["frame_pointer"] = True
    metadata["compile_flags"] = ["-fsanitize=address", "-fno-omit-frame-pointer"]
    metadata["link_flags"] = ["-fsanitize=address"]
    metadata["requires_instrumented_consumer"] = True
    return metadata


def output_name(profile: dict[str, Any], target: str, variant: str = "release") -> str:
    base = target_definition(profile, target)["output_name"]
    return f"{base}-{variant}" if profile.get("schema_version") == 2 else base


def normalized_gn_args(profile: dict[str, Any], target: str, variant: str = "release") -> dict[str, Any]:
    result = dict(profile["common_gn_args"])
    result.update(target_definition(profile, target)["gn_args"])
    selected_variant = variant_definition(profile, variant)
    result.update(selected_variant.get("gn_args", {}))
    if profile.get("schema_version") == 2:
        result.update(
            is_official_build=selected_variant["is_official_build"],
            is_debug=selected_variant["is_debug"],
            sanitize=selected_variant["sanitize"],
        )
    if target == "windows-x64-d3d12":
        result.update(cc="clang-cl", cxx="clang-cl")
    elif target == "web-wasm-webgl2":
        result.update(cc="emcc", cxx="em++")
    elif target.startswith("android-"):
        result["ndk"] = "${ANDROID_NDK_ROOT}"
    return result


def actual_gn_args(
    profile: dict[str, Any], target: str, *, cc: str | None = None,
    cxx: str | None = None, ndk: str | None = None, variant: str = "release",
) -> dict[str, Any]:
    result = normalized_gn_args(profile, target, variant)
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


def normalized_args_text(profile: dict[str, Any], target: str, variant: str = "release") -> str:
    args = normalized_gn_args(profile, target, variant)
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
        if isinstance(value, str) and any(
            marker in value for marker in ("InstalledDir:", "InstalledDir =", str(Path.home()))
        ):
            raise SchemaError(f"toolchain {key} contains an installation path")


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
    *, profile_path: Path = DEFAULT_PROFILE, variant: str = "release",
) -> dict[str, Any]:
    validate_toolchain(profile, target, toolchain)
    definition = target_definition(profile, target)
    identity = {
        "profile": profile["profile"],
        "profile_hash": profile_hash(profile),
        "skia_commit": profile["skia_commit"],
        "target": target,
        "platform": definition["platform"],
        "arch": definition["arch"],
        "backend": definition["backend"],
        "gn_args": normalized_gn_args(profile, target, variant),
        "toolchain": toolchain,
        "recipe_hash": recipe_hash(profile_path),
    }
    if profile.get("schema_version") == 2:
        identity["variant"] = variant
        identity["variant_metadata"] = variant_metadata(
            profile, target, variant, toolchain,
        )
        identity["capabilities"] = profile["capabilities"]
    return identity


def validate_manifest(
    manifest: Any, profile: dict[str, Any] | None = None,
    expected_target: str | None = None, profile_path: Path = DEFAULT_PROFILE,
) -> dict[str, Any]:
    full = manifest.get("schema_version") == 2 or manifest.get("format") == FULL_SDK_FORMAT
    result = require_fields(manifest, FULL_MANIFEST_FIELDS if full else MANIFEST_FIELDS, "manifest")
    if full and (result["schema_version"] != 2 or result["format"] != FULL_SDK_FORMAT):
        raise SchemaError("unsupported full SDK manifest schema or format")
    if not full and (result["schema_version"] != 1 or result["format"] != SDK_FORMAT):
        raise SchemaError("unsupported SDK manifest schema or format")
    identity = require_fields(result["identity"], FULL_IDENTITY_FIELDS if full else IDENTITY_FIELDS, "manifest identity")
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
            variant=identity.get("variant", "release"),
        )
        if identity != expected_identity:
            raise SchemaError("manifest identity does not match the selected profile")
    if full:
        if not isinstance(result["capabilities"], dict):
            raise SchemaError("manifest capabilities must be an object")
        if profile is not None and result["capabilities"] != profile["capabilities"]:
            raise SchemaError("manifest capabilities do not match profile")
        if not isinstance(result["unsupported_reasons"], dict):
            raise SchemaError("manifest unsupported_reasons must be an object")
        if profile is not None and result["unsupported_reasons"] != profile.get(
            "unsupported_reasons", {}
        ):
            raise SchemaError("manifest unsupported_reasons do not match profile")
        closure = result["archive_closure"]
        if not isinstance(closure, list) or not closure:
            raise SchemaError("full manifest archive_closure must be non-empty")
        packaged_paths: list[str] = []
        source_paths: list[str] = []
        for index, archive in enumerate(closure):
            require_fields(
                archive, ARCHIVE_CLOSURE_FIELDS,
                f"manifest archive_closure {index}",
            )
            for field in ("source_path", "packaged_path"):
                path = archive[field]
                if not isinstance(path, str) or not path or Path(path).is_absolute() or \
                   ".." in Path(path).parts:
                    raise SchemaError(
                        f"manifest archive_closure {index} has unsafe {field}"
                    )
            if not archive["packaged_path"].startswith("lib/"):
                raise SchemaError(
                    f"manifest archive_closure {index} packaged_path must be under lib/"
                )
            if not isinstance(archive["size"], int) or archive["size"] <= 0:
                raise SchemaError(
                    f"manifest archive_closure {index} has invalid size"
                )
            digest = archive["sha256"]
            if not isinstance(digest, str) or len(digest) != 64 or \
               digest != digest.lower() or any(
                   character not in "0123456789abcdef" for character in digest
               ):
                raise SchemaError(
                    f"manifest archive_closure {index} has invalid SHA-256"
                )
            source_paths.append(archive["source_path"])
            packaged_paths.append(archive["packaged_path"])
        if len(source_paths) != len(set(source_paths)) or \
           len(packaged_paths) != len(set(packaged_paths)):
            raise SchemaError("manifest archive_closure paths must be unique")
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
    if full:
        file_by_path = {entry["path"]: entry for entry in result["files"]}
        for archive in result["archive_closure"]:
            file_entry = file_by_path.get(archive["packaged_path"])
            if file_entry is None or file_entry["role"] != "library" or any(
                file_entry[field] != archive[field] for field in ("sha256", "size")
            ):
                raise SchemaError(
                    "manifest archive_closure does not match the packaged library files"
                )
    return result


def validate_symbols_manifest(
    value: Any, *, expected_sdk_id: str | None = None,
    expected_target: str | None = None, expected_variant: str | None = None,
) -> dict[str, Any]:
    result = require_fields(value, SYMBOLS_MANIFEST_FIELDS, "symbols manifest")
    if result["schema_version"] != 1 or \
       result["format"] != "canvas-skia-symbols-v1":
        raise SchemaError("unsupported symbols manifest schema or format")
    if result["variant"] not in {"debug", "asan"}:
        raise SchemaError("symbols manifest variant must be debug or asan")
    if result["embedded_symbols"] is not True:
        raise SchemaError("symbols manifest must record embedded static-library symbols")
    if expected_sdk_id is not None and result["sdk_id"] != expected_sdk_id:
        raise SchemaError("symbols manifest SDK ID mismatch")
    if expected_target is not None and result["target"] != expected_target:
        raise SchemaError("symbols manifest target mismatch")
    if expected_variant is not None and result["variant"] != expected_variant:
        raise SchemaError("symbols manifest variant mismatch")
    if not isinstance(result["files"], list):
        raise SchemaError("symbols manifest files must be an array")
    paths: list[str] = []
    for index, entry in enumerate(result["files"]):
        require_fields(entry, SYMBOL_FILE_FIELDS, f"symbols file {index}")
        path = entry["path"]
        if not isinstance(path, str) or not path.startswith("symbols/") or \
           Path(path).is_absolute() or ".." in Path(path).parts:
            raise SchemaError(f"symbols file {index} path is unsafe")
        if not isinstance(entry["size"], int) or entry["size"] <= 0:
            raise SchemaError(f"symbols file {index} size is invalid")
        digest = entry["sha256"]
        if not isinstance(digest, str) or len(digest) != 64 or \
           digest != digest.lower() or any(
               character not in "0123456789abcdef" for character in digest
           ):
            raise SchemaError(f"symbols file {index} SHA-256 is invalid")
        paths.append(path)
    if paths != sorted(paths) or len(paths) != len(set(paths)):
        raise SchemaError("symbols manifest file paths must be sorted and unique")
    return result
