# Black-box API test contract for AI agents

Status: test-authoring contract for `scene-polytree` 0.4.x<br>
Language: C++20<br>
Package: `scene-polytree`

This is the complete brief for an AI agent writing consumer-facing tests. The
agent must derive expectations from this contract and public return values,
not from the implementation, existing tests, repository history, or issues.
Unspecified behavior must not be inferred from a passing implementation.

## Isolation model

“Black box” can mean either:

1. **No private source**: the agent receives a read-only installed package,
   including public headers, but no producer checkout, private source, tests,
   build tree, history, or implementation notes. This is the conventional C++
   consumer model.
2. **No implementation at all**: the agent receives only this contract and an
   editable test workspace. A trusted build service compiles submitted tests
   against the package and returns sanitized diagnostics and test results.

Use the second model if public header bodies also count as implementation.
Most of `core` and all of `motion` are templates, so the compiler must read
their definitions. An agent sharing the compiler's filesystem authority can
normally read them too; directory permissions are not an effective boundary.

For strict isolation, keep the SDK and producer checkout outside the agent's
machine/container. A trusted broker must own the build manifest and compiler
invocation; accepting arbitrary agent-authored CMake would permit file reads
during configuration. The broker should accept only test sources and declared
test names. It should run binaries without the SDK mounted, without network,
and in a temporary directory. Return diagnostics from agent-owned files, test
names, exit codes, and sanitizer findings. Filter SDK paths and excerpts,
preprocessor/include traces, object files, and compilation databases.

## Package boundary

Consume the installed package only through:

```cmake
find_package(scene-polytree 0.4 CONFIG REQUIRED)
```

| Imported target | Umbrella include | Contract |
|---|---|---|
| `scene-polytree::core` | `<scene_polytree/scene_polytree.hpp>` | Scenes, authoring, freeze, transform state and evaluation |
| `scene-polytree::motion` | `<scene_polytree/motion/motion.hpp>` | Fixed-step motion; links `core` transitively |
| `scene-polytree::cpu-executor` | `<scene_polytree/cpu_task_executor.hpp>` | Compiled CPU worker pool |

All targets require C++20. `algo`, `polytree` 0.2, and threads are transitive
package dependencies. Tests must not add producer include paths, use the
producer as a subdirectory/FetchContent dependency, or link unexported targets.

The signatures below abbreviate these public `wz::core::graph` dependency
types: `StableNodeId`, `NodeHandle`, `MutationResult<T>`, `MutationError`,
`FreezeWorkspace`, `FreezeError`, `FreezeMetrics`, `FrozenIdentityMap`,
`Polytree<N,E>`, `PolytreeStorage<N,E>`, and `PolytreeEvaluationPlan`.
`StableNodeId` has `valid()` and integral `value`; invalid sentinels are
`INVALID_STABLE_NODE` and `INVALID_NODE`. A successful `MutationResult<T>` is
truthy and has `value()`; a failure has `error()`. Identity maps provide
`runtime_handle(stable_id)` and `authoring_id(runtime_handle)`. Tests may use
dependency queries to observe scene results, but dependency behavior itself
belongs in the `polytree` suite.

## Core API surface

Everything in this section is in `scene_polytree`. Anything in a `detail`
namespace is excluded, even if visible from a public header.

### Generic scene

`basic_scene<Topology, SceneState>` owns the two constructor arguments and
publishes `topology_type`, `state_type`, and const/non-const `topology()` and
`state()` accessors. It has deduction guide
`basic_scene(Topology, SceneState) -> basic_scene<Topology, SceneState>`.
Accessors are `constexpr` and `noexcept`; mutability follows object constness.

### Transform records and state

```cpp
using scene_revision = std::uint64_t;

enum class transform_error {
    none, invalid_node, state_size_mismatch, invalid_scope, stale_plan,
    topology_mismatch, revision_exhausted
};

template<class Transform> struct transform_record {
    Transform local{};
    Transform world{};
    std::uint64_t local_revision{};
    std::uint64_t world_revision{};
    bool dirty{true};
};
```

