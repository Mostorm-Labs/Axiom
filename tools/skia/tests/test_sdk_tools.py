from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
from unittest import mock


SKIA_TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SKIA_TOOLS))

from sdk import (  # noqa: E402
    DEFAULT_PROFILE, SDK_FORMAT, SchemaError, canonical_bytes,
    canonical_sha256, load_profile, make_identity, normalized_recipe_bytes,
    validate_manifest, validate_toolchain,
)
from package import cmake_config, copy_file, download_locked_file, role  # noqa: E402
from verify import archive_architectures, verify_archive  # noqa: E402


class SdkMetadataTest(unittest.TestCase):
    def setUp(self) -> None:
        self.profile = load_profile()

    def manifest(self, target: str = "macos-arm64-metal") -> dict:
        toolchain = {"sdk": "macosx"}
        identity = make_identity(self.profile, target, toolchain)
        return {
            "schema_version": 1,
            "format": SDK_FORMAT,
            "sdk_id": canonical_sha256(identity),
            "identity": identity,
            "files": [{
                "path": "args.gn",
                "sha256": hashlib.sha256(b"bad").hexdigest(),
                "size": 3,
                "role": "build-arguments",
            }],
        }

    def test_profile_contains_exact_target_set(self) -> None:
        self.assertEqual(len(self.profile["targets"]), 7)
        self.assertEqual(self.profile.get("build_targets", ["skia"]), ["skia"])

    def test_rf01_profile_declares_sksg_target_headers_and_libraries(self) -> None:
        profile_path = DEFAULT_PROFILE.parent / "rf01-sksg-v1.json"
        profile = load_profile(profile_path)
        self.assertEqual(profile["build_targets"], ["skia", "sksg"])
        sksg_headers = {
            header for header in profile["module_headers"]
            if header.startswith("modules/sksg/include/")
        }
        self.assertEqual(len(sksg_headers), 23)
        self.assertIn("modules/sksg/include/SkSGGroup.h", sksg_headers)
        self.assertIn("modules/sksg/include/SkSGRect.h", sksg_headers)
        self.assertIn("modules/sksg/include/SkSGScene.h", sksg_headers)
        for target in profile["targets"].values():
            expected = (
                "sksg.lib" if target["platform"] == "windows"
                else "libsksg.wasm.a" if target["platform"] == "web"
                else "libsksg.a"
            )
            self.assertIn(expected, target["libraries"])

    def test_canonical_hash_is_order_independent_and_rejects_nan(self) -> None:
        self.assertEqual(canonical_sha256({"b": 2, "a": 1}), canonical_sha256({"a": 1, "b": 2}))
        with self.assertRaises(ValueError):
            canonical_bytes({"bad": float("nan")})

    def test_recipe_hash_input_is_checkout_newline_independent(self) -> None:
        self.assertEqual(
            normalized_recipe_bytes(b"first\r\nsecond\rthird\n"),
            b"first\nsecond\nthird\n",
        )

    def test_unknown_profile_field_is_rejected(self) -> None:
        invalid = copy.deepcopy(self.profile)
        invalid["surprise"] = True
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "poc01-minimal-v1.json"
            path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaisesRegex(SchemaError, "unknown fields"):
                load_profile(path)

    def test_unsafe_or_unlocked_fixture_font_is_rejected(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/poc04-richtext-v1.json"
        profile = load_profile(profile_path)
        for destination, dependency in (
            ("../escape.ttf", "noto_sans_cjk_subset"),
            ("resources/fonts/test.ttf", "missing_dependency"),
        ):
            invalid = copy.deepcopy(profile)
            invalid["fixture_fonts"] = {destination: dependency}
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "poc04-richtext-v1.json"
                path.write_text(json.dumps(invalid), encoding="utf-8")
                with self.assertRaisesRegex(SchemaError, "fixture_fonts"):
                    load_profile(path)

    def test_manifest_sdk_id_and_unknown_fields_are_rejected(self) -> None:
        manifest = self.manifest()
        validate_manifest(manifest, expected_target="macos-arm64-metal")
        invalid_id = copy.deepcopy(manifest)
        invalid_id["sdk_id"] = "0" * 64
        with self.assertRaisesRegex(SchemaError, "sdk_id"):
            validate_manifest(invalid_id)
        unknown = copy.deepcopy(manifest)
        unknown["unexpected"] = 1
        with self.assertRaisesRegex(SchemaError, "unknown fields"):
            validate_manifest(unknown)

    def test_locked_manifest_does_not_depend_on_current_recipe(self) -> None:
        historical = self.manifest()
        historical["identity"]["recipe_hash"] = "1" * 64
        historical["sdk_id"] = canonical_sha256(historical["identity"])
        validate_manifest(historical, expected_target="macos-arm64-metal")
        with self.assertRaisesRegex(SchemaError, "selected profile"):
            validate_manifest(
                historical, self.profile, "macos-arm64-metal", DEFAULT_PROFILE,
            )

    def test_target_and_toolchain_identity_are_enforced(self) -> None:
        with self.assertRaisesRegex(SchemaError, "target mismatch"):
            validate_manifest(self.manifest(), expected_target="ios-arm64-metal")
        with self.assertRaisesRegex(SchemaError, "toolchain identity mismatch"):
            validate_toolchain(
                self.profile, "web-wasm-webgl2",
                {"emscripten": "0", "llvm": "22.1.8", "pthread": False},
            )

    def test_corrupt_zip_and_path_traversal_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            corrupt = root / "corrupt.zip"
            corrupt.write_bytes(b"not a zip")
            with self.assertRaises(zipfile.BadZipFile):
                verify_archive(corrupt, DEFAULT_PROFILE, "macos-arm64-metal")
            traversal = root / "traversal.zip"
            with zipfile.ZipFile(traversal, "w") as archive:
                archive.writestr("../escape", b"bad")
            with self.assertRaisesRegex(RuntimeError, "unsafe ZIP path"):
                verify_archive(traversal, DEFAULT_PROFILE, "macos-arm64-metal")

    def test_missing_package_input_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(RuntimeError, "required SDK input is missing"):
                copy_file(root / "missing.a", root / "sdk/lib/missing.a")

    def test_locked_license_checksum_is_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as temporary, \
             mock.patch("urllib.request.urlopen") as urlopen:
            urlopen.return_value.__enter__.return_value.read.return_value = b"license"
            destination = Path(temporary) / "license.txt"
            with self.assertRaisesRegex(RuntimeError, "checksum"):
                download_locked_file("https://example.invalid/license", "0" * 64, destination)
            self.assertFalse(destination.exists())

    def test_cmake_target_owns_platform_link_dependencies(self) -> None:
        android = cmake_config(["libskia.a"], "android")
        self.assertIn(
            'INTERFACE_LINK_LIBRARIES "CanvasSkia::Archive0;android;EGL;GLESv2;log;dl"',
            android,
        )
        apple = cmake_config(["libskia.a"], "ios")
        self.assertIn("find_library(_CANVAS_SKIA_CORETEXT CoreText REQUIRED)", apple)
        windows = cmake_config(["skia.lib"], "windows")
        self.assertIn("d3d12;dxgi;d3dcompiler;ole32", windows)
        self.assertNotIn("CanvasSkia_CJK_FONT_PATH", windows)
        self.assertNotIn("CanvasSkia_ICU_DATA_PATH", windows)

    def test_checksum_drift_is_rejected_before_extraction(self) -> None:
        manifest = self.manifest()
        with tempfile.TemporaryDirectory() as temporary:
            archive_path = Path(temporary) / "drift.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr("manifest.json", json.dumps(manifest))
                archive.writestr("args.gn", b"bad")
            manifest["files"][0]["sha256"] = "0" * 64
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr("manifest.json", json.dumps(manifest))
                archive.writestr("args.gn", b"bad")
            with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                verify_archive(archive_path, DEFAULT_PROFILE, "macos-arm64-metal")

    def test_archive_architecture_detection(self) -> None:
        wasm = b"\x00asm" + b"\0" * 8
        coff_bigobj = b"\x00\x00\xff\xff\x02\x00\x64\x86" + b"\0" * 8

        def member(name: bytes, body: bytes) -> bytes:
            header = (
                name.ljust(16) + b"0           " + b"0     " + b"0     "
                + b"100644  " + f"{len(body):<10}".encode("ascii") + b"`\n"
            )
            self.assertEqual(len(header), 60)
            return header + body + (b"\n" if len(body) & 1 else b"")

        archive = b"!<arch>\n" + member(b"wasm.o/", wasm) + member(b"coff.obj/", coff_bigobj)
        self.assertEqual(archive_architectures(archive), {"wasm32", "x64"})

    def test_richtext_profile_freezes_four_targets_modules_fonts_and_runtime(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/poc04-richtext-v1.json"
        profile = load_profile(profile_path)
        self.assertEqual(set(profile["targets"]), {
            "windows-x64-d3d12", "web-wasm-webgl2",
            "android-arm64-v8a-gles3", "android-x86_64-gles3",
        })
        self.assertEqual(profile["build_targets"], [
            "skia", "modules/skparagraph:skparagraph",
            "modules/skshaper:skshaper", "modules/skunicode:skunicode",
        ])
        self.assertEqual(
            profile["licenses"],
            {
                "HarfBuzz.txt": "third_party/externals/harfbuzz/COPYING",
                "ICU.txt": "third_party/externals/icu/LICENSE",
            },
        )
        self.assertEqual(profile["fixture_fonts"], {
            "resources/fonts/NotoSansCJK-VF-subset.otf.ttc":
                "noto_sans_cjk_subset",
        })
        self.assertIn("src/core/SkUTF.h", profile["module_headers"])
        self.assertEqual(profile["runtime_files"], [{
            "target": "windows-x64-d3d12",
            "source": "icudtl.dat",
            "destination": "runtime/windows/icudtl.dat",
        }])
        for target in profile["targets"].values():
            self.assertNotIn("libharfbuzz.a", target["libraries"])
            self.assertNotIn("libicu.a", target["libraries"])
            self.assertNotIn("harfbuzz.lib", target["libraries"])
            self.assertNotIn("icu.lib", target["libraries"])
        windows_config = cmake_config(
            profile["targets"]["windows-x64-d3d12"]["libraries"], "windows",
            cjk_font=True, icu_data=True,
        )
        self.assertIn("CanvasSkia_CJK_FONT_PATH", windows_config)
        self.assertIn("CanvasSkia_ICU_DATA_PATH", windows_config)
        self.assertLess(
            windows_config.index("CanvasSkia_ICU_DATA_PATH"),
            windows_config.index("unset(_CANVAS_SKIA_PREFIX)"),
        )

    def test_richtext_v2_adds_exact_apple_sdk_targets(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/poc04-richtext-v2.json"
        profile = load_profile(profile_path)
        self.assertEqual(set(profile["targets"]), {
            "windows-x64-d3d12", "web-wasm-webgl2",
            "macos-arm64-metal", "ios-arm64-metal",
            "ios-simulator-arm64-metal", "android-arm64-v8a-gles3",
            "android-x86_64-gles3",
        })
        self.assertEqual(
            profile["targets"]["ios-arm64-metal"]["toolchain"],
            {"deployment_target": "17.0", "sdk": "iphoneos"},
        )
        self.assertEqual(
            profile["targets"]["ios-simulator-arm64-metal"]["toolchain"],
            {"deployment_target": "17.0", "sdk": "iphonesimulator"},
        )
        for target_name in (
            "macos-arm64-metal", "ios-arm64-metal",
            "ios-simulator-arm64-metal",
        ):
            target = profile["targets"][target_name]
            self.assertEqual(target["backend"], "metal")
            self.assertIn("libskparagraph.a", target["libraries"])
            self.assertIn("libskunicode_icu.a", target["libraries"])
            self.assertTrue(target["gn_args"]["skia_use_metal"])

    def test_private_sdk_header_has_header_role(self) -> None:
        self.assertEqual(role("src/core/SkUTF.h"), "header")


if __name__ == "__main__":
    unittest.main()
