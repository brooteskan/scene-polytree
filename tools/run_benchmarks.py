#!/usr/bin/env python3

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE_NAME = (
    "scene_polytree_performance_benchmark.exe"
    if sys.platform == "win32"
    else "scene_polytree_performance_benchmark"
)
EXECUTABLE_CANDIDATES = (
    ROOT / "build-benchmark" / "benchmarks" / "Release" / EXECUTABLE_NAME,
    ROOT / "build-benchmark" / "benchmarks" / EXECUTABLE_NAME,
    ROOT / "build" / "benchmarks" / "Release" / EXECUTABLE_NAME,
    ROOT / "build" / "benchmarks" / EXECUTABLE_NAME,
    ROOT / "build-issue9" / "benchmarks" / "Release" / EXECUTABLE_NAME,
)
DEFAULT_EXECUTABLE = next(
    filter(Path.is_file, EXECUTABLE_CANDIDATES), EXECUTABLE_CANDIDATES[0]
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run reproducible scene-polytree benchmarks")
    parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    parser.add_argument("--preset", choices=("smoke", "full"), default="full")
    parser.add_argument(
        "--suite",
        choices=("all", "topology", "transform", "motion", "synchronization"),
        default="all",
    )
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--seed", type=int, default=0x5CE90009)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def git_output(*arguments: str) -> str:
    completed = subprocess.run(
        ("git", "-c", f"safe.directory={ROOT.as_posix()}", *arguments),
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def main() -> int:
    args = parse_args()
    if args.samples <= 0:
        raise SystemExit("--samples must be positive")
    executable = args.executable.resolve()
    if not executable.is_file():
        raise SystemExit(f"benchmark executable does not exist: {executable}")

    timestamp = datetime.now(timezone.utc)
    output = args.output
    if output is None:
        suffix = timestamp.strftime("%Y%m%dT%H%M%SZ")
        output = ROOT / "benchmarks" / "results" / f"baseline-{suffix}.jsonl"
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    command = (
        str(executable),
        f"--preset={args.preset}",
        f"--suite={args.suite}",
        f"--samples={args.samples}",
        f"--seed={args.seed}",
    )
    completed = subprocess.run(command, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        print(completed.stdout, file=sys.stderr)
        print(completed.stderr, file=sys.stderr)
        return completed.returncode

    records = list(map(json.loads, filter(str.strip, completed.stdout.splitlines())))
    metadata = next(
        (record for record in records if record.get("record_type") == "metadata"), None
    )
    if metadata is None:
        print("benchmark emitted no metadata record", file=sys.stderr)
        return 3
    metadata.update(
        {
            "run_id": timestamp.strftime("%Y%m%dT%H%M%SZ"),
            "utc_timestamp": timestamp.isoformat(),
            "host_node": platform.node(),
            "platform": platform.platform(),
            "python_version": platform.python_version(),
            "command": list(command),
            "launcher_git_revision": git_output("rev-parse", "HEAD"),
            "launcher_git_dirty": bool(git_output("status", "--porcelain")),
        }
    )
    metadata["git_revision"] = metadata["launcher_git_revision"]
    metadata["git_dirty"] = metadata["launcher_git_dirty"]
    output.write_text(
        "".join(json.dumps(record, separators=(",", ":")) + "\n" for record in records),
        encoding="utf-8",
    )
    measurements = tuple(
        filter(lambda record: record.get("record_type") == "measurement", records)
    )
    if not measurements or not all(record.get("correct") for record in measurements):
        print("benchmark emitted missing or failed measurements", file=sys.stderr)
        return 4
    print(f"wrote {len(measurements)} measurements to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
