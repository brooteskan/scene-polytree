# Behavior-to-test manifest

## Reading this manifest

The immutable behavioral authority is Wozzits commit
`0ca3377dd5d9472a5a73426646026f2b085994e1`, reproduced by the `algo` and
`polytree` `v0.0.1-wozzits-baseline` tags. Baseline behavior and current
post-extraction behavior are intentionally distinguished.

Status values mean:

- **Covered**: a focused test directly asserts the contract.
- **Partial**: tests assert part of the contract; the source supplies the rest.
- **Observed**: source behavior is clear but lacks a focused assertion.
- **Gap**: the contract is unsafe or insufficiently specified and must not be
  guessed by a later implementation.
- **Current**: evidence added after the immutable baseline.

Algorithms themselves do not allocate unless a callable or sink does so.
Polytree allocation is listed explicitly because traversal and construction
own temporary or persistent storage.

## Historical `algo.h`

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `algo.h` | `for_each` | Buffer index order `[0,count)` | None | Visits every element; no early termination | `Algo.ForEachVisitsAllElements`, `AlgoSpec.ForEachIsDeterministicOrder`, empty/count tests | **Covered** |
| `algo.h` | `transform` | Input buffer order | None | Ignores the boolean returned by `out.push`; evaluates the full input even after capacity is exhausted | `Algo.TransformProducesCorrectOutput`, `AlgoSpec.TransformRespectsOutputCapacity`, zero/overflow tests | **Partial**: bounded result is tested; post-rejection evaluation count is source-observed only. |
| `algo.h` | `reduce` | Deterministic left-to-right | None | Always consumes the input; returns the final accumulator | `Algo.ReduceIsDeterministic`, `AlgoSpec.ReduceDoesNotMutateBuffer` | **Covered** |
| `algo.h` | `filter` | Retained values preserve input order | None | Ignores failed `out.push`; predicate continues over the full input | `AlgoSpec.FilterKeepsOnlyMatchingElements`, `FilterPreservesOrder`, capacity/empty/all-rejected tests | **Partial**: post-rejection predicate count is not directly asserted. |
| `algo.h` | `apply` | Operations invoked left-to-right | None | Each operation receives the original input and shared output; it is not a pipeline | `AlgoApplySpec.OpsExecuteInOrder`, expected failure `MultipleOpsExecuteSequentially`, current `AlgoBaseline.MultipleApplyOperationsShareOriginalInputAndBoundedOutput` | **Covered + Current**: the legacy test expectation is deliberately false. |

## Historical `ops.h`

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `ops.h` | `map` / `map_t::operator()` | Input buffer order | Callable stored by value; no dynamic allocation by adapter | Ignores failed `out.push` and consumes full input | `AlgoApplySpec.SingleMapIdentity`, input mutation/order/determinism tests | **Partial** |
| `ops.h` | `filter` / `filter_t::operator()` | Retained values preserve input order | Callable stored by value | Ignores failed `out.push` | No direct `ops::filter` test | **Gap** |
| `ops.h` | `reduce_op` / `reduce_t::operator()` | Left-to-right | Callable stored by value | Always consumes input and returns accumulator | No direct `reduce_op` test | **Gap** |
| `ops.h` | Callable ownership | Construction forwards into a stored callable typed as `F` | No adapter allocation | Lvalue and move-only callable support are not characterized | No focused test | **Gap** |

## Baseline `next.h`

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `next.h` | `ReadableRange`, `CanTransformInto` | N/A | None | Compile-time constraints only | `WorksWithStdVectorAsRange`, function-pointer transform | **Partial**: proxy ranges and reference preservation are not covered. |
| `next.h` | `transform` | Range order | None | Stops on first rejected `out.push`, but returns `void` and cannot report truncation | `Transform_Basic`, `Transform_TruncatesOnOverflow`, `Transform_StopsOnOverflow` | **Partial**: baseline tests do not count callable evaluations. |
| `next.h` | `filter` | Accepted values preserve range order | None | Skipped values continue; first rejected accepted value stops processing; returns `void` | `Filter_Basic`, stateful predicate, direct/filter-adapter equivalence | **Partial** |
| `next.h` | `reduce` | Left-to-right | None | Always consumes input and returns accumulator | `Reduce_Basic`, `Reduce_IsDeterministic`, transform/reduce consistency | **Covered** |
| `next.h` | `map` / `map_t` | Range order | Callable stored by value | Delegates to baseline `transform`; truncation is hidden | Fused map/filter and composition tests | **Partial** |
| `next.h` | `filter` factory / `filter_t` | Accepted values preserve range order | Callable stored by value | Delegates to baseline `filter`; truncation is hidden | `FilterT_Basic`, equivalence/order/stateful tests | **Covered** for normal execution; overflow reporting remains absent. |
| `next.h` | `map_t \| filter_t` / `map_filter_t` | Map then predicate per input, preserving accepted order | Stores two callables; no intermediate buffer | Stops when output rejects an accepted transformed value; returns `void` | `MapFilter_FusedMatchesBaseline`, order, nonlinear predicate, overflow policy | **Covered** for values/order; truncation result unavailable. |
| `next.h` | Other operation composition | N/A | N/A | Baseline supports only map-then-filter fusion | `MapT_Then_FilterT_Composition` | **Observed** limitation. |

