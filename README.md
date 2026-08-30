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

The repository does not privately copy the Wozzits polytree headers. Its core
target requires `polytree::polytree` by default. CMake uses an explicitly
provided source checkout or installed package when available, otherwise it
fetches the mutable/freeze-capable `polytree` v0.2.0 release. That package resolves
`algo::algo` in the same way.

## Initial targets

- `scene-polytree::core`: topology composition, transform records, and runtime
  evaluation-plan contracts.
- `scene-polytree::motion`: optional motion state layered on the core.
- `scene-polytree::o3de`: reserved for the optional O3DE Gem adapter; it will
  not become the owner of scene topology.

The contract tests instantiate a mutable three-node hull, turret, and gun
authoring hierarchy, freeze it into compact runtime topology, verify both
identity directions and cached evaluation plans, then reparent and freeze again
to demonstrate explicit snapshot invalidation. Transform propagation remains a
later milestone and will be implemented exclusively through the generic
package APIs.

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
- [Extraction inventory](docs/extraction-inventory.md)
- [Behavior-to-test manifest](docs/behavior-test-manifest.md)
- [Baseline test report](docs/baseline-test-report.md)
- [Contributing rules](CONTRIBUTING.md)
