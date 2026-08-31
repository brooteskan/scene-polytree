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
- The level component queue-loads every registered `AzFramework::Spawnable`, extracts logical
  topology from generic node metadata, validates the complete set, and submits one combined forest
  to the system. A failed asset or topology never creates a partial runtime scene.
- Registration closes on O3DE's root-Spawnable-ready notification, after root entities have
  activated. The earlier game-entities-started signal is intentionally ignored because
  Play-In-Editor emits it before asynchronous root spawning completes.
- Transform, binding, reset, release, activation, and correction requests append to a
  mutex-protected command queue. Slot reservation is synchronized and immediate so callers receive
  a unique lease; queued operations are applied when the system drains the queue.
- Each scene uses a fixed 60 Hz step, bounded four-step catch-up, and tick order
  `AZ::TICK_GAME + 1`.
- Same-tick evaluations accumulate ordered `changed_nodes` batches and synchronize their union
  directly with `SetWorldTM`. If direct-batch lifetime is lost after an error,
  `changed_transform_nodes_since` remains the revision-token fallback.
- Scene handles are monotonic. Slot indices are reused only after their generation increments, so
  stale slot and node handles remain invalid. Destroying a scene immediately stops it from
  accepting commands; queued destruction then releases the runtime forest.

## Prefab topology and capacity

Attach `ScenePolytreePrefabNodeComponent` to each Prefab entity that represents a logical node.
The component supplies a Prefab-local binding ID, optional logical parent ID, node type, and joint
type. The entity's authored `TransformComponent` local transform supplies the initial logical
transform. The logical relationship is independent from the O3DE entity parent hierarchy.
Multiple roots are supported. Binding IDs must be unique inside one Prefab but may repeat in other
registrations.

There is no role-name fallback or domain-specific metadata path. A Prefab without valid generic
node metadata fails with `MissingTopologyMetadata`.

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