## Baseline `pipeline.h`

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `pipeline.h` | Header self-containment | N/A | N/A | Baseline header omits direct tuple/invoke/type-trait includes | Scene contract originally supplied transitive standard includes | **Gap** at baseline; dependency workaround is documented. |
| `pipeline.h` | `pipeline_t::operator()` | Input range order; stages in tuple order | Tuple storage only | Stops at first `apply_all == false`; returns `void` | `Pipeline.TruncatesOnOverflow`, `PreservesOrder` | **Covered** for stop behavior; result is hidden. |
| `pipeline.h` | `apply_all` | One value through every stage | None | Returns final sink continuation boolean; filters return true for skipped values | Polytree `as_sink` early-termination tests; current direct `ApplyAllRetainsBooleanSinkContract` | **Partial + Current** |
| `pipeline.h` | `map` factory / map stage | Stage order | Callable stored in tuple | Sink rejection propagates through `apply_all` | `Pipeline.MapBasic` | **Covered** |
| `pipeline.h` | `filter` factory / filter stage | Accepted order preserved | Callable stored in tuple | Rejected predicate skips without terminating; rejected sink terminates | `Pipeline.FilterBasic`, `FilterSkipsCorrectly` | **Covered** |
| `pipeline.h` | Pipeline concatenation | Left pipeline stages precede right stages | Tuple concatenation; no dynamic allocation by API | Continuation propagates across concatenated stages | Multi-stage tests | **Partial**: pipeline-to-pipeline concatenation lacks a dedicated baseline test. |
| `pipeline.h` | Map-map composition | First map then second map | Composed callable stored by value | Final sink boolean propagates | `Pipeline.MapMap_Fusion` | **Covered** |
| `pipeline.h` | Filter-filter composition | First predicate short-circuits second | Combined callable stored by value | A false predicate skips value without terminating | No direct filter-filter test | **Gap** |
| `pipeline.h` | Map-filter-map specialization | Map, predicate, final map | Fused callable tuple; no intermediate buffer | All-filtered completes; sink rejection terminates | `MapFilterMap_Basic`, matches/order/truncation/all-filtered tests | **Covered** |

## Current canonical `algo::next`

These rows describe `algo` commit
`ea2e45f8c19ebe7deb44c85c91179e12fac57154`, not the Wozzits baseline.

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `next.h` | `execution_status` / `was_truncated` | N/A | None | Two states only: `completed` and `truncated`; the helper is true only for `truncated` | All `AlgoNextStatus` result assertions; migrated polytree `PolytreePipelineSink` | **Current** |
| `next.h` | `ReadableRange`, `CanTransformInto`, `PipelineOperation` | N/A | None | Compile-time constraints reject incompatible ranges, sinks, transforms, and pipeline operands | Compilation of vector ranges, function pointers, direct operations, and composed operations | **Current/Partial**: negative and proxy-range compile tests are absent. |
| `next.h` | Range execution status | Preserves baseline input/stage ordering | None | Returns `completed` when all inputs are handled and `truncated` only when an accepted output is rejected | `TransformReportsCompleted/Truncated`, `ExactCapacityIsCompleted`, `EmptyInputIsCompleted`, filter status test | **Current** |
| `next.h` | Evaluation after rejection | Stops at the first rejected output | None | No later input is evaluated | `TransformStopsAtFirstRejectedValue` | **Current** |
| `next.h` | Arbitrary map/filter composition | Declared left-to-right stage order | Tuple storage; no intermediate ranges | Truncation propagates through longer and fused pipelines | `LongerPipelineReportsTruncation`, `ComposedPipelinesPreserveStageOrder`, fused tests | **Current** |
| `next.h` | Per-element traversal adapter | Stage order | None | `pipeline_t::apply_all` retains boolean sink contract | `ApplyAllRetainsBooleanSinkContract`; migrated Wozzits/polytree graph tests | **Current** |
| `next.h` | `reduce` | Left-to-right | None | No rejecting sink, so no execution status is needed | Baseline reduce tests | **Current contract unchanged** |

