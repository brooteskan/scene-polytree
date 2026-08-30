# Motion storage benchmark

## Decision

The production motion extension uses a sorted sparse active set. Its work and
storage scale with the number of moving nodes, its `NodeHandle` ordering makes
fixed-step integration deterministic, and stationary state is represented by
removing a record.

A dense state array plus active-byte array is a legitimate alternative when
almost every node moves. The measurement below shows that it registers faster,
uses less storage at 100% activity, and has comparable full-scan time there.
That case does not justify making every runtime scene pay dense motion storage
or scanning stationary nodes on every tick. If a future workload stays near
100% active, a separate dense execution policy can be added without changing
the motion-state or integration-policy contracts.

## Method

`scene_polytree_motion_storage_benchmark` builds flat frozen scenes of 1,000,
10,000, and 100,000 nodes. For each size it activates evenly spaced nodes at
0.1%, 1%, 10%, and 100%. The sparse candidate is the production
`active_motion_set`; the dense candidate is one state record and one active
byte per scene node.

Registration is timed once and includes candidate storage growth. Tick time is
the mean over the reported repetition count. A sparse tick reduces active
records directly; a dense tick scans all node slots and filters by the active
byte. Both compute the same checksum. The scene shape does not affect this
storage-only scan; transform propagation is covered separately by the motion
and transform tests.

The hierarchy is a flat forest, execution is a sequential `transform_reduce`,
and reported allocation is retained payload capacity rather than container
object overhead. Changed-node ratio is not applicable because this benchmark
does not evaluate transforms; active ratio is the independent storage variable.

Recorded on Windows x64 with MSVC 19.51.36256.0 in Release on 2026-08-30.
Times are nanoseconds and are illustrative microbenchmark observations, not
cross-machine performance guarantees.

| Nodes | Active | Moving | Repeats | Sparse bytes | Dense bytes | Sparse register | Dense register | Sparse tick | Dense tick |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.1% | 1 | 2,000 | 24 | 17,000 | 500 | 5,900 | 0.9 | 896.8 |
| 1,000 | 1% | 10 | 2,000 | 312 | 17,000 | 2,000 | 5,800 | 4.5 | 918.6 |
| 1,000 | 10% | 100 | 2,000 | 3,384 | 17,000 | 10,900 | 5,800 | 73.8 | 896.9 |
| 1,000 | 100% | 1,000 | 2,000 | 25,584 | 17,000 | 54,300 | 8,200 | 882.2 | 902.2 |
| 10,000 | 0.1% | 10 | 200 | 312 | 170,000 | 5,100 | 52,200 | 5.0 | 8,970.5 |
| 10,000 | 1% | 100 | 200 | 3,384 | 170,000 | 10,300 | 54,000 | 74.5 | 9,164.0 |
| 10,000 | 10% | 1,000 | 200 | 25,584 | 170,000 | 61,000 | 56,300 | 880.0 | 9,221.5 |
| 10,000 | 100% | 10,000 | 200 | 291,312 | 170,000 | 460,000 | 76,900 | 8,943.5 | 9,016.0 |
| 100,000 | 0.1% | 100 | 20 | 3,384 | 1,700,000 | 25,700 | 449,000 | 75.0 | 90,165.0 |
| 100,000 | 1% | 1,000 | 20 | 25,584 | 1,700,000 | 54,600 | 395,500 | 885.0 | 90,055.0 |
| 100,000 | 10% | 10,000 | 20 | 291,312 | 1,700,000 | 501,400 | 419,100 | 9,030.0 | 89,985.0 |
| 100,000 | 100% | 100,000 | 20 | 3,318,120 | 1,700,000 | 4,570,400 | 1,022,900 | 97,615.0 | 95,080.0 |

## Reproduce

```sh
cmake -S . -B build-benchmark \
  -DSCENE_POLYTREE_BUILD_BENCHMARKS=ON \
  -DSCENE_POLYTREE_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-benchmark --config Release
./build-benchmark/benchmarks/scene_polytree_motion_storage_benchmark
```
