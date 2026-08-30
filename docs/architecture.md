# Architecture

## Repository role

`scene-polytree` supplies scene semantics over a topology owned by a generic
polytree library. Composition is preferred to inheritance: `basic_scene` owns
or references a topology and a scene-specific state store without reproducing
the topology's traversal surface.

## Authoring and runtime

The intended authoring form uses a mutable polytree. A compile operation will
validate and freeze that topology into a static polytree plus an evaluation
plan. The plan exposes cached topological, reverse-topological, and level-based
orders supplied by the generic topology layer.

The word "compile" is deliberately narrow here. It means producing compact
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

Topology and scene state remain separate concerns. Dense scene state may be
indexed by runtime node handles, while sparse optional state such as motion can
live in side stores keyed by stable node identity. The concrete choice must be
benchmarked before becoming part of the persistent file format or ABI.

## O3DE

The O3DE integration is an adapter. Project-local Gems may own a scene instance
and synchronize selected results into O3DE. A deeper engine experiment may
instead use the same core types nearer the transform system. Neither route
changes the core's dependency on generic polytree algorithms.