## Incidental static-polytree support contracts

The baseline package contains the complete `static_dag.h`, but DAG operations
are not intended for extraction as polytree API and are therefore not assigned
behavioral rows here. The following facilities are the actual static-polytree
dependency closure.

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `concepts.h` | `Sink<S, V>` | N/A | None | Requires `sink.push(value)` convertible to `bool` | All sink traversal and `as_sink` test translation units compile through this constraint | **Partial**: no negative compile test. |
| `static_dag.h` | `NodeHandle`, `INVALID_NODE` | Dense unsigned 32-bit handle space | None | Invalid node is `0xFFFF'FFFFu`; no runtime validity checking is implied | Invalid-edge, root-parent, sibling-boundary, and path tests | **Covered** for values used by polytree. |
| `static_dag.h` | `EdgeHandle`, `INVALID_EDGE` | N/A | None | Unsigned 32-bit alias and `0xFFFF'FFFEu` sentinel | No static-polytree consumer or test | **Observed/incidental**: imported by the whole header but not required by static polytree. |
| `static_dag.h` `detail` | `carve` storage-layout helper | Advances spans in declared buffer-layout order | Uses caller-provided storage only | Aligns each span, asserts rather than returns an error on insufficient storage | Successful static build and payload-query tests exercise it indirectly | **Partial/internal**: alignment stress and release-mode bounds behavior are gaps. |

## Static polytree construction and queries

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `static_polytree.h` | `add_node` | Handles are sequential insertion indices | Builder vectors may grow | Always adds and returns a handle; allocation failure follows standard exception behavior | `AddNodeReturnsSequentialHandles` | **Covered** |
| `static_polytree.h` | `add_edge` | Records insertion order before build | Builder edge vector may grow | Rejects invalid handles, self-edge, and a second parent without changing `parent_of`; does not reject a longer cycle until build | self-loop, invalid-handle, second-parent tests | **Covered** for listed failures; atomicity beyond `parent_of` is source-observed. |
| `static_polytree.h` | `build` validation | Produces a parent-before-child Kahn order | Allocates temporary adjacency/order vectors and one owning byte buffer | Returns `nullopt` for a detected cycle; consumes builder by value | valid build, cycle, single-node tests | **Partial**: empty tree and allocation failure are not directly tested. |
| `static_polytree.h` | Child/root tie ordering during build | Edge sort compares only parent; topological worklist is LIFO | As above | Exact order among equal-parent edges and multiple roots is not a portable guarantee | wide-root and child-query tests observe useful results but do not pin all ties | **Gap**: issue #5 freeze must choose and test deterministic ordering. |
| `static_polytree.h` | Static storage/lifetime | Node/edge payloads are moved into a single byte allocation | One persistent byte buffer | View spans remain valid while owning `PolytreeStorage` and buffer address remain valid | node/edge data tests | **Gap** for non-trivial payload destruction, copy/move semantics, and alignment stress. |
| `static_polytree.h` | `node_count`, `edge_count` | N/A | None | Constant-time span-size queries | valid build, wide/chain fixtures and contract test | **Covered** |
| `static_polytree.h` | `node_data` | Handle-indexed | None | Returns const reference; invalid handle is unchecked | `NodeDataIsPreserved` | **Partial**; invalid-handle query is a gap. |
| `static_polytree.h` | `children`, `child_count`, `child_at` | Stored child CSR order | None | `child_at` returns `INVALID_NODE` out of range; other invalid handles unchecked | children tests; `ChildCount`, `ChildAtInRange/OutOfRange` | **Covered** except invalid parent handle. |
| `static_polytree.h` | `parent` | Direct handle lookup | None | Roots return `INVALID_NODE`; invalid handle unchecked | `RootHasInvalidParent`, `ParentLookupIsCorrect` | **Covered** except invalid query. |
| `static_polytree.h` | `parent_edge_data` | Parallel to parent array | None | Root receives default edge payload; invalid handle unchecked | `ParentEdgeDataIsPreserved` | **Partial**: root/default behavior not asserted. |
| `static_polytree.h` | `outgoing_edge_data`, `child_edge_data_at` | Parallel to child order | None | Indexed helper returns null out of range | child-edge-data in/out-of-range tests | **Covered** |
| `static_polytree.h` | `is_root`, `is_leaf` | N/A | None | Constant-time; invalid handle unchecked | `IsRootIsLeafAreCorrect` | **Covered** |
| `static_polytree.h` | `has_edge` | Linear scan in child order | None | Returns false when child is absent; invalid source unchecked | `HasEdgeIsCorrect` | **Covered** for valid handles. |
| `static_polytree.h` | `topo_order` | Parent precedes child; exact tie order not specified | None after build | Returns cached span | parent-before-child and wide-root-first tests | **Partial** |

