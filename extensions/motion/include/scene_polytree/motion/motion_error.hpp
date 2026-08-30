#pragma once

namespace scene_polytree::motion {
enum class motion_error {
    none,
    invalid_node,
    invalid_step,
    tick_exhausted,
    state_size_mismatch,
    topology_mismatch,
    revision_exhausted,
    transform_failure,
};
} // namespace scene_polytree::motion
