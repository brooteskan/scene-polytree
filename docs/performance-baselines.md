# Performance baselines

> This document records the pre-optimization issue #9 baseline and its budgets.
> The issue #13 implementation, direct before/after measurements, allocation
> checks, executor grain comparison, and budget disposition are published in
> [Performance round 2](performance-round-2.md).

## Scope and method

The issue #9 runner separates four costs:

1. mutable authoring topology to compact runtime topology and incremental
   freeze;
2. dirty planning and transform propagation;
3. active-motion maintenance and fixed-step motion evaluation; and
4. changed-node selection and engine-neutral target synchronization.

Synthetic fixtures cover chains, wide trees, four-way balanced trees, and
forests at 1,000, 10,000, and 100,000 nodes. Articulated fixtures cover 1, 32,
256, 1,024, and 10,000 three-node tanks. Requested dirty and active ratios are
zero, sparse, medium, and full. A separate dirty-root case records descendant
amplification: one changed root affects all 100,000 nodes in a chain, wide
tree, or balanced tree, but only 341 nodes in the 100,000-node forest fixture.

The benchmark uses Release MSVC 19.51.36256.0 on Windows 11, an AMD64 Family
23 Model 8 processor, and 16 hardware threads. Tables report the median and p95
of three samples in milliseconds. The complete machine-readable record is
[`windows-msvc-baseline-20260830.jsonl`](../benchmarks/results/windows-msvc-baseline-20260830.jsonl).
Its metadata truthfully records that the measurement came from the dirty issue
#9 implementation checkout at base revision `d0266904166`.

Fixture setup, correctness comparison, and result serialization are outside
the measured phase. The benchmark executable replaces allocation functions to
count allocations and bytes; production targets are unchanged. Every
topological/dependency-level pair is checked for exact world-transform parity.

## Topology compilation

| 100,000-node shape | Initial freeze median | Initial p95 | Clean incremental median | Clean incremental p95 |
| --- | ---: | ---: | ---: | ---: |
| Wide | 134.20 | 134.48 | 153.30 | 154.86 |
| Balanced | 151.20 | 152.70 | 165.77 | 167.55 |
| Forest | 152.54 | 153.23 | 170.52 | 172.19 |
| Chain | 171.96 | 184.10 | 186.40 | 188.45 |

Freeze is not suitable for a frame-time path at these sizes. Incremental
freeze preserves runtime state semantics but still recompiles and allocates a
complete topology. The 100,000-node cases retain about 4.25 MB of reusable
freeze scratch and perform roughly 200,000 to 300,000 measured allocations,
depending on shape.

## Transform planning and evaluation

Warm workspaces perform zero allocations. Planning still scans every node, so
its cost remains visible at zero changed nodes.

| 100,000-node shape | Warm clean plan median | Full plan median | Full plan p95 | Full evaluation median | Full evaluation p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wide | 0.580 | 0.793 | 0.797 | 3.162 | 3.297 |
| Chain | 0.619 | 1.249 | 1.641 | 3.919 | 4.255 |
| Balanced | 1.636 | 2.464 | 2.694 | 5.957 | 6.435 |
| Forest | 1.365 | 2.345 | 2.382 | 6.456 | 7.264 |

The transform workspace retains 2.2 MB at 100,000 nodes. For sparse updates,
the `O(N)` dirty plan dominates the affected-only composition. For full
dirty-root propagation, the benchmark-only scheduling kernels measured:

| Shape | Maximum level width | Topological median | Sequential dependency-level median |
| --- | ---: | ---: | ---: |
| Chain | 1 | 3.676 | 3.663 |
| Wide | 99,999 | 2.784 | 2.679 |
| Balanced | 65,536 | 4.260 | 4.045 |

Sequential dependency levels are equivalent, but not consistently enough
faster to justify replacing the production topological evaluator by
themselves. Their value is the independent width exposed to a future executor.

## Motion and active-set maintenance

Steady fixed-step evaluation reuses all workspaces and performs no measured
allocations.

| 10,000 tanks / 30,000 nodes | Median | p95 |
| --- | ---: | ---: |
| Warm step, no active motion | 0.357 | 0.446 |
| Warm step, 0.1% active | 0.273 | 0.404 |
| Warm step, 10% active | 0.806 | 1.159 |
| Warm step, 100% active | 4.683 | 4.810 |

The sorted sparse vector is efficient for stable ordered records, but
individual insertion and erasure are strongly order-dependent:

| 30,000 active records | Median | p95 |
| --- | ---: | ---: |
| Ascending activation | 1.887 | 1.910 |
| Descending activation | 754.124 | 783.196 |
| Seeded shuffled activation | 373.988 | 375.443 |
| Update existing records | 1.586 | 1.640 |
| Seeded 10% deactivate/reactivate churn | 141.705 | 147.861 |
| Ascending per-record deactivation | 983.943 | 993.698 |
| Descending per-record deactivation | 1.350 | 1.414 |

This is not a transform-parallelism problem. It is a batch-maintenance and
data-structure problem. Full shutdown should continue to use `clear`; unordered
lifecycle changes need a validated sort-and-merge batch operation before they
are admitted to a frame-time workload.

## Synchronization

The engine-neutral synchronization fixture performs the same core work as the
O3DE adapter: filter by world revision, compose a node-to-target offset, and
write a dense target mirror. O3DE entity lookup and bus dispatch are not
included.

| 30,000 nodes | Selection median | Target-write median | Combined median | Combined p95 |
| --- | ---: | ---: | ---: | ---: |
| Zero changed | 0.162 | 0.001 | 0.180 | 0.284 |
| 0.1% changed | 0.123 | 0.002 | 0.192 | 0.258 |
| 10% changed | 0.149 | 0.125 | 0.249 | 0.290 |
| 100% changed | 0.323 | 1.061 | 1.370 | 1.467 |

Changed-node discovery performs an `O(N)` scan and retains four bytes of
scratch per node. The scan dominates sparse synchronization, while writes
dominate a fully changed scene.

## Reference budgets

These are reference-machine budgets, not shared-runner CI gates. A controlled
machine comparison should flag `max(15%, 3 * MAD)` regressions and investigate
before changing a recorded baseline.

| Operation | Reference budget |
| --- | ---: |
| Initial or incremental freeze, 100,000 nodes | p95 <= 200 ms, non-frame path |
| Warm dirty planning, 100,000 nodes | p95 <= 3.0 ms, zero allocations |
| Full transform evaluation, 100,000 nodes | p95 <= 7.5 ms, zero allocations |
| Warm full motion, 30,000 nodes | p95 <= 5.0 ms, zero allocations |
| Warm 10% motion, 30,000 nodes | p95 <= 1.25 ms, zero allocations |
| Full synchronization, 30,000 nodes | p95 <= 1.75 ms after warm scratch |
| Existing-active update, 30,000 records | p95 <= 1.75 ms, zero allocations |
| Ordinary active-set lifecycle maintenance | p95 <= 1.67 ms (10% of a 60 Hz frame) |

The existing unordered bulk insertion, churn, and per-record front erasure do
not meet the lifecycle budget. This is a recorded design limit, not a reason to
weaken deterministic ordering or error behavior.
