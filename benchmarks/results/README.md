# Recorded benchmark results

Files in this directory use the schema emitted by
`scene_polytree_performance_benchmark`. They are JSON Lines rather than one
large JSON document so tools can stream measurements from long runs.

The metadata record identifies the source revision and whether the checkout
was dirty. A dirty result is useful during implementation but should be
replaced by a clean-revision run before a release claim is made. Timings are
machine observations, not portable guarantees; compare identical schema,
suite, phase, shape, size, ratio, compiler mode, and sample policy.

Hierarchy-oriented measurements use the asset-agnostic `instance_count` field
and `three_node_hierarchy_forest` shape label.

`windows-msvc-baseline-20260830.jsonl` is the issue #9 development baseline.
It contains three samples for every full-preset case and records the exact
machine and command in its first line.
