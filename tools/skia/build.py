#!/usr/bin/env python3
"""Build one target from a versioned Canvas Skia SDK profile."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess

from sdk import (
    DEFAULT_PROFILE, SKIA_ROOT, actual_gn_args, gn_args_line, load_profile,
    ninja_targets, target_definition,
)


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(shlex.quote(part) for part in command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--skia-root", type=Path, default=SKIA_ROOT)
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
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
    )
    gn_args = gn_args_line(values)
    if args.print_args:
        print(gn_args)
        return 0

    host_gn = "gn.exe" if os.name == "nt" else "gn"
    gn = Path(args.gn).resolve() if args.gn else skia_root / "bin" / host_gn
    if not gn.exists():
        raise RuntimeError("missing GN; run bootstrap_deps.py --skia --sync-skia")
    output = skia_root / "out" / target["output_name"]
    run([str(gn), "gen", str(output), f"--args={gn_args}"], skia_root)
    run([args.ninja, "-C", str(output), *ninja_targets(profile)], skia_root)
    missing = [name for name in target["libraries"] if not (output / name).is_file()]
    if missing:
        raise RuntimeError(f"build did not produce required libraries: {missing}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
