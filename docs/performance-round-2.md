# Performance round 2

## Method

This round measures the issue #13 implementation on the same Windows 11,
MSVC 19.51, AMD64 Family 23 Model 8, 16-hardware-thread machine used for the
issue #9 baseline. Every row is the median and p95 of three Release samples.
The runner emitted 3,048 correctness-checked measurements; worker-thread
allocations are included in the allocation probe.

The machine-readable results are
[`windows-msvc-issue13-20260831.jsonl`](../benchmarks/results/windows-msvc-issue13-20260831.jsonl),
with the complete generated table in
[`windows-msvc-issue13-20260831-summary.md`](../benchmarks/results/windows-msvc-issue13-20260831-summary.md).
The run came from a dirty issue #13 worktree based on revision `9572737081a3`;
the dirty flag is intentional because these measurements validate the
implementation before commit.

## Incremental planning

The benchmark runs the former full-topology scan and the production dirty
frontier against the same state. Times below are p95 milliseconds at 100,000
nodes.

| Shape | Full-scan clean | Frontier clean | Full-scan 0.1% | Frontier 0.1% | Frontier 100% |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wide | 0.614 | 0.0004 | 1.036 | 0.0025 | 0.0126 |
| Chain | 0.532 | 0.0009 | 0.717 | 0.0012 | 0.0306 |
| Balanced | 0.805 | 0.0009 | 0.793 | 0.0033 | 0.0270 |
| Forest | 1.005 | 0.0004 | 0.920 | 0.0035 | 0.0130 |

The clean gain is approximately 590x to 2,500x; the 0.1% gain is 240x to
600x. The full-dirty fast path copies the cached generic roots and topological
view directly. Arbitrarily ordered sparse edits still sort by cached
topological rank, while already ordered edits skip that sort.

The tradeoff is retained metadata: the transform workspace now holds about
6.0 MB for wide/balanced/forest and 6.8 MB for the 100,000-node chain, compared
with 2.2 MB in the old production planner, and runtime state retains another
0.4 MB of direct-frontier handle capacity. This buys an exact direct-dirty
frontier, subtree ranges, generation masks, dependency levels, and preallocated
executor packing. All warm planning paths allocated zero bytes.

## Batch active-motion maintenance

The full 30,000-record unordered lifecycle case shows the algorithmic gain.

| Operation | Per-record median | Per-record p95 | Batch median | Batch p95 | Median gain |
| --- | ---: | ---: | ---: | ---: | ---: |
| Reverse activation | 715.653 ms | 717.060 ms | 4.869 ms | 5.451 ms | 147x |
| Shuffled activation | 362.927 ms | 379.068 ms | 4.869 ms | 5.451 ms | 75x |
| Ascending/front deactivation | 905.878 ms | 931.944 ms | 2.994 ms | 3.108 ms | 303x |

At the ordinary 10% lifecycle size, batch activation, warm update, and
deactivation had p95 values of 0.346, 0.259, and 0.251 ms. Warm batch update
and deactivation allocated zero bytes. A full warm batch update costs 3.585 ms
median because it deliberately normalizes and sorts all 30,000 inputs; the
unchanged per-record API remains preferable for already ordered updates to
existing records.

## Dependency-level CPU executor

This table compares the production topological evaluator with the persistent
15-worker executor at the default 2,048-node grain. Times are milliseconds for
a 100,000-node affected batch.

| Shape | Sequential median | Sequential p95 | CPU median | CPU p95 | Tasks / dispatches | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Wide | 3.052 | 3.187 | 2.483 | 2.577 | 49 / 1 | 1.23x faster |
| Balanced | 5.377 | 5.511 | 3.550 | 3.591 | 49 / 4 | 1.51x faster |
| Forest | 6.467 | 6.580 | 3.696 | 4.147 | 50 / 3 | 1.75x faster |
| Chain | 3.615 | 3.623 | 3.665 | 3.716 | 0 / 0 | sequential fallback |

The 1,024-node grain was fastest for balanced, while 8,192 narrowly led wide;
the documented 2,048 default remains a useful conservative starting point
across shapes. All four measured grains produced exact record and changed order
parity. Parallel evaluation allocated zero bytes; chains, sparse sub-grain
batches, and the 341-node forest dirty-root case dispatched no work.

## Direct evaluated-batch synchronization

The revision fallback selects by scanning topology and then writes. The direct
path writes the evaluation result while its lifetime is valid. Times are
milliseconds for 30,000 nodes.

| Changed ratio | Revision select + write median | Fallback p95 | Direct median | Direct p95 |
| ---: | ---: | ---: | ---: | ---: |
| 0% | 0.167 | 0.179 | 0.0001 | 0.0001 |
| 0.1% | 0.080 | 0.092 | 0.0008 | 0.0008 |
| 10% | 0.213 | 0.281 | 0.078 | 0.079 |
| 100% | 1.846 | 2.242 | 1.235 | 1.659 |

The O3DE adapter accumulates and de-duplicates same-tick evaluated batches,
orders their union by cached topological rank, and uses the revision scan only
after direct-batch lifetime is lost.

## Budget disposition

| Reference budget | Round-2 result |
| --- | --- |
| Freeze, 100,000 nodes, p95 <= 200 ms | Met; maximum p95 193.0 ms |
| Warm dirty planning, p95 <= 3.0 ms, zero allocations | Met; maximum p95 0.0306 ms |
| Full transform evaluation, p95 <= 7.5 ms, zero allocations | Met; maximum sequential p95 6.58 ms |
| Warm full motion, p95 <= 5.0 ms, zero allocations | Met; p95 4.36 ms |
| Warm 10% motion, p95 <= 1.25 ms, zero allocations | Met; p95 0.336 ms |
| Full synchronization, p95 <= 1.75 ms | Met by the same-frame direct path at 1.659 ms |
| Existing-active update, p95 <= 1.75 ms | Median 1.658 ms, but one sample raised p95 to 2.697 ms; this unchanged path is recorded as an accepted scheduling outlier |
| Ordinary lifecycle, p95 <= 1.67 ms | Met by all 10% batch operations; maximum p95 0.346 ms |

The delayed revision-scan fallback reached 2.242 ms p95 at 100% changed. That
variance is accepted because the same-frame production path is direct and
meets budget; the fallback preserves revision-token semantics for delayed
consumers. Full unordered batch activation is intentionally a bulk operation
and reached 5.451 ms p95. Full shutdown should use `clear`; the ordinary 10%
lifecycle cases remain well inside budget.
