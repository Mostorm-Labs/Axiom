from __future__ import annotations

import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest import mock


SKIA_TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SKIA_TOOLS))

import fetch  # noqa: E402
from consumer import POC01_EXPECTED_TARGETS, validate_lock  # noqa: E402
from sdk import (  # noqa: E402
    canonical_sha256, file_sha256, load_profile, make_identity,
    normalized_args_text, profile_hash,
)


class ConsumerTest(unittest.TestCase):
    target = "macos-arm64-metal"

    def setUp(self) -> None:
        self.profile = load_profile()
        self.identity = make_identity(self.profile, self.target, {"sdk": "macosx"})
        self.payload = normalized_args_text(self.profile, self.target).encode("utf-8")
        self.archive_bytes = b"locked archive bytes"
        target_metadata = {}
        for index, target in enumerate(sorted(POC01_EXPECTED_TARGETS), start=1):
            sdk_id = (
                canonical_sha256(self.identity)
                if target == self.target else hashlib.sha256(target.encode()).hexdigest()
            )
            target_metadata[target] = {
                "asset": f"skia-sdk-{target}.zip",
                "sdk_id": sdk_id,
                "sha256": (
                    hashlib.sha256(self.archive_bytes).hexdigest()
                    if target == self.target else f"{index:064x}"
                ),
                "size": len(self.archive_bytes) if target == self.target else index,
                "toolchain": self.identity["toolchain"] if target == self.target else {},
            }
        self.lock = {
            "schema_version": 1,
            "format": "canvas-skia-sdk-lock-v1",
            "repository": "Mostorm-Labs/Axiom",
            "tag": "skia-sdk-test",
            "set_id": canonical_sha256({
                name: value["sdk_id"] for name, value in target_metadata.items()
            }),
            "profile": self.profile["profile"],
            "profile_hash": profile_hash(self.profile),
            "skia_commit": self.profile["skia_commit"],
            "source_commit": "1" * 40,
            "targets": target_metadata,
        }

    def write_lock(self, root: Path) -> Path:
        path = root / "lock.json"
        path.write_text(json.dumps(self.lock), encoding="utf-8")
        return path

    def write_installed(self, root: Path) -> None:
        root.mkdir(parents=True, exist_ok=True)
        (root / "args.gn").write_bytes(self.payload)
        manifest = {
            "schema_version": 1,
            "format": "canvas-skia-sdk-v1",
            "sdk_id": canonical_sha256(self.identity),
            "identity": self.identity,
            "files": [{
                "path": "args.gn",
                "sha256": hashlib.sha256(self.payload).hexdigest(),
                "size": len(self.payload),
                "role": "build-arguments",
            }],
        }
        (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    def download(self, observed: list[str], data: bytes | None = None):
        content = self.archive_bytes if data is None else data

        def implementation(url: str, destination: Path) -> int:
            observed.append(url)
            destination.write_bytes(content)
            return len(content)

        return implementation

    def extract(self, _archive, _profile, target, extract, **_kwargs):
        self.assertEqual(target, self.target)
        self.write_installed(extract)
        return {"sdk_id": self.lock["targets"][self.target]["sdk_id"]}

    def tree_hashes(self, root: Path) -> dict[str, str]:
        return {
            path.relative_to(root).as_posix(): file_sha256(path)
            for path in root.rglob("*") if path.is_file()
        }

    def test_poc01_fixture_has_seven_targets_and_generic_lock_allows_profiles(self) -> None:
        validate_lock(self.lock)
        self.assertEqual(set(self.lock["targets"]), POC01_EXPECTED_TARGETS)
        invalid = json.loads(json.dumps(self.lock))
        invalid["targets"] = {}
        with self.assertRaisesRegex(RuntimeError, "at least one target"):
            validate_lock(invalid)

    def test_existing_install_is_fully_verified_before_reuse(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock_path = self.write_lock(root)
            destination = root / "install" / self.target
            self.write_installed(destination)
            with mock.patch.object(fetch, "download") as download:
                result = fetch.install(
                    self.target, lock_path, install_root=root / "install",
                )
            self.assertEqual(result["source"], "installed")
            self.assertEqual(result["bytes"], 0)
            download.assert_not_called()

    def test_github_and_mirror_urls_install_identical_verified_trees(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock_path = self.write_lock(root)
            asset = self.lock["targets"][self.target]["asset"]
            mirror_asset = root / "http" / self.lock["tag"] / asset
            mirror_asset.parent.mkdir(parents=True)
            mirror_asset.write_bytes(self.archive_bytes)
            observed: list[str] = []
            original_download = fetch.download

            class QuietHandler(SimpleHTTPRequestHandler):
                def log_message(self, _format: str, *_args) -> None:
                    pass

            server = ThreadingHTTPServer(
                ("127.0.0.1", 0),
                lambda *args, **kwargs: QuietHandler(
                    *args, directory=str(root / "http"), **kwargs,
                ),
            )
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()

            def download(url: str, destination: Path) -> int:
                observed.append(url)
                if url.startswith("https://github.com/"):
                    destination.write_bytes(self.archive_bytes)
                    return len(self.archive_bytes)
                return original_download(url, destination)

            try:
                with mock.patch.object(fetch, "download", download), \
                     mock.patch.object(fetch, "verify_archive", self.extract):
                    github = fetch.install(
                        self.target, lock_path, install_root=root / "github",
                    )
                    mirror = fetch.install(
                        self.target, lock_path, install_root=root / "mirror",
                        base_url=f"http://127.0.0.1:{server.server_port}",
                    )
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=5)
            self.assertEqual(
                observed,
                [
                    f"https://github.com/Mostorm-Labs/Axiom/releases/download/skia-sdk-test/{asset}",
                    f"http://127.0.0.1:{server.server_port}/skia-sdk-test/{asset}",
                ],
            )
            self.assertEqual(github["sdk_id"], mirror["sdk_id"])
            self.assertEqual(
                self.tree_hashes(Path(github["path"])),
                self.tree_hashes(Path(mirror["path"])),
            )

    def test_corrupt_download_leaves_no_consumable_partial_install(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock_path = self.write_lock(root)
            install_root = root / "install"
            observed: list[str] = []
            corrupted = b"x" * len(self.archive_bytes)
            with mock.patch.object(
                fetch, "download", self.download(observed, corrupted),
            ), self.assertRaisesRegex(RuntimeError, "SHA-256"):
                fetch.install(self.target, lock_path, install_root=install_root)
            self.assertFalse((install_root / self.target).exists())
            self.assertEqual(list(install_root.iterdir()), [])

    def test_failed_atomic_swap_restores_previous_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock_path = self.write_lock(root)
            install_root = root / "install"
            destination = install_root / self.target
            destination.mkdir(parents=True)
            (destination / "previous.txt").write_text("preserve", encoding="utf-8")
            original_replace = Path.replace

            def replace(path: Path, target: Path):
                if path.name == "verified" and Path(target).name == self.target:
                    raise OSError("simulated atomic swap failure")
                return original_replace(path, target)

            with mock.patch.object(fetch, "download", self.download([])), \
                 mock.patch.object(fetch, "verify_archive", self.extract), \
                 mock.patch.object(Path, "replace", replace), \
                 self.assertRaisesRegex(OSError, "atomic swap"):
                fetch.install(self.target, lock_path, install_root=install_root)
            self.assertEqual(
                (destination / "previous.txt").read_text(encoding="utf-8"),
                "preserve",
            )
            self.assertFalse((install_root / f".{self.target}.previous").exists())

    def test_interrupted_swap_recovery_is_verified_and_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock_path = self.write_lock(root)
            install_root = root / "install"
            previous = install_root / f".{self.target}.previous"
            self.write_installed(previous)
            with mock.patch.object(fetch, "download") as download:
                result = fetch.install(
                    self.target, lock_path, install_root=install_root,
                )
            self.assertEqual(result["source"], "installed")
            self.assertTrue((install_root / self.target / "manifest.json").is_file())
            self.assertFalse(previous.exists())
            download.assert_not_called()


if __name__ == "__main__":
    unittest.main()
