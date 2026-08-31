# Benchmarks

## Comprehensive performance runner

`scene_polytree_performance_benchmark` measures topology compilation, transform
planning and propagation, motion and active-set maintenance, and the
engine-neutral synchronization boundary. It uses the real rigid-pose policy
and a generic three-node articulation schema without linking O3DE.

The runner emits JSON Lines to standard output. The first record describes the
machine, compiler, build, revision, seed, and invocation. Each remaining record
contains topology shape and size, requested and actual changed ratios, timing,
allocation count and bytes, peak live bytes, retained payload, scratch
capacity, checksum, and correctness status.

Configure and build a Release binary with:

```sh
cmake -S . -B build-benchmark \
  -DSCENE_POLYTREE_BUILD_BENCHMARKS=ON \
  -DSCENE_POLYTREE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-benchmark --config Release
```

Run the bounded correctness matrix directly:

```sh
./build-benchmark/benchmarks/scene_polytree_performance_benchmark \
  --preset=smoke --samples=1
```

Use the launcher for a recorded full run. Pass `--executable` when the build
layout is not one of the standard single- or multi-configuration locations.

```sh
python tools/run_benchmarks.py \
  --preset full --samples 5 \
  --output benchmarks/results/local-baseline.jsonl
python tools/summarize_benchmarks.py \
  benchmarks/results/local-baseline.jsonl \
  --output local-summary.md
```

`--suite` accepts `topology`, `transform`, `motion`, `synchronization`, or
`all`. Fixture generation, correctness comparisons, and checksums are outside
the measured operation. Cold and warm workspace phases are reported
separately. Allocation instrumentation is linked only into the benchmark
executable.

The smoke runner is a CTest correctness/schema test. Full wall-clock results
are not gated on shared CI machines.

## Motion storage experiment

`scene_polytree_motion_storage_benchmark` compares the production sorted sparse
active-motion set with a dense state-plus-active-flag candidate. It reports
registration time, storage capacity, and a representative per-tick state scan
for 1,000, 10,000, and 100,000 nodes at four active ratios.

Benchmarks must report node count, hierarchy shape, changed-node ratio,
allocation behavior, and execution policy.

Configure and run a Release build with:

```sh
cmake -S . -B build-benchmark -DSCENE_POLYTREE_BUILD_BENCHMARKS=ON \
  -DSCENE_POLYTREE_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-benchmark --config Release
./build-benchmark/benchmarks/scene_polytree_motion_storage_benchmark
```

The historical storage decision and its reproducible result table are in
[`docs/motion-storage-results.md`](../docs/motion-storage-results.md).
