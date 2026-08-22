from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
import zipfile
from unittest import mock


SKIA_TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SKIA_TOOLS))

from sdk import (  # noqa: E402
    DEFAULT_PROFILE, ROOT, SDK_FORMAT, SchemaError, canonical_bytes,
    actual_gn_args, canonical_sha256, load_profile, make_identity,
    normalized_recipe_bytes, normalized_gn_args, validate_manifest,
    validate_symbols_manifest, variant_metadata,
    validate_toolchain,
)
from package import (  # noqa: E402
    cmake_config, copy_file, download_locked_file, role,
    validate_header_closure,
)
from build import gn_archive_closure, gn_label  # noqa: E402
from verify import archive_architectures, verify_archive  # noqa: E402
from smoke_consumer import (  # noqa: E402
    built_probe, canvas_variant_cmake_args, cmake_apple_arch,
    windows_variant_cmake_args,
)
from reuse_artifact import trusted_run  # noqa: E402
from classify_r1_changes import ALL_TARGETS, build_matrix, classify  # noqa: E402


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

    def test_unsafe_license_source_is_rejected(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/r1-full-v1.json"
        profile = load_profile(profile_path)
        profile["licenses"]["bad.txt"] = "../../outside/LICENSE"
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "r1-full-v1.json"
            path.write_text(json.dumps(profile), encoding="utf-8")
            with self.assertRaisesRegex(SchemaError, "unsafe source path"):
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

    def test_header_closure_rejects_unpacked_private_dependencies(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            sdk = root / "sdk"
            (source / "modules/example/include").mkdir(parents=True)
            (source / "src/example").mkdir(parents=True)
            (sdk / "modules/example/include").mkdir(parents=True)
            public = '#include "src/example/Private.h"\n'
            private = '#include "include/core/SkTypes.h"\n'
            (source / "modules/example/include/Public.h").write_text(
                public, encoding="utf-8"
            )
            (source / "src/example/Private.h").write_text(
                private, encoding="utf-8"
            )
            (sdk / "modules/example/include/Public.h").write_text(
                public, encoding="utf-8"
            )
            with self.assertRaisesRegex(
                RuntimeError, "src/example/Private.h"
            ):
                validate_header_closure(source, sdk)
            copy_file(
                source / "src/example/Private.h",
                sdk / "src/example/Private.h",
            )
            validate_header_closure(source, sdk)

    def test_r1_full_profile_freezes_matrix_variants_and_capabilities(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/r1-full-v1.json"
        profile = load_profile(profile_path)
        self.assertEqual(profile["format"], "canvas-skia-sdk-profile-v2")
        self.assertEqual(len(profile["targets"]), 8)
        self.assertEqual(set(profile["variants"]), {"release", "debug", "asan"})
        self.assertTrue(profile["capabilities"]["skottie"])
        self.assertTrue(profile["capabilities"]["pathops"])
        self.assertFalse(profile["capabilities"]["raw_dng"])
        self.assertFalse(profile["common_gn_args"]["skia_use_dng_sdk"])
        self.assertFalse(profile["common_gn_args"]["skia_use_piex"])
        self.assertFalse(profile["common_gn_args"]["skia_enable_tools"])
        self.assertEqual(
            set(profile["module_headers"]),
            {
                "modules/skcms/skcms.h",
                "modules/skcms/src/skcms_public.h",
                "modules/skottie/src/SkottieValue.h",
                "modules/skottie/src/animator/Animator.h",
                "modules/skottie/src/text/Font.h",
                "modules/skottie/src/text/TextAdapter.h",
                "modules/skottie/src/text/TextAnimator.h",
                "modules/skottie/src/text/TextValue.h",
                "src/core/SkChecksum.h",
                "src/core/SkMathPriv.h",
                "src/core/SkTHash.h",
                "src/core/SkTLazy.h",
                "src/core/SkUTF.h",
            },
        )
        for argument in (
            "skia_use_system_expat",
            "skia_use_system_freetype2",
            "skia_use_system_harfbuzz",
            "skia_use_system_icu",
            "skia_use_system_libjpeg_turbo",
            "skia_use_system_libpng",
            "skia_use_system_libwebp",
            "skia_use_system_zlib",
        ):
            self.assertFalse(profile["common_gn_args"][argument])
        self.assertNotIn("symbol_level", profile["variants"]["release"]["gn_args"])
        self.assertNotIn("symbol_level", profile["variants"]["debug"]["gn_args"])
        self.assertEqual(profile["targets"]["macos-x64-metal"]["arch"], "x64")
        for target in profile["targets"].values():
            self.assertEqual(target["libraries"], "discover")

        release = normalized_gn_args(profile, "web-wasm-webgl2", "release")
        debug = normalized_gn_args(profile, "web-wasm-webgl2", "debug")
        asan = normalized_gn_args(profile, "web-wasm-webgl2", "asan")
        self.assertEqual((release["is_official_build"], release["is_debug"]), (True, False))
        self.assertEqual((debug["is_official_build"], debug["is_debug"]), (False, True))
        self.assertEqual(asan["sanitize"], "ASAN")

    def test_r1_full_profile_rejects_dng_piex_and_illegal_variant(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/r1-full-v1.json"
        profile = load_profile(profile_path)
        for argument in ("skia_use_dng_sdk", "skia_use_piex"):
            invalid = copy.deepcopy(profile)
            invalid["common_gn_args"][argument] = True
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "r1-full-v1.json"
                path.write_text(json.dumps(invalid), encoding="utf-8")
                with self.assertRaisesRegex(SchemaError, "DNG and PIEX"):
                    load_profile(path)
        invalid = copy.deepcopy(profile)
        invalid["variants"]["debug"]["is_official_build"] = True
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "r1-full-v1.json"
            path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaisesRegex(SchemaError, "non-official debug"):
                load_profile(path)

    def test_full_cmake_config_exports_capability_targets(self) -> None:
        config = cmake_config(["libskia.a"], "macos", full=True)
        for target in ("Paragraph", "Skottie", "Svg", "PathOps", "Media"):
            self.assertIn(f"CanvasSkia::{target}", config)
        self.assertIn("SK_ENABLE_SKOTTIE", config)

    def test_windows_full_cmake_config_exports_stl_debug_contract(self) -> None:
        config = cmake_config(["skia.lib"], "windows", full=True, variant="debug")
        self.assertIn("_ITERATOR_DEBUG_LEVEL=0", config)
        self.assertIn("_HAS_ITERATOR_DEBUGGING=0", config)
        asan = cmake_config(["skia.lib"], "windows", full=True, variant="asan")
        self.assertIn("CanvasSkia_ASAN_RUNTIME_PATH", asan)
        self.assertIn("clang_rt.asan_dynamic-x86_64.lib", asan)
        self.assertIn(
            "/WHOLEARCHIVE:${_CANVAS_SKIA_PREFIX}/runtime/windows/"
            "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib",
            asan,
        )
        self.assertNotIn('INTERFACE_LINK_OPTIONS "/fsanitize=address', asan)

    def test_windows_debug_consumer_matches_static_runtime_and_stl_contract(self) -> None:
        args = windows_variant_cmake_args("debug")
        self.assertIn("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded", args)
        self.assertFalse(any("CMAKE_CXX_FLAGS" in value for value in args))
        probe = (SKIA_TOOLS / "cmake_probe/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn('CanvasSkia_VARIANT STREQUAL "debug"', probe)
        root = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("DEFINED CANVAS_SKIA_SDK_VARIANT", root)
        self.assertIn(
            'CANVAS_SKIA_SDK_VARIANT MATCHES "^(release|debug|asan)$"', root,
        )
        self.assertIn("add_compile_definitions(", root)
        self.assertIn("_ITERATOR_DEBUG_LEVEL=0", root)

    def test_windows_asan_gn_args_require_explicit_llvm_root(self) -> None:
        profile = load_profile(SKIA_TOOLS / "profiles/r1-full-v1.json")
        with self.assertRaisesRegex(RuntimeError, "--clang-win"):
            actual_gn_args(
                profile, "windows-x64-d3d12", cc="clang-cl", cxx="clang-cl",
                variant="asan",
            )
        with tempfile.TemporaryDirectory() as temporary:
            args = actual_gn_args(
                profile, "windows-x64-d3d12", cc="clang-cl", cxx="clang-cl",
                clang_win=temporary, variant="asan",
            )
            self.assertEqual(args["clang_win"], str(Path(temporary).resolve()))

    def test_windows_asan_metadata_describes_bundled_dynamic_runtime(self) -> None:
        profile = load_profile(SKIA_TOOLS / "profiles/r1-full-v1.json")
        metadata = variant_metadata(
            profile, "windows-x64-d3d12", "asan", {"llvm": "22.1.8"},
        )
        self.assertEqual(metadata["sanitizer_runtime"], "sdk-bundled-clang-dynamic")
        self.assertIn("/fsanitize=address", metadata["compile_flags"])

    def test_windows_asan_manifest_requires_bundled_dynamic_runtime(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/r1-full-v1.json"
        profile = load_profile(profile_path)
        identity = make_identity(
            profile, "windows-x64-d3d12", {"llvm": "22.1.8"},
            profile_path=profile_path, variant="asan",
        )
        payload = b"archive"
        manifest = {
            "schema_version": 2,
            "format": "canvas-skia-sdk-v2",
            "sdk_id": canonical_sha256(identity),
            "identity": identity,
            "capabilities": profile["capabilities"],
            "unsupported_reasons": profile["unsupported_reasons"],
            "archive_closure": [{
                "source_path": "obj/skia.lib",
                "packaged_path": "lib/obj__skia.lib",
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }],
            "files": [{
                "path": "args.gn", "sha256": hashlib.sha256(b"args").hexdigest(),
                "size": 4, "role": "build-arguments",
            }, {
                "path": "lib/obj__skia.lib",
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload), "role": "library",
            }],
        }
        with self.assertRaisesRegex(SchemaError, "dynamic runtime"):
            validate_manifest(
                manifest, profile, "windows-x64-d3d12", profile_path,
            )
        manifest["files"].append({
            "path": "runtime/windows/clang_rt.asan_dynamic-x86_64.dll",
            "sha256": hashlib.sha256(b"runtime").hexdigest(),
            "size": len(b"runtime"), "role": "runtime",
        })
        for name in (
            "clang_rt.asan_dynamic-x86_64.lib",
            "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib",
        ):
            manifest["files"].append({
                "path": f"runtime/windows/{name}",
                "sha256": hashlib.sha256(b"runtime").hexdigest(),
                "size": len(b"runtime"), "role": "runtime",
            })
        manifest["files"].sort(key=lambda entry: entry["path"])
        validate_manifest(
            manifest, profile, "windows-x64-d3d12", profile_path,
        )

    def test_ios_simulator_probe_uses_bundle_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle = root / "Debug-iphonesimulator/canvas_skia_sdk_probe.app"
            bundle.mkdir(parents=True)
            (bundle / "Info.plist").write_bytes(plistlib.dumps({
                "CFBundleExecutable": "canvas_skia_sdk_probe",
            }))
            executable = bundle / "canvas_skia_sdk_probe"
            executable.write_bytes(b"probe")
            (root / "CMakeFiles/canvas_skia_sdk_probe").mkdir(parents=True)
            (root / "CMakeFiles/canvas_skia_sdk_probe/Info.plist").write_bytes(b"noise")
            self.assertEqual(built_probe(root, "ios-simulator"), executable)
        cmake_probe = (SKIA_TOOLS / "cmake_probe/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("MACOSX_BUNDLE_GUI_IDENTIFIER", cmake_probe)
        self.assertIn("XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER", cmake_probe)

    def test_pathops_probe_uses_current_skpath_factory_api(self) -> None:
        probe = (SKIA_TOOLS / "cmake_probe/probe_pathops.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("SkPath::Rect", probe)
        self.assertNotIn("left.addRect", probe)

    def test_apple_cmake_arch_translates_gn_x64(self) -> None:
        self.assertEqual(cmake_apple_arch("x64"), "x86_64")
        self.assertEqual(cmake_apple_arch("arm64"), "arm64")

    def test_full_asan_consumer_links_the_asan_runtime_explicitly(self) -> None:
        self.assertEqual(
            canvas_variant_cmake_args("asan"),
            [
                "-DCANVAS_SKIA_SDK_ASAN_CONSUMER=ON",
                "-DCMAKE_PROJECT_INCLUDE="
                f"{SKIA_TOOLS / 'cmake/asan_consumer.cmake'}",
            ],
        )
        self.assertEqual(canvas_variant_cmake_args("release"), [])
        injector = (SKIA_TOOLS / "cmake/asan_consumer.cmake").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("set(CANVAS_POC01_ENABLE_SANITIZERS ON", injector)
        self.assertNotIn("add_link_options(/fsanitize=address", injector)
        self.assertIn("add_link_options(/INCREMENTAL:NO)", injector)
        self.assertIn("add_link_options(-fsanitize=address)", injector)

    def test_only_r1_full_producer_tracks_shared_skia_tooling(self) -> None:
        workflows = SKIA_TOOLS.parents[1] / ".github/workflows"
        legacy_producers = (
            workflows / "skia-sdk-producer.yml",
            workflows / "skia-sdk-poc04-producer.yml",
        )
        for workflow in legacy_producers:
            trigger = workflow.read_text(encoding="utf-8").split(
                "permissions:", maxsplit=1
            )[0]
            self.assertNotIn("pull_request:", trigger)
            self.assertIn("workflow_dispatch:", trigger)
        full_trigger = (workflows / "r1-full-producer-contract.yml").read_text(
            encoding="utf-8"
        ).split("permissions:", maxsplit=1)[0]
        self.assertIn("pull_request:", full_trigger)
        self.assertIn('"tools/skia/**"', full_trigger)
        self.assertIn("r1-full-consumer-validation.yml", full_trigger)
        for consumer in ("poc01.yml", "poc03.yml", "poc04.yml"):
            trigger = (workflows / consumer).read_text(encoding="utf-8").split(
                "concurrency:", maxsplit=1
            )[0]
            self.assertNotIn('"tools/skia/**"', trigger)
            self.assertNotIn('"tools/**"', trigger)

    def test_full_producer_is_target_variant_matrix(self) -> None:
        workflow = (
            SKIA_TOOLS.parents[1]
            / ".github/workflows/skia-sdk-r1-full-producer.yml"
        ).read_text(encoding="utf-8")
        self.assertIn("matrix: ${{ fromJSON(needs.preflight.outputs.matrix) }}", workflow)
        self.assertIn("name: ${{ matrix.target }}-${{ matrix.variant }}", workflow)
        # The producer must expose one matrix job per target/variant.  Do not
        # assert against prose that happens to mention the old serial-loop
        # shape; the matrix contract above is the behavior we need to freeze.
        self.assertNotIn("foreach ($variant in @('release', 'debug', 'asan'))", workflow)
        self.assertIn("tools/skia/reuse_artifact.py", workflow)

    def test_r1_change_classifier_scopes_expensive_jobs(self) -> None:
        full = classify(["tools/skia/build.py"])
        self.assertEqual(full["mode"], "full")
        self.assertEqual(len(full["targets"]), 8)
        consumer = classify(["tools/skia/fetch.py", "tools/skia/tests/test_consumer.py"])
        self.assertEqual(consumer, {
            "mode": "consumer", "targets": [],
            "reason": "consumer or schema validation",
        })
        platform = classify(["tools/skia/platform/windows/adapter.py"])
        self.assertEqual(platform["mode"], "platform")
        self.assertEqual(platform["targets"], ["windows-x64-d3d12"])
        self.assertEqual(classify(["docs/design.md"])["mode"], "none")

    def test_r1_change_classifier_recognizes_workflow_consumer_changes(self) -> None:
        consumer = classify([
            ".github/workflows/r1-full-consumer-validation.yml",
            ".github/workflows/r1-full-producer-contract.yml",
        ])
        self.assertEqual(consumer["mode"], "consumer")
        self.assertEqual(consumer["targets"], [])

    def test_r1_target_matrix_expands_only_selected_platform_variants(self) -> None:
        windows = build_matrix(["windows-x64-d3d12"])["include"]
        self.assertEqual(len(windows), 3)
        self.assertEqual({item["variant"] for item in windows}, {
            "release", "debug", "asan",
        })
        self.assertTrue(all(item["family"] == "windows" for item in windows))
        self.assertEqual(len(build_matrix(ALL_TARGETS)["include"]), 24)
        with self.assertRaisesRegex(ValueError, "unknown R1 Skia target"):
            build_matrix(["unknown-target"])

    def test_cross_run_reuse_accepts_only_trusted_completed_runs(self) -> None:
        base = {
            "id": 41, "status": "completed", "head_sha": "a" * 40,
            "head_branch": "feature",
        }
        trusted = {"feature", "main"}
        self.assertTrue(trusted_run(base, "42", "a" * 40, trusted))
        from_main = dict(base, head_sha="b" * 40, head_branch="main")
        self.assertTrue(trusted_run(from_main, "42", "a" * 40, trusted))
        self.assertFalse(trusted_run(base, "41", "a" * 40, trusted))
        self.assertFalse(trusted_run(
            dict(base, status="in_progress"), "42", "a" * 40, trusted
        ))
        self.assertFalse(trusted_run(
            dict(base, head_sha="b" * 40, head_branch="untrusted"),
            "42", "a" * 40, trusted
        ))

    def test_legacy_workflows_do_not_track_full_sdk_changes(self) -> None:
        workflows = SKIA_TOOLS.parents[1] / ".github/workflows"
        for name in (
            "poc01.yml", "poc02.yml", "poc03.yml", "poc04.yml",
            "poc05.yml", "rf01.yml", "arc.yml",
        ):
            trigger = (workflows / name).read_text(encoding="utf-8").split(
                "concurrency:", maxsplit=1
            )[0]
            self.assertNotIn('"CMakeLists.txt"', trigger)
            self.assertNotIn('"tools/skia/**"', trigger)
            self.assertNotIn(f'".github/workflows/{name}"', trigger)

    def test_r1_full_profile_requires_bundled_dependencies(self) -> None:
        profile = load_profile(SKIA_TOOLS / "profiles/r1-full-v1.json")
        for argument in (
            "skia_use_system_expat",
            "skia_use_system_libjpeg_turbo",
            "skia_use_system_libwebp",
        ):
            invalid = copy.deepcopy(profile)
            invalid["common_gn_args"][argument] = True
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "r1-full-v1.json"
                path.write_text(json.dumps(invalid), encoding="utf-8")
                with self.assertRaisesRegex(SchemaError, "bundle all declared"):
                    load_profile(path)

    def test_full_manifest_requires_archive_closure_and_symbols_schema(self) -> None:
        profile_path = SKIA_TOOLS / "profiles/r1-full-v1.json"
        profile = load_profile(profile_path)
        identity = make_identity(
            profile, "macos-arm64-metal", {"sdk": "macosx"},
            profile_path=profile_path, variant="debug",
        )
        payload = b"archive"
        manifest = {
            "schema_version": 2,
            "format": "canvas-skia-sdk-v2",
            "sdk_id": canonical_sha256(identity),
            "identity": identity,
            "capabilities": profile["capabilities"],
            "unsupported_reasons": profile["unsupported_reasons"],
            "archive_closure": [{
                "source_path": "obj/libskia.a",
                "packaged_path": "lib/obj__libskia.a",
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }],
            "files": [{
                "path": "args.gn", "sha256": hashlib.sha256(b"args").hexdigest(),
                "size": 4, "role": "build-arguments",
            }, {
                "path": "lib/obj__libskia.a",
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload), "role": "library",
            }],
        }
        validate_manifest(manifest, profile, "macos-arm64-metal", profile_path)
        invalid = copy.deepcopy(manifest)
        invalid["archive_closure"][0]["packaged_path"] = "../escape"
        with self.assertRaisesRegex(SchemaError, "unsafe packaged_path"):
            validate_manifest(invalid, profile, "macos-arm64-metal", profile_path)
        symbols = {
            "schema_version": 1,
            "format": "canvas-skia-symbols-v1",
            "sdk_id": "a" * 64,
            "target": "macos-arm64-metal",
            "variant": "asan",
            "embedded_symbols": True,
            "files": [{
                "path": "symbols/libskia.debug",
                "sha256": "b" * 64,
                "size": 1,
            }],
        }
        validate_symbols_manifest(symbols, expected_variant="asan")

    def test_gn_labels_cover_root_and_module_targets(self) -> None:
        self.assertEqual(gn_label("skia"), "//:skia")
        self.assertEqual(
            gn_label("modules/skottie:skottie"),
            "//modules/skottie:skottie",
        )

    def test_gn_archive_closure_uses_dependency_outputs_in_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            skia = root / "skia"
            output = skia / "out/release"
            output.mkdir(parents=True)
            (output / "libroot.a").write_bytes(b"root")
            (output / "libdep.a").write_bytes(b"dep")
            script = root / "fake-gn"
            script.write_text(
                "#!/bin/sh\n"
                "if [ \"$4\" = deps ]; then\n"
                "  if [ \"$3\" = //:root ]; then printf '%s\\n' //:root //:group; "
                "elif [ \"$3\" = //:group ]; then printf '%s\\n' //:group //:dep; "
                "else printf '%s\\n' \"$3\"; fi\n"
                "elif [ \"$3\" = //:group ]; then\n"
                "  echo 'group outputs must not be queried' >&2; exit 91\n"
                "elif [ \"$3\" = //:root ]; then printf '%s\\n' out/release/libroot.a\n"
                "else printf '%s\\n' out/release/libdep.a\n"
                "fi\n",
                encoding="utf-8",
            )
            script.chmod(0o755)
            archives = gn_archive_closure(
                script, skia, output, "macos", ["root"],
            )
            self.assertEqual(
                [path.name for path in archives], ["libroot.a", "libdep.a"],
            )


if __name__ == "__main__":
    unittest.main()
