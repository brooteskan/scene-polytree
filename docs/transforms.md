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
invalidation is derived later, so reparenting does not walk the subtree.

Runtime-only local edits belong to that runtime snapshot. A later incremental
freeze treats authoring local state as authoritative.

## Dirty selection and evaluation

`make_transform_evaluation_plan` consumes the immutable polytree's cached
topological order. For each node it computes:

```text
affected(node) = directly_dirty(node) OR affected(parent(node))
```

An affected node with an unaffected parent is a dirty root. The plan exposes
dirty roots and affected nodes in cached topological order. Planning scans all
nodes in `O(N)` time; transform composition is performed for only the `K`
affected nodes. The reusable workspace owns `O(N)` scratch and retains its
capacity between calls.

`evaluate_transforms` copies local to world for roots and invokes the policy
for other affected nodes. It clears direct dirty state and gives every node in
the batch the same new world revision. The returned `changed_nodes` span is
exactly the evaluated order; “changed” means dependency-driven recomputation,
not numerical inequality.

`changed_transform_nodes_since` filters cached topological order by
`world_revision > token`. Its returned span borrows the caller's scratch
vector.

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
Dependency-level views remain available for a future executor, but the core
Phase 5 evaluator is deterministic and sequential.