`transform_state<Transform>` publishes `transform_type` and `record_type` and
has a default constructor plus:

```cpp
explicit transform_state(std::vector<record_type> records,
                         scene_revision revision = {});
std::size_t size() const noexcept;
bool empty() const noexcept;
scene_revision revision() const noexcept;
std::uint64_t mutation_generation() const noexcept;
std::uint64_t evaluation_generation() const noexcept;
std::span<const record_type> records() const noexcept;
bool has_dirty_transforms() const noexcept;
std::span<const NodeHandle> dirty_nodes() const noexcept;
std::size_t dirty_frontier_capacity_bytes() const noexcept;
const record_type& record(NodeHandle) const noexcept;
const Transform& local(NodeHandle) const noexcept;
const Transform& world(NodeHandle) const noexcept;
transform_error set_local(NodeHandle, Transform);
transform_error mark_dirty(NodeHandle);
```

The vector constructor preserves record order and initializes the direct-dirty
frontier from `dirty` records. Indexed reads require a valid handle.
`set_local` and `mark_dirty` validate handles. Each successful call increments
revision and mutation generation, marks the record dirty, and does not create
duplicate frontier entries. `set_local` also changes local and local revision.
At maximum revision they return `revision_exhausted` without mutation. Capacity
queries report retained bytes; exact capacity/growth is not contractual.

`TransformPolicy<Policy>` requires `Policy::transform_type` and:

```cpp
transform_type compose(const transform_type& parent_world,
                       const transform_type& local) noexcept;
```

The transform is copy constructible and non-throwing copy/move assignable.
Roots copy local to world without calling `compose`. Equality, matrix shape,
and identity construction are not required.

### Authoring scene

```cpp
template<class Transform> struct authoring_transform_record {
    Transform local{};
    scene_revision local_revision{};
};
```

`authoring_transform_state<Transform>` publishes `record_type` and:

```cpp
scene_revision revision() const noexcept;
bool revision_available() const noexcept;
bool contains(StableNodeId) const noexcept;
const record_type& record(StableNodeId) const;
const Transform& local(StableNodeId) const;
transform_error set_local(StableNodeId, Transform);
```

`record` and `local` require an existing ID. `contains` returns false for an
invalid/absent ID; `set_local` returns `invalid_node`. A successful edit
increments revision and records it on the local. Exhaustion is non-mutating.

`basic_authoring_scene<NodeData, EdgeData, Transform>` publishes
`topology_type = MutablePolytree<NodeData, EdgeData>`, `state_type`, and
`transform_type`. It is default constructible when its members are and exposes:

```cpp
const topology_type& topology() const noexcept;
const state_type& state() const noexcept;
scene_revision revision() const noexcept;

MutationResult<StableNodeId>
insert_root(NodeData, Transform, std::size_t ordinal = APPEND_CHILD);
MutationResult<StableNodeId>
insert_child(StableNodeId parent, NodeData, EdgeData, Transform,
             std::size_t ordinal = APPEND_CHILD);
transform_error set_local(StableNodeId, Transform);
MutationResult<StableNodeId>
reparent(StableNodeId node, StableNodeId new_parent, EdgeData,
         std::size_t ordinal = APPEND_CHILD);
MutationResult<StableNodeId>
detach_to_root(StableNodeId, std::size_t ordinal = APPEND_CHILD);
MutationResult<std::size_t> erase_subtree(StableNodeId);
MutationResult<StableNodeId> replace_node_data(StableNodeId, NodeData);
MutationResult<StableNodeId> replace_parent_edge_data(StableNodeId, EdgeData);
```

Insertion creates a matching transform record; subtree erasure removes all
matching records. Local edits and stable-parent changes advance scene revision.
Same-parent reorder and node/edge payload replacement do not invalidate
transforms or advance scene revision. Failed operations preserve topology,
state, and revision. Topology/state are const to prevent bypassing this coupling.

### Freeze and runtime scene