## Static-polytree traversal and helpers

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `static_polytree.h` | Visitor `dfs` | Root first; stack reverses stored sibling push order | Allocates visited vector and stack | Visitor cannot terminate early; invalid root unchecked | `DFSVisitsAllNodes` | **Partial**: exact sibling order and invalid root are gaps. |
| `static_polytree.h` | Visitor `bfs` | Root first and breadth-first in stored child order | Allocates visited vector and queue | Visitor cannot terminate early; invalid root unchecked | BFS all-nodes/level-order tests | **Covered** for valid hierarchy order constraints. |
| `static_polytree.h` | Visitor `walk_ancestors` | Immediate parent to root | None | Stops at root; invalid node unchecked | leaf/root/mid ancestor tests | **Covered** for valid handles. |
| `static_polytree_algo.h` | Sink `dfs`, `bfs` | Same traversal forms as visitor versions | Allocate visited/work vectors | Stop immediately when `sink.push` returns false | `AsSinkEarlyTerminationAbortsBFS`, DFS/BFS pipeline tests | **Partial**: direct DFS rejection count lacks a focused polytree test. |
| `static_polytree_algo.h` | Sink `walk_ancestors` | Immediate parent to root | None | Stops on sink rejection | `AncestorWalkSinkEarlyTermination`, pipeline filter test | **Covered** |
| `static_polytree_algo.h` | `dfs_materialize`, `bfs_materialize` | Respective traversal order | Traversal work vectors plus caller scratch | Silently returns a shortened span when scratch rejects; no status | contains-all and insufficient-scratch tests | **Covered** for truncation size; exact DFS sibling order remains unspecified. |
| `static_polytree_algo.h` | `ancestors_materialize` | Immediate parent to root | Caller scratch only | Silently truncates | leaf/root/truncation tests | **Covered** |
| `static_polytree_algo.h` | `roots_materialize` | Ascending dense handle scan | Caller scratch only | Silently truncates | single-root and forest tests | **Partial**: insufficient scratch lacks a direct test. |
| `static_polytree_algo.h` | `ancestors_materialize_root_first` | Root to immediate parent | Caller scratch only | Silently truncates before reversal | root-first/root/truncation tests | **Covered** |
| `static_polytree_algo.h` | `child_ordinal` | Stored sibling order | None | Roots/non-children return `UINT32_MAX` | root/non-root tests | **Covered** |
| `static_polytree_algo.h` | `previous_sibling`, `next_sibling` | Stored sibling order | None | Returns `INVALID_NODE` at boundaries | sibling tests | **Covered** |
| `static_polytree_algo.h` | `depth` | Parent chain | None | Root is zero; invalid/cyclic input unchecked | `Depth` | **Covered** for valid nodes. |
| `static_polytree_algo.h` | `subtree_size` | DFS internally; result independent of order | DFS work allocation | Counts complete valid subtree | `SubtreeSize` | **Covered** |
| `static_polytree_algo.h` | `find_child_if` | Stored child order, first match wins; predicate receives edge and ordinal | None | Returns `INVALID_NODE` when no match | predicate and ordinal tests | **Covered** |
| `static_polytree_algo.h` | `walk_path_from_root` | Root-to-node edges | Caller scratch | Returns false for `INVALID_NODE` or insufficient scratch; root succeeds with zero visits | path, root, invalid, insufficient-scratch tests | **Covered** |
| `static_polytree_algo.h` | `as_sink` | Pipeline stage order inside traversal order | Adapter itself allocates nothing | Converts pipeline/sink rejection to traversal `false` | BFS/DFS filter and early-termination tests | **Covered** at baseline and after migration to `algo::next`. |

