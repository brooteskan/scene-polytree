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
- A level-authored `ScenePolytreeComponent` owns one `SceneHandle`, its Prefab registration
  metadata, and the `Collecting -> Building -> Ready|Failed -> Destroying` lifecycle. It never
  subscribes to TickBus.
- Spawners register an O3DE **Prefab**, fixed capacity, optional explicit scene entity, and stable
  registration key during component activation. Registrations without a target resolve the unique
  level-default `ScenePolytreeComponent` after all game entities have activated.
- The level component queue-loads every registered `AzFramework::Spawnable`, extracts logical
  topology from its entity templates, validates the complete set, and submits one combined forest
  to the system. A failed asset or topology never creates a partial runtime scene.
- Registration closes on O3DE's root-Spawnable-ready notification, after all root entities have
  activated. The earlier game-entities-started signal is intentionally ignored because
  Play-In-Editor emits it before asynchronous root spawning completes.
- Transform, binding, reset, release, and activation requests append to a mutex-protected command
  queue. Slot reservation is synchronized and immediate so callers receive a unique lease; queued
  operations are applied when the system drains the queue on its main-thread tick.
- Each scene uses a fixed 60 Hz step, bounded four-step catch-up, and tick order
  `AZ::TICK_GAME + 1`.
- Player and AI adapters submit the same `TankIntent` and never subscribe to TickBus.
- Same-tick evaluations accumulate their ordered `changed_nodes` batches and synchronize that
  union directly with `SetWorldTM`, avoiding a second topology scan. If direct-batch lifetime is
  lost after an error, `changed_transform_nodes_since` remains the revision-token fallback.
- Hull, turret, and gun entities are detached before spawn insertion, so O3DE has no competing
  parent hierarchy. Authored turret and gun pivot markers are detached and sampled once during
  binding; only the three visual targets are projected afterward.
- Spawn callbacks carry only monotonic handles and resolve the system through `AZ::Interface`;
  they never capture component or container pointers.
- Scene handles are monotonic. Slot indices are reused only after their generation increments, so
  stale slot and node handles remain invalid. Destroying a scene immediately stops it from
  accepting commands; queued destruction then releases the runtime forest, so late spawn callbacks
  are harmless.

## Prefab topology and capacity

Attach `ScenePolytreePrefabNodeComponent` to each Prefab entity that represents a logical node. The
component supplies a Prefab-local binding ID, optional logical parent ID, node type, and joint type;
the entity's authored `TransformComponent` local transform supplies the initial logical transform.
The logical relationship is independent from the O3DE entity parent hierarchy. Multiple roots are
supported. Binding IDs must be unique inside one Prefab but may repeat in other registrations.

The level forest reserves this topology `capacity` times before its single freeze. A `SpawnerHandle`
can reserve only its own partition. `SlotHandle` generations reject stale releases and callbacks,
and logical binding IDs resolve to opaque node handles without exposing dense runtime indices.
Reset clears bindings and motion and restores Prefab-authored locals while retaining the lease;
release additionally returns the slot to the partition and increments its generation. Topology
registrations are rejected after collection closes, and runtime requests made before readiness
return `SceneNotReady`.

## Projection contract

The tank spawnable must provide exactly one active, parentless target for each `TankNodeRole` by
attaching `TankNodeBindingComponent` to its hull, turret, and gun visual entities. These existing
components also provide the Hull/Turret/Gun topology compatibility contract; new Prefabs should use
the generic `ScenePolytreePrefabNodeComponent`. The tank adapter
entity must also provide one `TankArticulationBindingComponent` that references authored turret and
gun pivot entities and stores a rigid asset-to-logical basis. Missing, duplicate, inactive,
parented, non-rigid, or aliased visual/pivot bindings are rejected before scene activation.

The spawner maps each normalized authored root transform through `spawn * assetToLogicalBasis`,
then detaches the three targets and two pivot markers. Binding derives the turret local transform
from the hull world and turret pivot, derives the gun local transform from the two pivot worlds, and
captures each target's constant node-to-visual offset. Later synchronization writes only the final
changed visual worlds produced by scene-polytree. Large mesh-origin offsets and uniform visual scale
are supported; source-scene placement is not part of the articulation frame.

The tank spawner no longer creates or destroys a private forest. It registers capacity with its
target level component, reserves slots when construction becomes ready, and releases only those
slots when it deactivates.

## Coordinate and correction policy

Transforms use `AZ::Transform` and velocities use `AZ::Vector3`. +Z is up and tank forward is +Y
in logical local space. Hull/turret yaw is around +Z; gun pitch is around +X. Asset-specific facing
is converted once by `TankArticulationBindingComponent`; motion integration remains asset agnostic.

Terrain sampling and physics contacts remain host responsibilities. Feed their authoritative
results back with `RequestCorrection`, selecting local or world space and identifying the source.
World corrections use the core topology's constant-time parent lookup and the inverse parent world
transform; no hierarchy traversal is added. Corrections received after this Gem's
`AZ::TICK_GAME + 1` update are queued for the next fixed update. The Gem never subscribes to
projection-target transform notifications, preventing transform feedback loops.
