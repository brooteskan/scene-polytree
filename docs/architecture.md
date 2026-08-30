# Architecture

## Repository role

`scene-polytree` supplies scene semantics over a topology owned by a generic
polytree library. Composition is preferred to inheritance: `basic_scene` owns
or references a topology and a scene-specific state store without reproducing
the topology's traversal surface.

## Authoring and runtime

The authoring form uses `MutablePolytree` with stable authoring identity. A
freeze operation validates and compiles that topology into a compact static
polytree, cached evaluation plans, bidirectional authoring/runtime identity
maps, and a source revision. `polytree` v0.2.0 owns this operation and its
ordering rules; scene-polytree consumes the generic result rather than
constructing a second scene-specific schedule.

The word "freeze" is deliberately narrow here. It means producing compact
topology and evaluation metadata; it does not mean mirroring an engine's scene
database or creating a second entity/component system.

## Evaluation

Transform propagation consumes topological order. Work that can execute in
parallel consumes dependency levels. Motion, procedural animation, Inochi
puppet evaluation, and engine synchronization are separate operations over the
same scene and ordering data.

Scene operations select data and describe transformations. Generic operations
own iteration, early termination, scheduling, and possible CPU, SIMD, task, or
GPU execution strategies.

## Storage

Dense runtime scene state is indexed by frozen `NodeHandle` values, while
authoring state and sparse optional state can be keyed by `StableNodeId`.
Freeze mappings are immutable snapshots: any successful authoring mutation
changes the source revision, so a later runtime snapshot must use the later
mapping. The mapping and old runtime scene remain valid as an older snapshot.

Mutable topology, compact runtime topology, and scene state remain separate
objects. A `basic_scene` composes either topology form with its corresponding
state without introducing separate hierarchy semantics.

## O3DE

The O3DE integration is an adapter. Project-local Gems may own a scene instance
and synchronize selected results into O3DE. A deeper engine experiment may
instead use the same core types nearer the transform system. Neither route
changes the core's dependency on generic polytree algorithms.