`basic_runtime_scene<NodeData, EdgeData, Transform>` publishes
`topology_storage_type`, `topology_type`, `state_type`, `transform_type`, and:

```cpp
basic_runtime_scene(topology_storage_type, FrozenIdentityMap, FreezeMetrics,
                    state_type,
                    std::vector<scene_revision> authoring_local_revisions);
const topology_type& topology() const noexcept;
const topology_storage_type& topology_storage() const noexcept;
const FrozenIdentityMap& identities() const noexcept;
const FreezeMetrics& freeze_metrics() const noexcept;
state_type& state() noexcept;
const state_type& state() const noexcept;
scene_revision authoring_local_revision(NodeHandle) const noexcept;
transform_error set_local(NodeHandle, Transform);
```

Resolve runtime handles through `identities()`; indexed reads require a valid
handle. Runtime local edits affect only that snapshot.

`scene_freeze_outcome<N,E,Transform>` publishes `value_type`, static
`success(value)`/`failure(FreezeError)` factories, explicit bool conversion,
`value()` overloads for mutable/const/rvalue objects, `operator->`, and
`error()`. `value()`/`operator->` require success.

```cpp
template<class N, class E, class Transform>
scene_freeze_outcome<N,E,Transform>
freeze_scene(const basic_authoring_scene<N,E,Transform>&,
             FreezeWorkspace&);

template<class N, class E, class Transform>
scene_freeze_outcome<N,E,Transform>
freeze_scene(const basic_authoring_scene<N,E,Transform>&,
             FreezeWorkspace&,
             const basic_runtime_scene<N,E,Transform>& previous);
```

First freeze creates one dense record per node, all dirty. Incremental freeze
matches by stable identity. It preserves a prior world only when authoring
local revision and stable parent identity are unchanged. New nodes, changed
locals/parents, and runtime-local divergence become dirty; descendants are
selected later by planning. Authoring locals override runtime-only edits at the
next freeze.

### Transform planning and evaluation

`transform_evaluation_workspace` has
`scratch_capacity_bytes() const noexcept`.

```cpp
struct transform_evaluation_plan {
    std::span<const NodeHandle> dirty_roots;
    std::span<const NodeHandle> ordered_nodes;
    scene_revision source_revision{};
    std::uint64_t mutation_generation{};
    std::uint64_t evaluation_generation{};
    const void* topology_identity{};
    transform_evaluation_workspace* workspace{};
    std::uint64_t workspace_generation{};
};
```

`transform_plan_outcome` has static `success(plan)`/`failure(error)`, explicit
bool conversion, `const transform_evaluation_plan& value()`, and `error()`.

```cpp
struct transform_evaluation_result {
    transform_error error{transform_error::none};
    std::span<const NodeHandle> changed_nodes;
    scene_revision world_revision{};
    explicit operator bool() const noexcept;
};

struct transform_execution_options {
    std::size_t minimum_task_grain{2048};
};
```

Public free functions are:

```cpp
make_transform_evaluation_plan(topology, state, workspace);
make_transform_evaluation_plan(topology, state, workspace,
                               std::span<const NodeHandle> selected_dirty_roots);
evaluate_transforms(topology, state, plan, policy);
evaluate_transforms(topology, state, plan, policy, cpu_task_executor&,
                    transform_execution_options = {});
changed_transform_nodes_since(topology, state, scene_revision exclusive_token,
                              std::vector<NodeHandle>& scratch);
```

These are constrained/deduced templates over `Polytree<N,E>`, transform, and
policy. Both planning overloads return `transform_plan_outcome`; evaluation
returns `transform_evaluation_result`; the change query returns a borrowed
`span<const NodeHandle>`.

A full plan returns `state_size_mismatch` on size mismatch. Otherwise a dirty
node is a dirty root when no dirty ancestor owns it. `ordered_nodes` is the
dirty-root subtrees in cached topological order; a clean plan succeeds empty.
The partial overload accepts a unique subset of current dirty roots. Invalid,
duplicate, clean, or dirty-descendant-owned handles return `invalid_scope`;
unselected roots remain pending.

