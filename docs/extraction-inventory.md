# Wozzits extraction inventory

## Purpose and authority

This document records the source boundary used to extract `algo`, `polytree`,
and the scene behavior reference that informs `scene-polytree`. It is a
retrospective completion of scene-polytree issue #2, not a claim that current
development branches are still byte-for-byte copies.

The behavioral baseline is Wozzits `master` at:

```text
repository: https://github.com/brooteskan/wozzits
commit:     0ca3377dd5d9472a5a73426646026f2b085994e1
date:       2026-08-12T09:05:18-07:00
```

Immutable extracted baselines are:

| Package | Tag | Commit |
|---|---|---|
| `algo` | `v0.0.1-wozzits-baseline` | `8d0801a22923790442c9972e6248ca30e14ca7c4` |
| `polytree` | `v0.0.1-wozzits-baseline` | `b74ffa17d6da7fc06ec5b585c5faf2ec118b406b` |

The package `BASELINE.md` files contain SHA-256 hashes proving the imported
files match the pinned Wozzits revision. Later commits are evaluated against
the contracts in [behavior-test-manifest.md](behavior-test-manifest.md).

## Target dependency graph

```text
C++ standard library
        |
        v
    algo::algo
        |
        v
polytree::polytree
        |
        v
scene-polytree::core
    |               |
    v               v
scene-polytree::motion    integration adapters
                          (O3DE or Wozzits)
```

Dependencies point downward only. Wozzits and O3DE are consumers and may not
become dependencies of any generic package.

## Algorithm source closure

| Wozzits baseline source | Extracted location | Direct dependencies | Classification and disposition |
|---|---|---|---|
| `window_engine/algo/algo.h` | `algo/include/algo/algo.h` | C++ standard library | Baseline input. Immediate `for_each`, `transform`, `reduce`, `filter`, and `apply` behavior retained at the baseline tag; superseded on current `main` by the canonical `next.h` direction. |
| `window_engine/algo/ops.h` | `algo/include/algo/ops.h` | C++ standard library | Baseline input. Immediate map, filter, and reduce adapters retained for historical characterization. |
| `window_engine/algo/next.h` | `algo/include/algo/next.h` | C++ standard library | Baseline input and primary range/sink reference. Current `main` extends it with composed pipelines and explicit execution status. |
| `window_engine/algo/pipeline.h` | `algo/include/algo/pipeline.h` | C++ standard library, with missing direct baseline includes | Baseline input. Composition and early-termination reference; capabilities now live in current `next.h`. |
| `window_engine/containers/buffer.h` | `algo/tests/support/containers/buffer.h` | C++ standard library | Test support only. It is not installed and is not a public dependency of `algo::algo`. |

Algorithm tests copied into the immutable baseline:

| Wozzits test | Extracted test | Role |
|---|---|---|
| `tests/algo/algo_test_1.cpp` | `algo/tests/algo/algo_test_1.cpp` | Immediate `algo.h`, `ops.h`, and `apply` characterization. |
| `tests/algo/algo_test_next.cpp` | `algo/tests/algo/algo_test_next.cpp` | Range/sink and fused map/filter characterization. |
| `tests/algo/algo_test_pipeline.cpp` | `algo/tests/algo/algo_test_pipeline.cpp` | Pipeline ordering, fusion, and truncation characterization. |

Post-baseline tests in `algo` are not attributed to Wozzits source:

- `tests/baseline_current_behavior.cpp` records the actual shared-input
  behavior of legacy `apply`.
- `tests/algo/algo_test_next_status.cpp` specifies current `next.h`
  completion/truncation reporting and general composition.

The public `algo::algo` target depends only on the C++ standard library.
GoogleTest is private to tests.

## Static-polytree source closure

| Wozzits baseline source | Extracted location | Direct dependencies | Classification and disposition |
|---|---|---|---|
| `window_engine/graph/concepts.h` | `polytree/include/graph/concepts.h` | `<concepts>` | Baseline input containing the sink concept. The concept is generic and may ultimately move to `algo`. |
| `window_engine/graph/static_dag.h` | `polytree/include/graph/static_dag.h` | C++ standard library | Incidental baseline closure. Static polytree uses its handle, invalid sentinel, and storage carving helper; DAG behavior is not part of the intended polytree API. |
| `window_engine/graph/static_polytree.h` | `polytree/include/graph/static_polytree.h` | Standard library plus the incidental DAG facilities | Baseline input for builder, static storage/view, parent array, child CSR, edge payloads, queries, visitor traversal, and cached topological order. |
| `window_engine/graph/static_polytree_algo.h` | `polytree/include/graph/static_polytree_algo.h` | Static polytree, sink concept, algo pipeline, standard library | Baseline input for sink traversal, scratch materialization, document-tree helpers, and pipeline adapters. Current `main` uses `algo/next.h`. |

Polytree tests copied into the immutable baseline:

| Wozzits test | Extracted test | Role |
|---|---|---|
| `tests/graph/static_polytree_tests_0.cpp` | `polytree/tests/graph/static_polytree_tests_0.cpp` | Builder, validation, static queries, cached order, and visitor traversal. |
| `tests/graph/static_polytree_algo_tests_0.cpp` | `polytree/tests/graph/static_polytree_algo_tests_0.cpp` | Sink traversal, pipeline adapters, scratch materialization, and document-tree helpers. |

