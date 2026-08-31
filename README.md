# scene-polytree

`scene-polytree` is an engine-neutral scene hierarchy built as a client of a
generic polytree and its functional algorithm vocabulary. It is intended for
use in project-local engine integrations as well as experiments with deeper
scene and transform architectures.

The defining rule is that scene code does not hand-code iteration or recursive
hierarchy traversal. Traversal belongs to the generic algorithm and polytree
layers; scene behavior is expressed with views, predicates, transforms, maps,
filters, folds, and apply operations.

## Dependency direction

```text
algo
  ^
polytree
  ^
scene-polytree core
  ^
motion extension
  ^
O3DE adapter
```

The repository does not privately copy the Wozzits polytree or algorithm
headers. Its core target requires both `polytree::polytree` and `algo::algo`.
CMake uses explicitly provided source checkouts or installed packages when
available, otherwise it fetches the mutable/freeze-capable `polytree` v0.2.0
release, which resolves the matching algorithm package.

## Targets

- `scene-polytree::core`: mutable authoring scenes, compact runtime snapshots,
  transform storage, dirty planning, partial evaluation, and changed-node
  views.
- `scene-polytree::motion`: deterministic fixed-step local motion, sparse
  active-node storage, and centralized dirty-transform evaluation layered on
  the core.
- `scene-polytree::cpu-executor`: persistent CPU workers for the optional
  dependency-level transform overload. Link this target in addition to
  `scene-polytree::core` when using that overload.
- `scene-polytree::o3de`: reserved for the optional O3DE Gem adapter; it will
  not become the owner of scene topology.

The core transform evaluator caches subtree metadata over polytree's
topological view. Local edits enter an exact dirty frontier; planning expands
only its owned subtrees, and evaluation composes only those affected nodes.
Stable-ID authoring state can be frozen initially or reconciled against a
previous runtime snapshot so reparented subtrees update without touching
unrelated roots. Large, wide batches may be evaluated through the CPU executor;
chains and sub-grain work remain sequential.

## Transform example

```cpp
struct translation
{
    int value{};
};

struct translation_policy
{
    using transform_type = translation;

    translation compose(
        const translation& parent_world,
        const translation& local) noexcept
    {
        return {parent_world.value + local.value};
    }
};

scene_polytree::basic_authoring_scene<int, int, translation> authoring;
const auto root = authoring.insert_root(10, translation{100}).value();
authoring.insert_child(root, 20, 1, translation{5});

wz::core::graph::FreezeWorkspace freeze_workspace;
auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
auto runtime = std::move(frozen).value();

scene_polytree::transform_evaluation_workspace evaluation_workspace;
auto plan = scene_polytree::make_transform_evaluation_plan(
    runtime.topology(), runtime.state(), evaluation_workspace);
translation_policy policy;
auto changed = scene_polytree::evaluate_transforms(
    runtime.topology(), runtime.state(), plan.value(), policy);
```

See [the transform contract](docs/transforms.md) for dirty-state, revision,
partial-evaluation, lifetime, reparenting, and thread-safety semantics.

## Motion extension

The motion extension keeps player/AI intent and engine math in consumer policy
while owning one fixed-step evaluation pipeline. Moving nodes are registered in
deterministic handle order; stationary state is removed from the active set.
Each successful tick integrates active local transforms and delegates all
descendant propagation to the core dirty planner.

See [the motion contract](docs/motion.md) and
[storage benchmark results](docs/motion-storage-results.md).

## Build and test

```sh
cmake -S . -B build -DSCENE_POLYTREE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For offline development with sibling checkouts:

```sh
cmake -S . -B build \
  -DSCENE_POLYTREE_BUILD_TESTS=ON \
  -DSCENE_POLYTREE_POLYTREE_SOURCE_DIR=/path/to/polytree \
  -DSCENE_POLYTREE_ALGO_SOURCE_DIR=/path/to/algo
```

Run the source-policy check directly with:

```sh
python tools/check_control_flow_policy.py
```

Before adding traversal or evaluation code, see:

- [Architecture](docs/architecture.md)
- [Transform evaluation contract](docs/transforms.md)
- [Motion extension contract](docs/motion.md)
- [Motion storage benchmark](docs/motion-storage-results.md)
- [Performance baselines](docs/performance-baselines.md)
- [Performance round 2](docs/performance-round-2.md)
- [Parallel execution recommendation](docs/parallel-execution-recommendation.md)
- [Extraction inventory](docs/extraction-inventory.md)
- [Behavior-to-test manifest](docs/behavior-test-manifest.md)
- [Baseline test report](docs/baseline-test-report.md)
- [Contributing rules](CONTRIBUTING.md)