Evaluation copies root local to world, composes non-roots parent-before-child,
clears evaluated dirty flags, assigns one new world revision, and returns the
exact evaluated order. “Changed” means recomputed, not numerically unequal.
Empty evaluation succeeds with no policy calls and no scene-revision advance.

Plans are one-shot borrowed views. They become stale after any state mutation,
any evaluation (including empty), or workspace reuse/destruction. A different
topology returns `topology_mismatch`; other stale use returns `stale_plan`.
Plan/result spans must be copied before owner reuse or destruction.
`changed_transform_nodes_since` clears/reuses caller scratch and selects world
revisions strictly greater than the token in topological order.

The CPU overload has identical semantic results and ordering to sequential
evaluation. Parallel work is limited to dependency-independent nodes.

### CPU executor and compatibility alias

```cpp
class cpu_task_executor {
public:
    using task_function =
        void (*)(void*, std::size_t first, std::size_t last) noexcept;
    struct statistics {
        std::size_t task_count{};
        std::size_t parallel_dispatch_count{};
    };
    explicit cpu_task_executor(std::size_t worker_count = 0);
    ~cpu_task_executor();
    cpu_task_executor(const cpu_task_executor&) = delete;
    cpu_task_executor& operator=(const cpu_task_executor&) = delete;
    std::size_t worker_count() const noexcept;
    statistics last_statistics() const noexcept;
    void reset_statistics() noexcept;
    void execute(std::size_t count, std::size_t minimum_grain,
                 void*, task_function) noexcept;
};
```

`execute` synchronously covers `[0,count)` exactly once. It calls once on the
caller thread unless at least two chunks can meet the grain and workers exist.
Callback/context remain valid until return. Statistics describe the latest
dispatch; reset clears them. The executor is not reentrant.

The compatibility spelling
`evaluation_plan_view<T> = wz::core::graph::PolytreeEvaluationPlan` retains an
ignored template parameter. New coverage should only compile-check the alias.

## Motion API surface

Everything here is in `scene_polytree::motion`.

```cpp
enum class motion_error {
    none, invalid_node, invalid_step, tick_exhausted, state_size_mismatch,
    topology_mismatch, revision_exhausted, transform_failure
};

template<class L, class A> struct motion_state {
    L linear_velocity{}; A angular_velocity{};
};
template<class L, class A> struct motion_update {
    NodeHandle node{INVALID_NODE}; motion_state<L,A> state;
};
template<class L, class A> struct active_motion_record {
    NodeHandle node{INVALID_NODE}; motion_state<L,A> state;
};

struct fixed_motion_step {
    std::uint64_t tick{}; std::chrono::nanoseconds delta{};
};
```

`fixed_step_sequence(delta, initial_tick = 0)` exposes `delta()`, `next_tick()`,
and `next_step()`, all `const noexcept`. It is not a clock scheduler. Successful
central evaluation advances one tick; validation failure does not. Non-positive
delta is `invalid_step`; tick `UINT64_MAX` is `tick_exhausted`.

`MotionPolicy<Policy>` requires transform, linear-velocity, and
angular-velocity type aliases plus:

```cpp
transform_type integrate(const transform_type& local,
    const motion_state<linear_velocity_type, angular_velocity_type>&,
    fixed_motion_step) noexcept;
bool is_stationary(
    const motion_state<linear_velocity_type, angular_velocity_type>&) noexcept;
```

The transform has the same copy/assignment requirements as `TransformPolicy`.

`active_motion_update_workspace<L,A>` publishes `update_type`, `record_type`,
and `scratch_capacity_bytes()`. `active_motion_set<L,A>` publishes
`state_type`, `update_type`, `record_type`, and:

```cpp
template<class N, class E>
explicit active_motion_set(const Polytree<N,E>&) noexcept;
std::size_t size() const noexcept;
bool empty() const noexcept;
std::size_t node_capacity() const noexcept;
std::size_t storage_capacity_bytes() const noexcept;
const void* topology_identity() const noexcept;
std::uint64_t mutation_generation() const noexcept;
std::span<const record_type> records() const noexcept;
template<class Policy> motion_error set(NodeHandle, state_type, Policy&);
motion_error deactivate(NodeHandle);
template<std::ranges::forward_range Updates, class Policy>
motion_error apply_updates(const Updates&, Policy&);
template<std::ranges::forward_range Updates, class Policy>
motion_error apply_updates(const Updates&, Policy&,
                           active_motion_update_workspace<L,A>&);
void clear() noexcept;
```

The set binds to the exact topology object and stores records in ascending
handle order. `set` inserts/replaces motion; stationary state removes it.
`deactivate` and repeated `clear` may be successful no-ops. Invalid handles
return `invalid_node` without mutation. Both batches validate all handles
before mutation; duplicate updates are last-write-wins and distinct input
order does not affect final storage. Borrowed record spans may be invalidated
by logical mutation. Capacity values/growth are not contractual.

`motion_evaluation_workspace<Transform>` has `scratch_capacity_bytes()`.

```cpp
struct motion_evaluation_result {
    motion_error error{motion_error::none};
    transform_error transform_status{transform_error::none};
    std::span<const NodeHandle> integrated_nodes;
    std::span<const NodeHandle> changed_nodes;
    fixed_motion_step step;
    scene_revision world_revision{};
    explicit operator bool() const noexcept;
};
```

The constrained free function:

```cpp
advance_motion_scene(topology, transform_state, active_motion_set,
                     fixed_step_sequence, motion_evaluation_workspace,
                     transform_evaluation_workspace, motion_policy,
                     transform_policy);
```

returns `motion_evaluation_result`. It validates first, integrates active
records in ascending handle order, applies locals, runs normal dirty transform
evaluation, and then advances the tick. `integrated_nodes` is direct order;
`changed_nodes` includes descendants in topological order. An empty active set
is valid and completes a tick; with clean transforms it makes no policy calls.

Topology identity, state size, handles, step, tick, and revision-headroom
errors precede local/tick mutation. `transform_status` preserves a core error;
`error` maps it to the corresponding motion error or `transform_failure`.
Returned spans borrow workspace/state storage.

## Instructions to the test-writing agent

### Evidence and prohibitions

Use only this contract, returned public values, the standard library, and the
provided test support. Compile/run only through the provided command or broker.

Do not inspect/search producer source, public header bodies, objects, symbols,
debug data, existing tests, build metadata, history, or issues. Do not use
`detail`, private-access macros, layout/pointer tricks, disassembly,
preprocessor dumps, include tracing, or diagnostic tricks. Do not assert exact
capacity growth, thread scheduling, representation, exception text, or dense
handles not resolved through the identity map. Do not make UB or
precondition-violating calls. Record missing/ambiguous rules in
`contract-questions.md` rather than discovering and freezing current behavior.

### Test style

- Name one public rule per test and use arrange/act/assert.
- Prefer small explicit fixtures. Use integer transforms and deterministic
  policies; include one non-commutative composition test to prove argument
  order. A root must not cause a compose call.
- Resolve runtime handles through `runtime.identities()`.
- Copy borrowed spans before reusing/mutating their owner.
- Check both error and promised non-mutation on failures. Call `.value()` only
  after checking success.
- Keep tests independent, order-insensitive, and process-parallel safe. Do not
  use timing, sleeps, network, global package state, or external files.
- Property tests must print a fixed seed and shrink to a minimal operation
  sequence. The reference model must derive only from this contract.
- Use stress sizes to catch recursion/scale defects, but keep performance gates
  in a separate benchmark suite.

A useful transform fixture is:

```cpp
struct translation {
    std::int64_t value{};
    friend bool operator==(const translation&, const translation&) = default;
};
struct translation_policy {
    using transform_type = translation;
    translation compose(const translation& parent,
                        const translation& local) noexcept {
        return {parent.value + local.value};
    }
};
```

### Required coverage groups