## Current `polytree` v0.2.0

These rows describe `polytree` tag `v0.2.0`, based on the stabilized static
contract at commit `0a84af11df7eb0a016009d2f93cfc0ca272e507d` and the mutable/freeze
implementation at commit `6a7e2ee401cb755a40512ab7b4630becf2eb9950`. The historical rows above remain the
Wozzits baseline comparison rather than the current API contract.

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `static_polytree.h` | `evaluation_plan` and cached views | Topological order preserves characterized LIFO selection; reverse is exact reverse; roots are ascending handles; dependency levels increase by depth and retain topological order within a level | Cached in the compact static allocation; view creation allocates nothing | Empty forests expose zero nodes/levels with offsets `{0}`; out-of-range levels are empty | exact plan, forest, empty, deep-chain, and wide-tree tests | **Current** |
| `static_polytree.h` | `depth_first_order`, `breadth_first_order`, `ancestor_order` | DFS retains reverse stored-sibling order; BFS retains stored-sibling breadth order; ancestors are immediate-parent first | Owning contiguous vectors, respectively `O(subtree)`, `O(subtree)`, and `O(depth)` | Full valid-tree orders; invalid handles remain unchecked | exact-order, visitor parity, 10,000-node chain, and 4,096-child tests | **Current** |
| `static_polytree.h` / `static_polytree_algo.h` | Visitor and sink adapters | Consume the same canonical owning order | Canonical-order allocation; adapters add none | Visitors complete; sinks return `algo::next::execution_status` and stop pushing at first rejection | direct BFS/DFS/ancestor status and pipeline-sink propagation tests | **Current** |
| `static_polytree_algo.h` | Scratch materializers | Preserve canonical order and the baseline root-first truncated prefix | Canonical-order allocation plus caller scratch | `PolytreeMaterialization` exposes the written span and explicit completed/truncated status; exact capacity completes | complete, truncated, empty-root, forest-root, and root-first tests | **Current** |
| `mutable_polytree.h` | Stable identity and ordered insertion, reparent, detach, payload replacement, and subtree deletion | Explicit root and child order | Authoring records and payloads allocate independently of static storage | Logical mutation failures are atomic; cycle and one-parent invariants are enforced at mutation boundaries; successful mutations increment the source revision once | identity, ordering, cycle, invalid ordinal, deletion, payload, and deep-chain tests | **Current** |
| `polytree_freeze.h` | Validation, deterministic freeze, and bidirectional identity maps | Canonical root-first preorder assigns dense handles; child insertion order is preserved; cached static plan ordering is unchanged | Compact topology allocation, owned identity maps, and reusable caller workspace are reported separately | Invalid topology returns an explicit error; unchanged input freezes repeatedly to equal observable topology and maps | exact forest, mapping, repeated-freeze, stale-revision, empty, and deep-chain tests plus `scene_polytree.contracts` | **Current** |
| Control-flow policy | Static-polytree implementation and tests | N/A | N/A | CMake policy test rejects handwritten C++ loops; legacy `static_dag.h` is outside the current polytree dependency closure | `polytree.no_handwritten_loops` | **Current** |
| `evaluation_plan_view` | Legacy type spelling for the generic `PolytreeEvaluationPlan` | Exactly the generic cached plan | None | Out-of-range dependency levels are empty | `scene_polytree.contracts` consumes `evaluation_plan` directly from frozen topology | **Legacy alias**; generic field names are authoritative. |

