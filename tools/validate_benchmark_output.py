#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys


REQUIRED_MEASUREMENT_FIELDS = {
    "schema_version",
    "record_type",
    "suite",
    "phase",
    "shape",
    "node_count",
    "edge_count",
    "depth",
    "instance_count",
    "wall_ns",
    "allocation_count",
    "allocated_bytes",
    "peak_live_bytes",
    "retained_bytes",
    "scratch_bytes",
    "checksum",
    "correct",
}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_benchmark_output.py <benchmark-executable>", file=sys.stderr)
        return 2

    completed = subprocess.run(
        [sys.argv[1], "--preset=smoke", "--samples=1"],
        check=False,
        capture_output=True,
        text=True,
        timeout=110,
    )
    if completed.returncode != 0:
        print(completed.stdout, file=sys.stderr)
        print(completed.stderr, file=sys.stderr)
        return completed.returncode

    try:
        records = tuple(map(json.loads, filter(str.strip, completed.stdout.splitlines())))
    except json.JSONDecodeError as error:
        print(f"benchmark emitted invalid JSONL: {error}", file=sys.stderr)
        return 3

    metadata = tuple(filter(lambda record: record.get("record_type") == "metadata", records))
    measurements = tuple(
        filter(lambda record: record.get("record_type") == "measurement", records)
    )
    missing = tuple(
        (index, REQUIRED_MEASUREMENT_FIELDS - record.keys())
        for index, record in enumerate(measurements)
        if REQUIRED_MEASUREMENT_FIELDS - record.keys()
    )
    if len(metadata) != 1 or not measurements or missing:
        print(
            f"invalid benchmark record set: metadata={len(metadata)} "
            f"measurements={len(measurements)} missing={missing}",
            file=sys.stderr,
        )
        return 4
    if not all(record["correct"] for record in measurements):
        print("one or more benchmark correctness checks failed", file=sys.stderr)
        return 5

    print(f"validated {len(measurements)} benchmark measurements")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
