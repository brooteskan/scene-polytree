# Transform evaluation contract

## Transform policy

Transform evaluation is parameterized by a `TransformPolicy`. A policy names
its `transform_type` and supplies this operation:

```cpp
Transform compose(const Transform& parent_world, const Transform& local) noexcept;
```

Composition always receives parent world first and child local second. A root
does not invoke the policy; its world value is copied from its local value.
Transforms must be copy constructible and support non-throwing copy and move
assignment. No equality operator, matrix representation, engine type, or
identity-construction operation is required.

## Authoring and runtime forms

`basic_authoring_scene<NodeData, EdgeData, Transform>` owns an ordered
`MutablePolytree` and local transforms keyed by `StableNodeId`. Its mutation
methods are the transform-aware boundary:

- insertion creates a local-transform record;
- `set_local` advances the scene revision;
- changing stable parent identity advances the scene revision;
- same-parent reordering and edge/node payload replacement do not invalidate
  transforms; and
- subtree erasure removes the corresponding local records.

The class exposes topology and transform state only through const access.
Calling generic mutable-polytree operations on separately owned topology is
not equivalent to mutating a transform-aware scene.

`freeze_scene` delegates topology compilation to `polytree::freeze`. The first
freeze creates dense records in runtime-handle order and marks every node
dirty. The incremental overload accepts the previous runtime snapshot and
remaps by stable identity. It preserves a world value only when the node
survives, its local revision is unchanged, and its stable parent identity is
unchanged. New nodes and nodes whose parent changed become dirty. Descendant
invalidation is derived later, so reparenting does not walk the subtree. The
new runtime state reconstructs its direct-dirty frontier from the remapped
records, so no frontier handle survives a freeze boundary and a reparented node
owns its affected subtree in the next plan.

Runtime-only local edits belong to that runtime snapshot. A later incremental
freeze treats authoring local state as authoritative.

## Dirty selection and evaluation

`make_transform_evaluation_plan` consumes the immutable polytree's cached
topological order. Runtime state maintains an exact frontier of directly dirty
nodes. The workspace caches topological ranks, subtree ranges, subtree sizes,
and dependency levels for one topology. This metadata is built in `O(N)` on
the first plan for that topology and reused afterward.

Planning sorts the `D` direct-dirty candidates by topological rank, discards
any candidate already owned by an earlier dirty ancestor, and appends that
owner's cached contiguous subtree. It preserves the rule:

```text
affected(node) = directly_dirty(node) OR affected(parent(node))
```

An affected node with an unaffected parent is a dirty root. The plan exposes
dirty roots and affected nodes in cached topological order. After metadata is
warm, a clean plan is `O(1)` and a normal dirty plan is
`O(D log D + K)` worst-case, or `O(D + K)` when edits already arrive in
topological order, where `K` is the affected-node count. A conservative `O(N)`
fallback remains for a foreign polytree whose cached topological order does not
store subtrees contiguously. The reusable workspace owns `O(N)` metadata plus
result scratch, and runtime state retains up to `O(N)` handles for its direct
frontier. Both retain capacity between calls; repeated plans within capacity
allocate zero bytes.

`evaluate_transforms` copies local to world for roots and invokes the policy
for other affected nodes. It clears direct dirty state and gives every node in
the batch the same new world revision. The returned `changed_nodes` span is
exactly the evaluated order; “changed” means dependency-driven recomputation,
not numerical inequality.

`changed_transform_nodes_since` filters cached topological order by
`world_revision > token`. Its returned span borrows the caller's scratch
vector. Same-frame consumers should instead use the ordered `changed_nodes`
span returned by evaluation while its workspace lifetime is still valid. This
avoids a second full-topology scan. Revision-token selection remains the
fallback for delayed or independently scheduled synchronization.

## Dependency-level CPU execution

The overload accepting `cpu_task_executor` executes independent nodes within
cached dependency levels. The executor owns a persistent worker pool and uses
a configurable `transform_execution_options::minimum_task_grain`, which
defaults to 2,048 changed nodes. A complete operation below the grain, levels
below the grain, chains, and an executor with no workers stay on the caller
thread.

The affected generation mask and packed level batches are immutable while
workers run. Workers compose disjoint world records, each dependency level is
a parent-before-child barrier, and world revisions, dirty flags, and the
ordered result are published afterward in a deterministic serial commit. The
parallel result is exactly the same topological `changed_nodes` order and uses
the same single world revision as the sequential evaluator. Workspace and
executor capacity are reusable; steady-state dispatch does not allocate.

## Partial evaluation

The partial-plan overload accepts a subset of the current dirty roots. Every
selected handle must be a unique dirty root reported by a current full plan.
Membership and uniqueness validation is `O(S)` for `S` selected roots. A
directly dirty descendant beneath another dirty node is not independently
selectable because its parent world may still be stale. Unselected dirty roots
remain pending. Each non-empty partial execution receives its own world
revision.

## Revisions and lifetime

Local and world revisions share one monotonic runtime clock. A local edit
stores its edit revision in `local_revision`; a non-empty evaluation stores its
batch revision in `world_revision`. Revision exhaustion is reported before the
operation mutates state.

Evaluation plans are one-shot borrowed views into
`transform_evaluation_workspace`. A plan becomes stale after:

- any local edit or explicit runtime dirty mark;
- any evaluation, including an empty evaluation;
- moving or replacing the topology owner; or
- reusing its workspace to build another plan.

Execution validates the topology address, state revision, mutation and
evaluation generations, and workspace generation. Changed-node views remain
valid only until their workspace is reused or destroyed.

## Thread safety

Static polytree topology and completed transform records may be read
concurrently when their owners remain alive. Authoring mutation, planning into
a workspace, runtime local edits, and evaluation require external exclusive
synchronization. One workspace may not be shared by simultaneous planners.
One `cpu_task_executor` may not be used reentrantly. The parallel overload calls
`TransformPolicy::compose` concurrently, so the supplied policy must either be
stateless or synchronize any shared mutable state. The sequential overload
remains available for policies that do not meet that requirement.
