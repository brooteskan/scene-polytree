# ScenePolytree O3DE Gem

`ScenePolytree` is the O3DE host adapter for the engine-neutral scene-polytree libraries. It
keeps authoritative logical hierarchies in frozen runtime forests and projects evaluated world
transforms onto O3DE entities.

## Register and enable

Register this directory as an external Gem, or add its absolute path to a project's
`external_subdirectories`, then add `ScenePolytree` to `gem_names`. The Gem consumes the
repository's `scene-polytree::motion` target. Generic repository builds do not load O3DE.

## Runtime model

- `ScenePolytreeSystemComponent` owns every runtime forest and is the only TickBus handler.
- A level-authored `ScenePolytreeComponent` owns one `SceneHandle`, Prefab registration
  metadata, and the `Collecting -> Building -> Ready|Failed -> Destroying` lifecycle. It never
  subscribes to TickBus.
- Runtime owners register an O3DE **Prefab**, fixed capacity, optional explicit scene entity, and
  stable registration key during component activation. Registrations without a target resolve the
  unique level-default `ScenePolytreeComponent`.
- The level component queue-loads every registered `AzFramework::Spawnable`, derives logical
  topology from its complete O3DE Transform hierarchy, validates the result, and submits one
  combined forest to the system. A failed asset or topology never creates a partial runtime scene.
- Registration closes on O3DE's root-Spawnable-ready notification, after root entities have
  activated. The earlier game-entities-started signal is intentionally ignored because
  Play-In-Editor emits it before asynchronous root spawning completes.
- Transform, binding, reset, release, activation, and correction requests append to a
  mutex-protected command queue. Slot reservation is synchronized and immediate so callers receive
  a unique lease; result-bearing command submissions return a command ID and notify their target
  entity only after the system has executed the queued operation with its final typed result.
- Each scene uses a fixed 60 Hz step, bounded four-step catch-up, and tick order
  `AZ::TICK_GAME + 1`.
- Same-tick evaluations accumulate ordered `changed_nodes` batches and synchronize their union
  directly with `SetWorldTM`. If direct-batch lifetime is lost after an error,
  `changed_transform_nodes_since` remains the revision-token fallback.
- Scene handles are monotonic. Slot indices are reused only after their generation increments, so
  stale slot and node handles remain invalid. Destroying a scene immediately stops it from
  accepting commands; queued destruction then releases the runtime forest.

## Prefab topology and capacity

Prefab topology requires no ScenePolytree authoring components. Every Prefab entity with an O3DE
`TransformComponent` becomes a logical node. Its Transform parent supplies the logical parent, and
its authored local `AZ::Transform` supplies the initial local transform. Multiple roots and
arbitrarily deep hierarchies are supported. Entities without a Transform are ignored; a Transform
whose parent is not another Transform node in the same Prefab is rejected as dangling.

Each binding ID is inferred as the entity-name path from its root, for example
`Root/ArticulationA/ArticulationB/Mesh`. Literal `%` and `/` characters inside an entity name are
escaped as `%25` and `%2F`, so path boundaries stay unambiguous. Names must be nonempty and sibling
name paths must be unique. These are structural identities, not gameplay roles: there are no role
names, pivot types, joint classifications, axis-limited nodes, or fixed-node rules.
Every inferred node accepts the full `AZ::Transform`, including simultaneous translation and
rotation around all three axes.

The level forest reserves each registered topology `capacity` times before its single freeze. A
`SpawnerHandle` can reserve only its own partition. `SlotHandle` generations reject stale
releases and callbacks, and binding IDs resolve to opaque `SceneNodeHandle` values without
exposing dense runtime indices.

`PlaceSlot` applies a final world placement to authored roots. `BindSlot` associates logical
binding IDs with unique O3DE entity IDs and optional node-to-entity offsets. `ResetSlot` clears
bindings and active motion and restores Prefab-authored locals while retaining the lease.
`ReleaseSlot` additionally returns the slot to its partition and increments its generation.
Registrations are rejected after collection closes, and runtime requests made before readiness
return `SceneNotReady`.

## Generalized Prefab spawner

Add `ScenePolytreeSpawnerComponent` beside an O3DE `TransformComponent`. Its Editor configuration
exposes Scene, Prefab, Capacity, Default Placement, Trigger Mode, and Initial Spawn Count. The
Prefab field stores the compiled `AZ::Data::Asset<AzFramework::Spawnable>` while remaining labeled
as a Prefab. Capacity must be nonzero, and the initial count cannot exceed it.

The spawner registers during scene collection and enters `Ready` only after it receives the
existing partition `SpawnerHandle`. `OnReady` starts configured initial instances and disables
external spawn requests; `ExternalRequestsOnly` skips initial instances; and
`OnReadyAndExternalRequests` permits both. Every initial instance uses one snapshot of the spawner
entity's world transform. External requests may instead supply an explicit world transform.

Other entities call the entity-addressed `ScenePolytreeSpawnerRequestBus` and observe the matching
notification bus. Spawn and despawn always allocate generation-bearing request IDs, including
pre-ready, shutdown, stale-handle, and no-capacity failures. Successful spawn notifications occur
only after reset, placement, O3DE insertion, exact Transform-hierarchy validation, and binding have
completed. During pre-insertion, the spawner captures the authored local hierarchy, applies final
world placement, and detaches O3DE Transform parents so ScenePolytree remains the single transform
authority. Successful despawn notifications occur only after unbind, O3DE removal, reset, and
release have completed.

`InstanceHandle` wraps the partition's `SlotHandle` and adds only a spawner-lifetime generation.
Cancellation succeeds until the pre-insertion callback atomically commits. Deactivation advances
that generation, cancels safe pre-commit callbacks, initiates entity and logical-slot cleanup, and
disconnects callback buses so late completions are harmless. The spawner never subscribes to
TickBus and contains no controller discovery or behavior assignment. A future behavior API can
resolve any inferred hierarchy-path binding and drive unrestricted transforms without changing
Prefab authoring or topology extraction.

## Projection and correction

Projection targets are supplied as generic `ScenePolytreeEntityBinding` records. Binding IDs must
belong to the leased slot, entity IDs must be valid and unique, offsets must be finite, and one
entity cannot alias a logical node in another slot. Synchronization writes only changed bound
entities.

Transforms use `AZ::Transform` and velocities use `AZ::Vector3`; coordinate conventions and
gameplay semantics remain consumer policy. Terrain, physics, gameplay, and other host systems may
submit a `SceneCorrection` against an opaque `SceneNodeHandle` in local or world space. World
corrections use the core topology's constant-time parent lookup and inverse parent world transform.
The Gem never subscribes to projection-target transform notifications, preventing transform
feedback loops.
