#!/usr/bin/env python3
"""Run one CI command and append its elapsed time to the job summary."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    parser.add_argument("--summary-file", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a command is required after --")
    started = time.monotonic()
    try:
        return subprocess.run(command, check=False).returncode
    finally:
        elapsed = time.monotonic() - started
        with args.summary_file.open("a", encoding="utf-8") as summary:
            summary.write(f"- {args.label}: {elapsed:.3f} s\n")


if __name__ == "__main__":
    raise SystemExit(main())
