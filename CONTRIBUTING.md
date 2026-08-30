# Contributing

## No hand-coded loops

Production, integration, example, benchmark, and test C++ code must not contain
handwritten `for`, range-`for`, `while`, or `do` loops. It must not replace them
with ad hoc recursive hierarchy traversal.

Use iterators, standard algorithms, the shared `algo` vocabulary, polytree
views, predicates, lambdas, maps, filters, folds, and apply operations.

If a scene operation cannot be expressed through the existing vocabulary, add
a reusable primitive to the appropriate generic library. Do not hide a scene-
specific traversal behind a narrowly named helper.

Every generic operation must document:

- traversal and ordering semantics;
- time and scratch-space complexity;
- allocation behavior;
- early-termination behavior;
- iterator, reference, and mutation invalidation.

Complex stateful lambdas should become named operations with explicit inputs
and outputs. A lambda is not a waiver for concealed control flow or mutation.

The lightweight repository check rejects raw loop statements in C++ sources.
It is intentionally conservative. A Clang AST-based check should replace it
when the supported compiler toolchain is fixed.

## Dependency boundaries

- `algo` owns general functional composition and execution primitives.
- `polytree` owns topology, ordering, and hierarchy traversal.
- `scene-polytree` owns scene data and scene-specific operations.
- Integrations translate changes at the engine boundary; they do not implement
  another hierarchy walker.

Changes that reverse these dependencies require an architectural proposal.