The public `polytree::polytree` target depends only on `algo::algo` and the C++
standard library. The complete static DAG is installed today because it is
part of the internal include closure, not because scene-polytree consumes DAG
semantics.

## Scene behavior reference

Scene implementation was deliberately not copied during Phase 1.

| Wozzits baseline source | Target responsibility | Disposition |
|---|---|---|
| `window_engine/scene/transform_node.h` | Split between `scene-polytree::core`, optional motion, and engine adapters | Behavioral reference only. Wozzits matrices, engine flags, and `MotionType` do not define the generic topology API. |
| `window_engine/scene/scene_graph.h` | `scene-polytree::core` | Behavioral reference for parent-before-child propagation, local edits, dirty roots, subtree updates, and animated selection. Replace Wozzits matrix multiplication with a transform policy and `const_cast` payload mutation with explicit mutable state. |
| `tests/scene_graph/scene_graph_test_1.cpp` | `scene-polytree` contract tests | Compatibility evidence. Tests are to be re-expressed with package-local types and the no-handwritten-loops policy rather than copied literally. |

The current `scene-polytree/tests/contracts_tests.cpp` is a narrow package
wiring test for a real root-to-intermediate-to-leaf static hierarchy. It is not a
replacement for the Wozzits scene behavior suite.

## Reverse consumers that stay outside the packages

The following depend on extracted concepts but are not extraction inputs:

- `window_engine/graph/graph_problems.h`.
- JSON, TOML, and other Wozzits polytree adapters.
- `window_engine/scene/scene_ecs.h` and its component schema.
- `window_engine/scene/compile/*` and compiled render storage.
- `window_engine/engine/assets/scene/*` and scene instantiation.
- Renderer, audio, behavior, collision, input, editor, and application code.
- O3DE entities, transforms, buses, reflection, allocators, and jobs.

Those systems may consume released packages through adapters. They may not
donate engine-specific types or dependencies to the generic targets.

## Dependencies excluded from package boundaries

### Excluded from `algo::algo`

- Graph, scene, engine, math, rendering, platform, allocator, task, logging,
  and O3DE types.
- `containers/buffer.h` as a public or required container.
- GoogleTest outside the private test build.

### Excluded from `polytree::polytree`

- Scene payloads, matrices, ECS records, serialization, assets, and engines.
- Shared-edge graph variants and graph-problem algorithms.
- DAG semantics beyond the baseline's accidental internal facilities.
- O3DE types and services.

### Excluded from `scene-polytree::core`

- `wz::math::Mat4` and `wz::math::mul`.
- Wozzits render/update-domain flags and ECS component records.
- Compiled render scenes, assets, behavior, audio, collision, editor, and
  document models.
- O3DE types; they belong only in `integrations/o3de`.

The Wozzits monolithic test target brings many unrelated libraries into a
normal engine build. None of RtAudio, PMP, fastgltf, D3D12/DXGI, TinyEXR,
miniz, yyjson, TOML++, pugixml, libyaml, PLY, or stb belongs in an extracted
package interface.

## License and attribution inventory

| Source family | License evidence | Required treatment |
|---|---|---|
| Wozzits sources and tests | Wozzits `LICENSE.txt`: MIT License, copyright 2026 Wozzits Engine contributors | Preserve the MIT notice in extracted distributions and identify Wozzits plus the pinned source revision. |
| `algo` package additions | `algo/LICENSE` and `algo/NOTICE` | MIT; imported Wozzits files retain Wozzits attribution, while new packaging/tests remain MIT. |
| `polytree` package additions | `polytree/LICENSE` and `polytree/NOTICE` | MIT; imported Wozzits files retain Wozzits attribution. |
| `scene-polytree` original code and documentation | Repository `LICENSE` and `NOTICE` | MIT; no Wozzits implementation header was copied into this repository. |
| GoogleTest | Private fetched or installed test dependency | Its license remains with the dependency; it is not installed by these packages. |

## Closed minimal dependency lists

```text
algo production:
  include/algo/{algo.h, ops.h, next.h, pipeline.h}
  -> C++ standard library

algo tests:
  production headers
  + tests/support/containers/buffer.h
  + GoogleTest

polytree production baseline:
  include/graph/{concepts.h, static_dag.h,
                 static_polytree.h, static_polytree_algo.h}
  -> algo::algo
  -> C++ standard library

polytree tests:
  production headers
  + GoogleTest

scene behavior reference:
  Wozzits scene/{transform_node.h, scene_graph.h}
  + tests/scene_graph/scene_graph_test_1.cpp
  (reference only; not a package dependency)
```

## Known boundary findings

1. The four historical algorithm headers overlap and disagree about sink
   rejection. Current development makes `next.h` canonical without altering
   the immutable baseline tag.
2. Baseline `pipeline.h` is not self-contained; it obtains tuple, invocation,
   and type-trait declarations transitively in some translation units.
3. Static polytree imports the full static DAG for incidental facilities.
4. Visitor and sink traversals duplicate control flow.
5. Static edge sorting compares only the parent handle. Existing tests observe
   useful child order but do not establish a portable stable sibling-order
   guarantee.
6. Static payload access is const, while Wozzits scene code mutates it through
   `const_cast`; scene state must become explicitly mutable and separate from
   immutable topology.
7. Materialization and scene scratch collectors return shortened spans without
   reporting truncation.
8. Non-trivial payload destruction and invalid-handle query behavior are not
   adequately characterized.

These findings are inputs to later issues. This document records them without
silently choosing replacement behavior.
