# Parallel execution recommendation

## Recommendation

Prototype dependency-level CPU tasks for large, wide transform-composition
batches. Do not prototype GPU execution yet. Before parallelizing sparse frame
updates, remove the unconditional full-scene planning and synchronization
scans and add batch active-set maintenance.

This order follows the measured bottlenecks:

- a 100,000-node clean scene still spends 0.58 to 1.64 ms planning because the
  planner scans every node;
- a 30,000-node sparse motion step spends roughly 0.27 to 0.81 ms even though
  only 0.1% to 10% of actors move;
- 30,000-node changed selection spends 0.12 to 0.16 ms before sparse writes;
  and
- unordered sparse-vector lifecycle updates can cost hundreds of milliseconds,
  far more than transform composition.

Parallel composition is justified only when changed work is already large.
The 100,000-node wide and balanced cases expose maximum dependency levels of
99,999 and 65,536 nodes and spend 2.7 to 4.3 ms in the equivalent sequential
kernel. The chain exposes width one and cannot benefit from dependency-level
parallelism.

## Minimum CPU task grain

The full dirty-root kernels cost approximately 27 ns per node for the wide
shape and 43 ns per node for the balanced shape on the reference machine. A
task should carry at least four times its dispatch overhead in useful work.

| Assumed dispatch overhead | Wide minimum | Balanced minimum |
| ---: | ---: | ---: |
| 1 us | 149 nodes | 94 nodes |
| 5 us | 746 nodes | 469 nodes |
| 10 us | 1,493 nodes | 938 nodes |
| 25 us | 3,731 nodes | 2,345 nodes |

Use 2,048 changed nodes as the initial minimum batch size for a task prototype
and make it configurable. At 100,000 nodes this yields about 48 wide batches or
32 batches in the widest balanced level, enough to exercise the 16 hardware
threads while remaining above a 10 us dispatch assumption. Do not create tasks
for a chain level, a level below the grain, or a complete operation below about
40 us of measured sequential work.

The prototype must consume cached dependency levels and one immutable affected
mask. It must preserve parent-before-child level barriers, stable result
ordering, one world revision per logical batch, stale-plan validation, and the
existing externally synchronized mutation boundary. Parallel workers should
write disjoint world-transform records; revision publication and changed-node
materialization remain a deterministic serial commit.

## Planning and synchronization before more threads

For sparse workloads, first prototype an explicit dirty-root/active-subtree
frontier that can produce the affected mask without scanning clean nodes. The
frontier must be invalidated or remapped at freeze and reparent boundaries and
must retain the current rule that a dirty ancestor owns its affected subtree.

Similarly, expose the already ordered changed batch from evaluation directly
to same-frame synchronization where lifetime permits. Keep
`changed_transform_nodes_since` as the revision-token fallback. This avoids a
second full topology scan without changing the meaning of revision-based
queries.

Active motion needs a sort-and-merge batch update operation. Validate all
handles first, stable-sort or require sorted updates, coalesce repeated node
updates with last-write-wins semantics, and merge once with the current sorted
records. The operation must preserve deterministic `NodeHandle` order and
all-or-nothing validation. Parallelizing individual vector insertions or
erasures would retain the measured quadratic movement cost and add contention.

## SIMD and data layout

A SIMD prototype is reasonable after the scheduling and maintenance changes,
but only for large homogeneous level batches. The current
`transform_record<rigid_pose>` is array-of-structures storage containing local
and world poses, revisions, and dirty state. SIMD-friendly kernels should use
separate contiguous streams for translation, rotation, parent handles, and
affected flags, or construct non-owning structure-of-arrays views over a
consumer-selected transform representation.

Keep revisions and dirty flags out of vector math lanes. Motion integration
should generate a contiguous local-update batch, then publish revisions in a
deterministic serial step. Policy customization must remain available; a SIMD
path is an optional policy/executor specialization, not a new mandatory
transform representation.

## GPU decision

No current operation justifies a GPU prototype:

- the largest measured transform kernel is 2.7 to 4.3 ms for shapes with
  usable width and 3.7 ms for a dependency-serial chain;
- scene planning and synchronization still require CPU-visible masks,
  revisions, and changed-node results;
- runtime state is CPU-resident array-of-structures data; and
- sparse workloads are dominated by full scans and active-set maintenance,
  not arithmetic throughput.

Reconsider GPU execution only if a measured workload remains above 100,000
changed nodes for many consecutive frames, transforms and parent data can stay
resident on the device, downstream consumers can use device-resident worlds,
and readback is limited to a compact changed/result set. A future experiment
must separately measure upload, dispatch, barriers, readback, and engine
synchronization; CPU kernel time alone is not a GPU business case.

## Prototype order

1. Batch sort-and-merge active-set updates and measure unordered lifecycle
   workloads again.
2. Add an incremental affected frontier and direct evaluated-batch
   synchronization path.
3. Prototype dependency-level CPU tasks with a 2,048-node initial grain and a
   deterministic serial commit.
4. Evaluate an optional structure-of-arrays/SIMD policy on the same fixtures.
5. Revisit GPU execution only if the residency and sustained-work criteria are
   demonstrated.

Every prototype remains subject to the repository's no-handwritten-loop
policy and must match the sequential evaluator exactly before timing results
are accepted.
