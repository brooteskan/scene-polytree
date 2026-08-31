#!/usr/bin/env python3

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
from statistics import median


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize scene-polytree JSONL results")
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, round((len(ordered) - 1) * fraction))
    return ordered[index]


def main() -> int:
    args = parse_args()
    records = tuple(
        map(json.loads, filter(str.strip, args.input.read_text(encoding="utf-8").splitlines()))
    )
    metadata = next(record for record in records if record["record_type"] == "metadata")
    measurements = tuple(record for record in records if record["record_type"] == "measurement")
    grouped: dict[tuple[object, ...], list[dict[str, object]]] = defaultdict(list)
    for record in measurements:
        key = (
            record["suite"],
            record["phase"],
            record["shape"],
            record["propagation_order"],
            record.get("worker_count", 0),
            record.get("task_grain", 0),
            record["node_count"],
            record["actor_count"],
            record["requested_ratio"],
        )
        grouped[key].append(record)

    lines = [
        "# scene-polytree benchmark summary",
        "",
        f"- Run: `{metadata.get('run_id', 'unknown')}`",
        f"- Revision: `{metadata.get('launcher_git_revision', metadata.get('git_revision'))}`",
        f"- Platform: `{metadata.get('platform', metadata.get('system'))}`",
        f"- Processor: `{metadata.get('processor', 'unknown')}`",
        f"- Compiler: `{metadata.get('compiler')} {metadata.get('compiler_version')}`",
        f"- Build: `{metadata.get('build_config')}`",
        "",
        "| Suite | Phase | Shape | Order | Workers | Grain | Nodes | Actors | Requested | Changed | Median ns | p95 ns | MAD ns | Allocations | Tasks | Dispatches | Scratch bytes |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for key, rows in sorted(grouped.items()):
        walls = [float(row["wall_ns"]) for row in rows]
        center = median(walls)
        mad = median([abs(value - center) for value in walls])
        representative = rows[0]
        lines.append(
            "| {} | {} | {} | {} | {} | {} | {} | {} | {:.3g} | {:.3g} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {} |".format(
                *key[:4],
                key[4],
                key[5],
                key[6],
                key[7],
                float(key[8]),
                float(representative["actual_changed_ratio"]),
                center,
                percentile(walls, 0.95),
                mad,
                median([float(row["allocation_count"]) for row in rows]),
                median([float(row.get("task_count", 0)) for row in rows]),
                median(
                    [float(row.get("parallel_dispatch_count", 0)) for row in rows]
                ),
                representative["scratch_bytes"],
            )
        )
    text = "\n".join(lines) + "\n"
    if args.output is None:
        print(text, end="")
    else:
        args.output.write_text(text, encoding="utf-8")
        print(f"wrote summary to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
