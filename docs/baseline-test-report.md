# Baseline test report

## Scope

This report completes the test-evidence portion of scene-polytree issue #2.
It records the immutable source baseline and the verification runs used after
extraction. The extraction tags remain the byte-for-byte authority even where
current branches have subsequently consolidated APIs.

## Revisions

| Repository | Revision used as authority or verification head |
|---|---|
| Wozzits source baseline | `0ca3377dd5d9472a5a73426646026f2b085994e1` |
| `algo` immutable baseline | `v0.0.1-wozzits-baseline` -> `8d0801a22923790442c9972e6248ca30e14ca7c4` |
| `algo` current verification | `ea2e45f8c19ebe7deb44c85c91179e12fac57154` |
| `polytree` immutable baseline | `v0.0.1-wozzits-baseline` -> `b74ffa17d6da7fc06ec5b585c5faf2ec118b406b` |
| `polytree` current verification | `4fdc416bd60f8208686be07985e5a8286545b751` |
| Wozzits current verification | `91a345aad96f1af5beee67262263bb81f5c1e3b4` |
| scene-polytree pre-documentation head | `0cf9132873242db5ccda068f0c033eb222f92e6d` |

The Wozzits algo and static-polytree test sources are unchanged from the
pinned baseline except that current graph algorithm tests include
`algo/next.h` and use its namespace after the migration. The immutable package
tags retain the exact original headers and tests.

## Environment

Verification was performed on Windows x64 with two compiler configurations:

```text
CMake:                4.3.1
Wozzits compiler:     clang-cl 21.1.8, x86_64-pc-windows-msvc
Wozzits preset:       clang-debug
Package compiler:     MSVC 19.51.36256.0, x64
Package generator:    Visual Studio 18 2026
Windows SDK:          10.0.26100.0
Git:                  2.43.0.windows.1
Date:                 2026-08-30 (America/Los_Angeles)
```

GoogleTest is a private test dependency. Extracted package targets do not link
the Wozzits engine or its third-party runtime dependencies.

## Original focused Wozzits baseline result

The six executables selected by the Phase 1 plan were:

```text
algo_algo_test_1
algo_algo_test_next
algo_algo_test_pipeline
graph_static_polytree_tests_0
graph_static_polytree_algo_tests_0
scene_graph_scene_graph_test_1
```

They were built and selected with:

```powershell
cmake --build build\clang-debug --target `
  algo_algo_test_1 `
  algo_algo_test_next `
  algo_algo_test_pipeline `
  graph_static_polytree_tests_0 `
  graph_static_polytree_algo_tests_0 `
  scene_graph_scene_graph_test_1 --parallel

ctest --test-dir build\clang-debug --output-on-failure `
  -R "^(algo_algo_test_1|algo_algo_test_next|algo_algo_test_pipeline|graph_static_polytree_tests_0|graph_static_polytree_algo_tests_0|scene_graph_scene_graph_test_1)$"
```

They built successfully with the Wozzits `clang-debug` preset. Five
executables passed. `algo_algo_test_1` passed 28 of 29 cases and failed only:

```text
AlgoApplySpec.MultipleOpsExecuteSequentially
```

That test expects two `ops::map` operations passed to legacy `algo::apply` to
form a pipeline. The implementation actually invokes both operations against
the original input and a shared bounded output. This mismatch is a known
baseline defect, not an extraction regression.

Current Wozzits CTest registration therefore runs:

- the other 28 legacy cases as a normal passing test; and
- the composition case separately with `WILL_FAIL`, so an unexpected change
  of behavior also makes the gate fail.

The extracted `algo` package uses the same expected-failure arrangement and
adds a passing test for the observed shared-input behavior.

## Commands

### Wozzits current-consumer gate

```powershell
cmake --build build\clang-debug --target `
  algo_algo_test_next `
  graph_static_dag_algo_tests_0 `
  graph_static_polytree_algo_tests_0 `
  scene_graph_scene_graph_test_1 `
  scene_compile_scene_compiler_test_2 `
  render_render_frame_sectioned_test_1 --parallel

ctest --test-dir build\clang-debug --output-on-failure `
  -R "^(algo_algo_test_next|graph_static_dag_algo_tests_0|graph_static_polytree_algo_tests_0|scene_graph_scene_graph_test_1|scene_compile_scene_compiler_test_2|render_render_frame_sectioned_test_1)$"
```

Result: 6 of 6 focused current-consumer tests passed.

