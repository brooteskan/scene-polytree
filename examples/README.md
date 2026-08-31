# Examples

## Articulated tank

`articulated_tank` is a private, headless example and test fixture. It creates
two instances of the same engine-neutral hull/turret/gun asset, then drives one
with player input and one with an AI goal. Both controllers produce
the same `tank_intent`; only `apply_intent` translates that intent into generic
motion-state updates.

The example verifies three architectural properties:

- controller code does not propagate transforms or traverse the hierarchy;
- hull, turret, and gun motion uses the same centralized fixed-step evaluator;
- equivalent player and AI intent remains transform-equivalent across repeated
  fixed ticks, while turret yaw and gun pitch preserve their authored pivots;
- writing stationary intent removes nodes from the active set, so a later tick
  performs no integration or composition callbacks.

Tank types are deliberately not installed and are not part of the
`scene-polytree` API. The fixture exists to exercise articulated motion through
a concrete hierarchy.
