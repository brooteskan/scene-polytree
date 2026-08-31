# Motion extension

The optional `scene-polytree::motion` target adds deterministic fixed-step
local-transform integration without owning a second hierarchy or controller
model. It depends on `scene-polytree::core`; the core has no dependency on
motion.

## Data and policy

`motion_state<LinearVelocity, AngularVelocity>` stores only integration data.
Whether a state is stationary, how velocities combine, and how a local
transform changes over a step are supplied by a `MotionPolicy`. The policy must
provide:

- `transform_type`, `linear_velocity_type`, and `angular_velocity_type`;
- `integrate(local, state, fixed_motion_step) noexcept`;
- `is_stationary(state) noexcept`.

This keeps vector, quaternion, units, damping, constraints, and coordinate
system choices in the consumer. Player input, AI goals, and other intent are
also consumer concerns: intent producers write equivalent motion-state updates
rather than becoming alternate transform evaluators.

## Active storage

`active_motion_set` belongs to one frozen topology. Records are sorted by
`NodeHandle`, so integration order is stable and independent of registration
order. Calling `set` with stationary state removes the record. `deactivate` and
`clear` are explicit alternatives, while `apply_updates` accepts a batch from
controller or simulation code.

The set exposes records in ascending handle order without allocation. The
two-argument `apply_updates(updates, policy)` overload preserves the original
per-record behavior: lookup is `O(log A)` for `A` active nodes, insertion and
removal can move `O(A)` records, and a batch of `U` updates can therefore cost
`O(U * A)`.

Frame-time bulk maintenance should use the three-argument overload with an
`active_motion_update_workspace`. It validates every handle before mutation,
copies and sorts updates by `NodeHandle`, coalesces duplicates with
last-write-wins semantics, and performs one linear merge with the current
records. Its time is `O(U log U + A + U)` and its retained scratch is
`O(A + U)`. Both set buffers and workspace buffers retain capacity, so repeated
same-size batches allocate zero bytes after the first call. An invalid handle
leaves records, record order, and the mutation generation unchanged. Any
successful logical mutation can invalidate the record span and references into
it.

The set is sparse because motion is optional and typical scenes have many more
stationary than moving nodes. The benchmark compares this choice with a dense
state-plus-active-flag candidate; see
[`motion-storage-results.md`](motion-storage-results.md).

## Fixed-step evaluation

`advance_motion_scene` is the single integration entry point:

1. Validate the fixed step, topology identity, state size, node handles, and
   revision headroom without mutation.
2. Map active records to new local transforms through the motion policy.
3. Apply those local changes to transform state.
4. Build a normal dirty transform plan and evaluate affected nodes through the
   transform policy.
5. Advance the sequence tick only after success.

Integration visits `A` active records. After topology metadata is prepared,
dirty planning visits the exact direct-dirty frontier and the affected
subtrees, and transform composition visits only the `C` selected nodes. Motion
scratch is `O(A)` and is retained by the caller-owned workspace; capacity
growth may allocate, while reuse within capacity does not. Transform planning
has the allocation and complexity behavior documented by the core transform
contract.

The returned `motion_evaluation_result` contains the nodes integrated directly,
the nodes whose world transforms changed after descendant propagation, the
completed step, and resulting world revision. Its spans borrow workspace/state
storage and are valid only until either is reused or mutated. Calls require
external synchronization; neither the scene nor its workspaces are internally
thread-safe.

An empty active set is valid. If no transform was already dirty, the evaluator
performs no integration or transform-composition callbacks and reports empty
node spans while still completing the fixed tick.

Validation errors return before changing local transforms or the tick. The
sequence rejects a non-positive duration and refuses to execute tick
`UINT64_MAX`; callers may provide an initial tick when constructing the
sequence. Standard allocation failures retain normal C++ exception behavior.

## Determinism boundaries

For the same frozen topology, initial transform state, exact ordered intent
updates, fixed-step sequence, and motion/transform policies, the extension
produces the same integration order, changed-node order, tick progression, and
policy results. Active records are integrated in ascending `NodeHandle` order,
so registration order among distinct nodes is not observable once their final
motion states match. Multiple updates for one node use last-write-wins
semantics. The optimized batch overload makes only the final update observable
and emits records in ascending `NodeHandle` order regardless of input order.

`fixed_step_sequence` is deliberately not a wall-clock scheduler. The caller
owns elapsed-time accumulation, catch-up limits, pausing, interpolation, and the
number of fixed evaluations requested. A step exposes its current tick and
exact integer-nanosecond duration to the policy; the tick advances once only
after the complete motion and transform evaluation succeeds.

The library guarantees deterministic ordering, not cross-platform bitwise
floating-point identity. A consumer that requires bitwise replay must supply
policies and numeric types with that property and must keep external intent
ordering deterministic. Coordinate conventions, damping, constraints,
stationary thresholds, and velocity reference frames are likewise policy
decisions. Intent updates and evaluation require external synchronization.

## Example

```cpp
scene_polytree::motion::active_motion_set<velocity, angular_velocity> active{
    runtime.topology()};
motion_policy motion;
transform_policy transforms;

active.set(node, {linear, angular}, motion);

std::vector<decltype(active)::update_type> updates = make_updates();
scene_polytree::motion::active_motion_update_workspace<velocity,
                                                        angular_velocity>
    update_workspace;
active.apply_updates(updates, motion, update_workspace);

scene_polytree::motion::fixed_step_sequence steps{std::chrono::milliseconds{16}};
scene_polytree::motion::motion_evaluation_workspace<pose> motion_workspace;
scene_polytree::transform_evaluation_workspace transform_workspace;

const auto result = scene_polytree::motion::advance_motion_scene(
    runtime.topology(), runtime.state(), active, steps, motion_workspace,
    transform_workspace, motion, transforms);
```

The benchmark runner exercises this API with a generic three-node articulation
fixture. Its root, yaw pivot, and pitch pivot share one active set and one
centralized fixed-step evaluation path.
