# Benchmarks

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

The recorded decision and a reproducible result table are in
[`docs/motion-storage-results.md`](../docs/motion-storage-results.md).
