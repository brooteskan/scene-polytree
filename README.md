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

The repository does not privately copy the Wozzits polytree headers. During
bootstrap, its public scene contracts can be built without a concrete polytree
package. Set `SCENE_POLYTREE_REQUIRE_POLYTREE=ON` once a CMake package exporting
`polytree::polytree` is available.

## Initial targets

- `scene-polytree::core`: topology composition, transform records, and runtime
  evaluation-plan contracts.
- `scene-polytree::motion`: optional motion state layered on the core.
- `scene-polytree::o3de`: reserved for the optional O3DE Gem adapter; it will
  not become the owner of scene topology.

The initial commit deliberately establishes contracts and repository policy
before importing a concrete topology implementation. The next implementation
milestone is to package the generic `algo` and `polytree` dependencies, then add
transform propagation exclusively through those APIs.

## Build and test

```sh
cmake -S . -B build -DSCENE_POLYTREE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the source-policy check directly with:

```sh
python tools/check_control_flow_policy.py
```

See [docs/architecture.md](docs/architecture.md) and
[CONTRIBUTING.md](CONTRIBUTING.md) before adding traversal or evaluation code.
