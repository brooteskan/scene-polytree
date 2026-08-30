#pragma once

#include <graph/handles.h>

namespace scene_polytree::motion {
template <class LinearVelocity, class AngularVelocity> struct motion_state {
    LinearVelocity linear_velocity{};
    AngularVelocity angular_velocity{};
};

template <class LinearVelocity, class AngularVelocity> struct motion_update {
    wz::core::graph::NodeHandle node{wz::core::graph::INVALID_NODE};
    motion_state<LinearVelocity, AngularVelocity> state;
};

template <class LinearVelocity, class AngularVelocity> struct active_motion_record {
    wz::core::graph::NodeHandle node{wz::core::graph::INVALID_NODE};
    motion_state<LinearVelocity, AngularVelocity> state;
};
} // namespace scene_polytree::motion