### Wozzits full pre-push gate

```powershell
cmake --build --preset clang-debug
ctest --preset clang-debug
```

Result after isolating the known legacy expectation: 345 of 345 CTest entries
passed. The total includes four `algo` entries: the legacy subset, current
`next`, historical pipeline, and the expected-failure composition case.

### Extracted algo package

```powershell
cmake -S D:\wzmono\algo -B D:\TG\build-algo-doc-pass `
  -DALGO_BUILD_TESTS=ON
cmake --build D:\TG\build-algo-doc-pass --config Debug --parallel
ctest --test-dir D:\TG\build-algo-doc-pass -C Debug --output-on-failure
```

Result: 6 of 6 entries passed:

```text
algo.baseline.legacy
algo.baseline.known_failure.apply_composition
algo.baseline.next
algo.next.status
algo.baseline.pipeline
algo.baseline.current_behavior
```

The known-failure entry is successful only when the selected GoogleTest case
returns failure. This prevents the historical mismatch from being silently
forgotten.

### Extracted polytree package

The current dependency pin was verified both with an explicit sibling `algo`
checkout and through a clean FetchContent configuration:

```powershell
cmake -S D:\wzmono\polytree -B D:\TG\build-polytree-doc-pass `
  -DPOLYTREE_BUILD_TESTS=ON `
  -DPOLYTREE_ALGO_SOURCE_DIR=D:\wzmono\algo
cmake --build D:\TG\build-polytree-doc-pass --config Debug --parallel
ctest --test-dir D:\TG\build-polytree-doc-pass -C Debug --output-on-failure
```

Result: 2 of 2 entries passed:

```text
polytree.baseline.core
polytree.baseline.algorithms
```

The earlier clean FetchContent verification passed both entries, confirming
that the checked-in immutable algo revision can be obtained without a sibling
checkout.

### scene-polytree contracts and policy

```powershell
cmake -S D:\wzmono\scene-polytree -B D:\TG\build-scene-polytree-doc-pass `
  -DSCENE_POLYTREE_BUILD_TESTS=ON `
  -DSCENE_POLYTREE_POLYTREE_SOURCE_DIR=D:\wzmono\polytree `
  -DSCENE_POLYTREE_ALGO_SOURCE_DIR=D:\wzmono\algo
cmake --build D:\TG\build-scene-polytree-doc-pass --config Debug --parallel
ctest --test-dir D:\TG\build-scene-polytree-doc-pass -C Debug --output-on-failure
```

Result: 2 of 2 entries passed:

```text
scene_polytree.contracts
scene_polytree.no_handwritten_loops
```

The contract executable constructs and validates a real three-node
hull-to-turret-to-gun hierarchy against the extracted packages. The policy
entry scans scene-polytree C++ production, extension, integration, example,
benchmark, and test sources for handwritten loop statements.

## Evidence coverage and open gaps

Detailed operation-to-test mapping is in
[behavior-test-manifest.md](behavior-test-manifest.md). The significant gaps
that remain intentionally open are:

- `ops::filter`, `ops::reduce_op`, filter-filter pipeline composition, and
  move-only/reference callable behavior lack direct baseline tests.
- Baseline `next.h` and pipeline range calls stop on rejection but cannot tell
  callers that output was truncated. Current `next.h` corrects this with an
  explicit two-state result and focused tests.
- Exact root and sibling tie order is not guaranteed by the static builder's
  parent-only edge sort.
- Empty static topology, invalid-handle queries, insufficient root scratch,
  and non-trivial payload lifetime need stronger characterization.
- Wozzits scene scratch collectors silently truncate and are not tested with
  insufficient capacity.
- The Wozzits transform fixture establishes translation composition, not a
  complete backend-neutral transform policy.
- Wozzits scene payload mutation uses `const_cast`; this is recorded as a
  boundary defect and must not be reproduced.

These gaps are explicit inputs to later issues. They are not permission to
invent behavior silently.

## Baseline sign-off

The Phase 1 evidence now has:

- a pinned and attributed source closure;
- immutable extraction tags;
- a public-operation behavior manifest;
- reproducible focused and package test commands;
- an explicit expected-failure treatment for the legacy apply mismatch; and
- a list of remaining ambiguities assigned to later design work.

No O3DE or Wozzits engine dependency crosses into the extracted package target
interfaces, and no Wozzits implementation header was copied into
scene-polytree.
