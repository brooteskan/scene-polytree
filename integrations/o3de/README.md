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
  `AZ::TICK_GAME + 1`. For every available step the system advances persistent motion, evaluates
  hierarchy transforms, and finally synchronizes changed O3DE entities. Only behavior types that
  explicitly opt into continuous execution receive a fixed-step callback.
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
authority. Successful despawn notifications occur only after controller input closure and
destruction, unbind, O3DE removal, reset, and release have completed.

`InstanceHandle` wraps the partition's `SlotHandle` and adds only a spawner-lifetime generation.
Cancellation succeeds until the pre-insertion callback atomically commits. Deactivation advances
that generation, cancels safe pre-commit callbacks, destroys attached controllers before the
logical slot is touched, initiates entity and logical-slot cleanup, and disconnects callback buses
so late completions are harmless. The spawner never subscribes to TickBus.

## Prefab-owned behaviors and authored node targets

A feature Gem supplies its own component that derives from both `AZ::Component` and
`ScenePolytreeBehaviorProvider`. Include `ScenePolytreeBehaviorProvider` in the `AZ_COMPONENT`
base list so AZ RTTI can discover the interface. Attach that component to the Prefab root that owns
the behavior. A Windmill Gem therefore places its Windmill behavior component on the Windmill
root. The rotor pivot is a node the Windmill controls, not the owner of the Windmill behavior.

The component stores behavior-specific reflected authoring data, but it does not tick or write an
O3DE transform. `CopyScenePolytreeControllerDeclaration` returns:

- a stable declaration name and registered controller type ID;
- an immutable clone of configuration derived from `ScenePolytreeControllerConfiguration`; and
- semantic targets derived from the component's reflected Prefab-local entity references, such as
  `Rotor` mapped to the Windmill's rotor-pivot entity.

During spawn, ScenePolytree discovers providers in the spawned Prefab only. O3DE's Spawnable
cloning remaps each reflected target reference independently for every instance. ScenePolytree
then verifies that every target belongs to that spawned instance and resolves it through the
instance's binding table. A Prefab with no provider follows the existing behaviorless fast path.
The spawner has no behavior field and cannot replace, append, disable, or redirect a Prefab's
internal behavior.

The target mechanism is engine plumbing for a Prefab-specific behavior; it does not require a
generic behavior component per operation. The Windmill Gem can define one Windmill behavior,
author its `Rotor` reference in the Windmill Prefab, and use shared math internally. The spawner
does not attach a generic ConstantRotation behavior or choose which Windmill node it affects.

## Behavior libraries, lifecycle, and events

A controller library registers a `ScenePolytreeControllerFactory` through
`ScenePolytreeControllerRegistry`. Registration is keyed by `ScenePolytreeControllerTypeId` and
returns a generation-bearing token; duplicate types are rejected, and a factory cannot unregister
while any scene still owns that type. Registration and construction are cold-path operations.

The factory creates one `ScenePolytreeControllerBatch` per behavior type per scene, not one
heap-owned polymorphic object per spawned instance. `CreateController` is the one-shot
`self.start` equivalent: it receives the owning instance, the behavior's resolved authored
targets, and a scoped command sink. It adds compact instance state and may set persistent linear or
angular velocity on the `Rotor` target exactly once. All start commands are accumulated and
applied only after every behavior binding constructs successfully.

The command sink is also passed to `SubmitInput`, which is the cold event/intent entry point. A
Windmill Gem can store the returned public handle and submit a new angular velocity when wind
speed changes. That event writes the new persistent motion state once; the Windmill behavior is
not called while the rotor continues to turn.

`HasRunningControllers` is an explicit continuous-execution subscription, analogous to Wozzits
`frame.update`. ScenePolytree calls `FixedStepBatch` only for a type that returns true. Constant
motion behaviors return false. Fixed simulation steps still occur while persistent motion is
active, because the engine must integrate velocity, but no behavior virtual call or EBus/TickBus
dispatch occurs for those steps.

The scoped command sink permits local transform reads/writes, linear and angular velocity, and
motion stop against validated authored target tokens. It exposes neither O3DE entities nor
`SetWorldTM`. The generic hot path reuses motion-update and evaluation workspaces.

Gameplay obtains a public handle with `ScenePolytreeControllerRequests::FindController(instance,
declarationId)` and submits a controller-specific `ScenePolytreeControllerInput` through
`SubmitControllerInput`. The central runtime validates the instance, public controller generation,
and batch state, then refreshes scene tick eligibility. No raw controller pointer is returned.
Input closes before detach; late input returns `InputClosed`, and destroyed or superseded handles
return `StaleHandle`. Script-facing code may wrap this cold intent API, but it cannot enter the hot
node update or transform projection path.

## Behavior failures and lifetime

Controller attachment occurs only after exact logical binding succeeds, and spawn success is held
until every declaration has been validated and constructed. Missing factories, invalid
configuration, missing targets, cross-instance targets, write conflicts, and construction failure
map to distinct `SpawnError` and `ScenePolytreeResultCode` values. Attachment is transactional:
created batch states and target ownership are rolled back before the normal unbind, O3DE despawn,
reset, and release cleanup sequence.

The successful lifetime is:

```text
logical slot bound
    -> root-owned behavior declarations copied and authored node targets resolved
    -> one-shot self.start construction commands applied
    -> instance becomes Active
    -> engine advances persistent motion
    -> later behavior events may replace persistent motion state
    -> input closes
    -> controller states and unused batches are destroyed
    -> logical slot unbinds
    -> O3DE entities despawn
```

Immediate component deactivation performs the same controller-before-slot ordering. Public
controller tokens are generated independently of reusable batch-state indices, so destroying and
recreating a batch cannot make an earlier handle valid again.

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
