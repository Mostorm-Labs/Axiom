#!/usr/bin/env python3
"""Build one target from a versioned Canvas Skia SDK profile."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

from sdk import (
    DEFAULT_PROFILE, SKIA_ROOT, actual_gn_args, gn_args_line, load_profile,
    output_name, target_definition,
)


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(shlex.quote(part) for part in command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def gn_label(target: str) -> str:
    if target.startswith("//"):
        return target
    if ":" in target or "/" in target:
        return f"//{target}"
    return f"//:{target}"


def gn_archive_closure(
    gn: Path, skia_root: Path, output: Path, platform: str,
    build_targets: list[str],
) -> list[Path]:
    """Resolve static archives from GN's actual transitive dependency graph."""
    suffix = ".lib" if platform == "windows" else ".a"
    roots = [target for target in build_targets if target != "skia"]
    roots.extend(target for target in build_targets if target == "skia")
    labels: list[str] = [gn_label(target) for target in roots]
    direct_deps: dict[str, list[str]] = {}
    visiting: set[str] = set()

    def deps(label: str) -> list[str]:
        if label in direct_deps:
            return direct_deps[label]
        described = subprocess.check_output(
            [str(gn), "desc", str(output), label, "deps"],
            cwd=skia_root, text=True,
        )
        values = []
        for line in described.splitlines():
            value = line.strip().split("(", 1)[0].strip()
            if value.startswith("//") and value != label:
                values.append(value)
        direct_deps[label] = list(dict.fromkeys(values))
        return direct_deps[label]

    def visit(label: str) -> None:
        if label in visiting:
            return
        visiting.add(label)
        for dependency in deps(label):
            if dependency not in labels:
                labels.append(dependency)
            visit(dependency)
        visiting.remove(label)

    for label in list(labels):
        visit(label)
    archives: list[Path] = []
    seen_labels: set[str] = set()
    seen_paths: set[Path] = set()
    for label in labels:
        if label in seen_labels:
            continue
        seen_labels.add(label)
        try:
            described = subprocess.check_output(
                [str(gn), "desc", str(output), label, "outputs"],
                cwd=skia_root, text=True, stderr=subprocess.PIPE,
            )
        except subprocess.CalledProcessError:
            # GN group/action/source_set targets do not expose outputs. Their
            # static-library dependencies are visited separately above.
            continue
        for value in described.splitlines():
            value = value.strip()
            if not value.endswith(suffix):
                continue
            candidate = Path(value.removeprefix("//"))
            if candidate.is_absolute():
                path = candidate
            elif candidate.parts and candidate.parts[0] == "out":
                path = skia_root / candidate
            else:
                path = output / candidate
            path = path.resolve()
            try:
                path.relative_to(output.resolve())
            except ValueError as error:
                raise RuntimeError(
                    f"GN archive output escapes the selected build directory: {value}"
                ) from error
            if not path.is_file():
                raise RuntimeError(f"GN archive output is missing: {path}")
            if path not in seen_paths:
                seen_paths.add(path)
                archives.append(path)
    return archives


def write_archive_closure(
    gn: Path, skia_root: Path, output: Path, platform: str,
    build_targets: list[str],
) -> Path:
    """Freeze the GN-derived archive closure so packaging never scans opportunistically."""
    archives = gn_archive_closure(gn, skia_root, output, platform, build_targets)
    if not archives:
        raise RuntimeError("build did not produce any target static libraries")
    metadata = {
        "schema_version": 1,
        "archives": [path.relative_to(output).as_posix() for path in archives],
    }
    destination = output / "canvas-skia-archive-closure.json"
    destination.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )
    return destination


def stage_windows_asan_runtime(clang_root: Path, output: Path) -> None:
    """Copy the pinned dynamic ASan runtime closure beside the GN outputs."""
    runtime_names = (
        "clang_rt.asan_dynamic-x86_64.dll",
        "clang_rt.asan_dynamic-x86_64.lib",
        "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib",
    )
    runtime_roots = sorted((clang_root / "lib/clang").glob("*/lib/windows"))
    if len(runtime_roots) != 1:
        raise RuntimeError(
            "Windows LLVM must expose exactly one compiler-rt runtime directory"
        )
    for name in runtime_names:
        source = runtime_roots[0] / name
        if not source.is_file():
            raise RuntimeError(f"Windows ASan runtime is missing: {source}")
        shutil.copyfile(source, output / name)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--variant", choices=("release", "debug", "asan"), default="release")
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--skia-root", type=Path, default=SKIA_ROOT)
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    parser.add_argument("--clang-win")
    parser.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--gn")
    parser.add_argument("--ninja", default=os.environ.get("NINJA", "ninja"))
    parser.add_argument("--print-args", action="store_true")
    args = parser.parse_args()

    profile_path = args.profile.resolve()
    profile = load_profile(profile_path)
    target = target_definition(profile, args.target)
    skia_root = args.skia_root.resolve()
    marker = skia_root / ".canvas-poc-revision"
    if marker.exists():
        actual_revision = marker.read_text(encoding="utf-8").strip()
    else:
        actual_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=skia_root, text=True,
        ).strip()
    if actual_revision != profile["skia_commit"]:
        raise RuntimeError(
            f"Skia revision mismatch: expected {profile['skia_commit']}, "
            f"got {actual_revision}"
        )

    values = actual_gn_args(
        profile, args.target, cc=args.cc, cxx=args.cxx, ndk=args.ndk,
        clang_win=args.clang_win, variant=args.variant,
    )
    gn_args = gn_args_line(values)
    if args.print_args:
        print(gn_args)
        return 0

    host_gn = "gn.exe" if os.name == "nt" else "gn"
    gn = Path(args.gn).resolve() if args.gn else skia_root / "bin" / host_gn
    if not gn.exists():
        raise RuntimeError("missing GN; run bootstrap_deps.py --skia --sync-skia")
    output = skia_root / "out" / output_name(profile, args.target, args.variant)
    run([str(gn), "gen", str(output), f"--args={gn_args}"], skia_root)
    build_targets = profile.get("build_targets", ["skia"])
    run([args.ninja, "-C", str(output), *build_targets], skia_root)
    if target["platform"] == "windows" and args.variant == "asan":
        stage_windows_asan_runtime(Path(args.clang_win).resolve(), output)
    if target["libraries"] == "discover":
        closure = write_archive_closure(
            gn, skia_root, output, target["platform"], build_targets,
        )
        print(f"wrote target archive closure: {closure}", flush=True)
    else:
        missing = [name for name in target["libraries"] if not (output / name).is_file()]
        if missing:
            raise RuntimeError(f"build did not produce required libraries: {missing}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