1. Package/compile contracts: umbrella headers, imported targets, C++20, CTAD,
   policy concept accept/reject cases, and executor copy/move traits.
2. Authoring: empty, insertion coupling, invalid IDs/parents, ordinals, local
   edits, reparent versus reorder, payload replacement, erase, atomic failure.
3. Freeze/identity: empty, singleton, forest, deep/wide; both identity
   directions; incremental insert/erase/edit/reparent/runtime divergence.
4. Transform planning: empty/clean/all-dirty, leaf/ancestor/overlap, forests,
   size mismatch, partial/duplicate scope, workspace lifetime.
5. Transform evaluation: root behavior, parent-first composition, exact order,
   revisions, clean evaluation, stale/topology mismatch, change tokens,
   atomic failure.
6. CPU executor: exact range coverage, zero, grain boundaries, reset/fallback,
   parallel equivalence, reuse, and race-detector execution where available.
7. Active motion: sorted records, replacement/removal/no-ops, invalid atomicity,
   batch equivalence, duplicate last-write-wins, workspace reuse.
8. Motion evaluation: integration order, descendant propagation, empty set,
   ticks, invalid/exhausted steps, identity/size/revision failures, replay.
9. Generated sequences: small forests and operations compared with an
   independent contract model.

Cover every error enumerator reachable by a safe public call. Construct
revision exhaustion and size mismatch through public constructors, never by
altering private state.

Review each test: public rule in name; public installed include; exported
target only; validated indexed reads; identity-derived handles; valid borrowed
lifetime; non-mutation checked on error; no implementation-specific assertion;
reproduction data in failure output.

## Recommended test-project structure

Keep this suite in a separate repository from the producer:

```text
scene-polytree-api-tests/
├── AGENTS.md                    # Points to this contract; repeats prohibitions
├── CMakeLists.txt               # Consumer build; protected in strict mode
├── CMakePresets.json
├── spec/
│   └── scene-polytree-0.4.md    # Version-pinned copy of this document
├── tests/
│   ├── support/                 # Assertions, fixtures, models, generators
│   ├── compile/
│   ├── authoring/
│   ├── freeze/
│   ├── transforms/
│   ├── executor/
│   ├── motion/
│   ├── properties/
│   └── regression/              # One minimized case per accepted defect
└── contract-questions.md
```

Use one CTest executable per behavior area. Keep a dependency-free harness in
`tests/support`, or preinstall an approved framework; tests must not download
dependencies. A consumer build can start with:

```cmake
cmake_minimum_required(VERSION 3.24)
project(scene_polytree_api_tests LANGUAGES CXX)
include(CTest)
find_package(scene-polytree 0.4 CONFIG REQUIRED)

add_executable(transform_contract_tests
    tests/transforms/transform_contract_tests.cpp)
target_compile_features(transform_contract_tests PRIVATE cxx_std_20)
target_link_libraries(transform_contract_tests
    PRIVATE scene-polytree::core scene-polytree::cpu-executor)
add_test(NAME api.transforms COMMAND transform_contract_tests)

add_executable(motion_contract_tests
    tests/motion/motion_contract_tests.cpp)
target_compile_features(motion_contract_tests PRIVATE cxx_std_20)
target_link_libraries(motion_contract_tests PRIVATE scene-polytree::motion)
add_test(NAME api.motion COMMAND motion_contract_tests)
```

For the ordinary private-source model, configure against a read-only install:

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=<scene-polytree-install>
cmake --build build
ctest --test-dir build --output-on-failure
```

For strict isolation, the agent edits only `tests/**` and
`contract-questions.md`; trusted infrastructure supplies the fixed build
manifest. Test at least the oldest and newest supported compiler families,
Debug and optimized builds, and available address/undefined/thread sanitizers.
Pin the library artifact by version and digest.

The project is accepted when it configures solely through the installed
package, has no producer-checkout path, covers the groups above, reports
contract gaps explicitly, passes the supported matrix, detects deliberately
seeded public-contract mutations, and remains iterable without exposing the
implementation or broker internals to the agent.