## Wozzits scene behavior reference

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `transform_node.h` | Default node state | N/A | None | Local/world identity, invalid update frame, zero flags, static motion | Fixture assumptions and `AllNodesDirtyAfterBuild` | **Partial** |
| `scene_graph.h` | `compute_world` | Parent multiplied by local | None | Delegates to Wozzits `wz::math::mul` | root/child/grandchild propagation tests using translations | **Partial**: general rotation/scale convention is not characterized here. |
| `scene_graph.h` | `is_dirty` | N/A | None | Dirty iff `last_updated_frame == INVALID_FRAME` | dirty tests | **Covered** |
| `scene_graph.h` | `propagate_all` | Cached topological order, parent before child | None in algo; no traversal work allocation | Computes world transforms but deliberately does not mark nodes clean | four propagation tests | **Partial**: clean-state behavior is source-observed. |
| `scene_graph.h` | `mark_dirty` | N/A | None | Mutates payload through `const_cast`; invalid handle unchecked | No direct test | **Gap** |
| `scene_graph.h` | `set_local` | N/A | None | Sets local and marks node dirty through `const_cast` | `SetLocalMarksDirty`, later update tests | **Covered** for valid node. |
| `scene_graph.h` | `collect_dirty_roots` | Topological order | Caller scratch only | Selects dirty node whose parent is clean or absent; silently truncates scratch; frame parameter unused | all-dirty and one-dirty-root tests | **Partial**: forest, insufficient scratch, and ordering ties are gaps. |
| `scene_graph.h` | `update_static` | Dirty-root input order; BFS within each subtree | BFS allocates visited/queue per root | Updates entire selected subtree and marks it clean; no overlap deduplication across supplied roots | full, clean, and partial-subtree tests | **Partial** |
| `scene_graph.h` | `build_animated_list` | Topological order | Caller scratch only | Selects `MotionType::Animated`; silently truncates | animated-list test | **Partial**: insufficient scratch is a gap. |
| `scene_graph.h` | `update_animated` | Supplied animated-list order | None in algo | Recomputes only listed nodes and marks them clean; caller must ensure parent worlds are valid | animated propagation/clean tests | **Covered** for the fixture contract. |
| `scene_graph.h` | Mutable scene payload boundary | N/A | N/A | Static topology exposes const payload spans; scene mutates through `const_cast` | Source observation | **Gap/intentional correction**: scene-polytree must use explicit mutable state separate from topology. |

## Mutable freeze integration

Issue #5 is represented by the v0.2.0 dependency contract and the current
scene contract test:

1. Child and root order compile deterministically into the existing cached
   static evaluation plan.
2. `StableNodeId` is distinct from dense runtime `NodeHandle`.
3. Mutation failure and range/reference invalidation rules are documented in
   polytree's `MUTABLE.md` and covered by tests.
4. Freeze returns both identity directions and its source revision.
5. Repeated freeze compares all generic plan views and identity maps.
6. `basic_scene` composes mutable or static topology with separate scene state;
   runtime topology remains immutable and no `const_cast` pattern is retained.

## Current motion integration

Issue #7 adds the following current scene-polytree contracts. These are not
claims about the historical Wozzits motion implementation.

| Source/API | Operation or observable contract | Ordering | Allocation | Termination/error behavior | Test evidence | Status/notes |
|---|---|---|---|---|---|---|
| `active_motion_set` | Sparse registration, update, stationary removal, deactivation, clear, and validated batch application | Ascending runtime `NodeHandle`, independent of distinct-node registration order; repeated updates to one node are applied in input order | Sorted vector grows on demand; storage capacity is observable | Invalid handles reject before a batch mutation; stationary state removes rather than disables a record; absent deactivation and repeated clear are no-ops | `scene_polytree.motion` registration, ordering, and lifecycle case | **Current** |
| `fixed_step_sequence` | Supplies an exact duration and monotonic tick to policy integration | Tick increases once after each successful centralized evaluation | None | Non-positive steps, exhausted ticks, topology/state mismatch, and revision exhaustion do not mutate scene or tick | `scene_polytree.motion` validation and deterministic replay cases | **Current** |
| `advance_motion_scene` | Integrates active locals, then delegates affected-subtree world propagation to the core dirty planner | Active handle order followed by cached topological transform order | Caller workspaces retain `O(active)` motion scratch plus core transform-planning scratch | Preflight errors return before mutation; success reports directly integrated and propagated changed nodes | `scene_polytree.motion` centralized integration and three-tick deterministic replay cases | **Current** |
| Articulated tank fixture | Same hull/turret/gun asset and scene representation accept player and AI intent | Central motion and transform ordering only; controllers do not traverse | Private example support only; no installed tank API | Equivalent controllers remain transform-equivalent across repeated ticks; authored turret/gun pivots are preserved; stationary intent empties active storage and a later tick invokes no integration/composition callbacks | `scene_polytree.example.articulated_tank`, `scene_polytree.tank_example` articulation and controller replay cases | **Current, acceptance-complete integration fixture** |
| Motion storage benchmark | Compares production sparse records with dense state-plus-active-byte storage | Evenly spaced active handles; same checksum per candidate | Reports capacity bytes at 1K, 10K, and 100K nodes | Standalone Release benchmark; not a pass/fail performance gate | `scene_polytree_motion_storage_benchmark` and recorded results | **Current measurement** |
