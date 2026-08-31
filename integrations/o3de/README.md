# ScenePolytree O3DE Gem

`ScenePolytree` is the O3DE host adapter for the engine-neutral scene-polytree libraries. It keeps
the authoritative articulated hierarchy in one frozen runtime forest and projects evaluated world
transforms onto flat O3DE entities.

## Register and enable

Register this directory as an external Gem, or add its absolute path to a project's
`external_subdirectories`, then add `ScenePolytree` to `gem_names`. The Gem consumes the
repository's `scene-polytree::motion` target. Generic repository builds do not load O3DE.

## Runtime model

- `ScenePolytreeSystemComponent` owns every runtime forest and is the only TickBus handler.
- Mutating interface and EBus requests append to a mutex-protected command queue. Frozen scene
  state is changed only while the system drains that queue on its main-thread tick.
- Each scene uses a fixed 60 Hz step, bounded four-step catch-up, and tick order
  `AZ::TICK_GAME + 1`.
- Player and AI adapters submit the same `TankIntent` and never subscribe to TickBus.
- Only nodes returned by `changed_transform_nodes_since` are written with `SetWorldTM`.
- Hull, turret, and gun entities are detached before spawn insertion, so O3DE has no competing
  parent hierarchy.
- Spawn callbacks carry only monotonic handles and resolve the system through `AZ::Interface`;
  they never capture component or container pointers.
- Handles are never reused. Destroying a scene immediately stops it from accepting commands;
  queued destruction then releases the runtime forest, so late spawn callbacks are harmless.

## Projection contract

The tank spawnable must provide exactly one active, parentless target for each `TankNodeRole` by
attaching `TankNodeBindingComponent` to its hull, turret, and gun visual entities. Missing,
duplicate, inactive, or parented targets are rejected before scene activation. A target's authored
world transform is captured as a constant node-to-visual offset; later synchronization writes only
the final changed worlds produced by scene-polytree.

## Coordinate and correction policy

Transforms use `AZ::Transform` and velocities use `AZ::Vector3`. +Z is up and tank forward is +Y
in local space. Hull/turret yaw is around +Z; gun pitch is around +X.

Terrain sampling and physics contacts remain host responsibilities. Feed their authoritative
results back with `RequestCorrection`, selecting local or world space and identifying the source.
World corrections use the core topology's constant-time parent lookup and the inverse parent world
transform; no hierarchy traversal is added. Corrections received after this Gem's
`AZ::TICK_GAME + 1` update are queued for the next fixed update. The Gem never subscribes to
projection-target transform notifications, preventing transform feedback loops.
