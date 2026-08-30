#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from itertools import chain
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SEARCH_ROOTS = tuple(
    ROOT / name
    for name in ("include", "extensions", "integrations", "examples", "benchmarks", "tests")
)
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".c", ".cc", ".cpp", ".cxx"})
RAW_LOOP = re.compile(r"\b(?:for|while)\s*\(|\bdo\s*\{")


def source_files() -> tuple[Path, ...]:
    candidates = chain.from_iterable(
        root.rglob("*") for root in SEARCH_ROOTS if root.exists()
    )
    return tuple(filter(lambda path: path.is_file() and path.suffix in SUFFIXES, candidates))


def violations(path: Path) -> tuple[str, ...]:
    lines = path.read_text(encoding="utf-8").splitlines()
    matches = filter(lambda item: RAW_LOOP.search(item[1]), enumerate(lines, start=1))
    return tuple(map(lambda item: f"{path.relative_to(ROOT)}:{item[0]}: {item[1].strip()}", matches))


def main() -> int:
    failures = tuple(chain.from_iterable(map(violations, source_files())))
    if failures:
        print("Handwritten C++ loop statements are not permitted:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1

    print("No handwritten C++ loop statements found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
