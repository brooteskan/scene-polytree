#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include <graph/handles.h>

#include <scene_polytree/authoring_scene.hpp>
#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_freeze.hpp>

namespace scene_polytree::benchmarks::fixture {
struct vector3 {
    double x{};
    double y{};
    double z{};

    friend constexpr bool operator==(const vector3 &, const vector3 &) = default;
};

struct quaternion {
    double w{1.0};
    double x{};
    double y{};
    double z{};

    friend constexpr bool operator==(const quaternion &, const quaternion &) = default;
};

struct rigid_pose {
    vector3 translation;
    quaternion rotation;

    friend constexpr bool operator==(const rigid_pose &, const rigid_pose &) = default;
};

struct rigid_policy {
    using transform_type = rigid_pose;
    using linear_velocity_type = vector3;
    using angular_velocity_type = vector3;

    std::size_t *integration_calls{};
    std::size_t *composition_calls{};

    [[nodiscard]] rigid_pose compose(const rigid_pose &parent_world,
                                     const rigid_pose &local) noexcept;
    [[nodiscard]] rigid_pose integrate(const rigid_pose &local,
                                       const motion::motion_state<vector3, vector3> &state,
                                       motion::fixed_motion_step step) noexcept;
    [[nodiscard]] bool is_stationary(const motion::motion_state<vector3, vector3> &state) noexcept;
};

enum class hierarchy_node : std::uint8_t {
    root,
    yaw_pivot,
    pitch_pivot,
};

enum class hierarchy_joint : std::uint8_t {
    yaw,
    pitch,
};

struct hierarchy_asset {
    rigid_pose root;
    rigid_pose yaw_pivot{{0.0, 0.0, 1.0}, {}};
    rigid_pose pitch_pivot{{0.0, 1.0, 0.25}, {}};
};

struct hierarchy_instance {
    wz::core::graph::StableNodeId root;
    wz::core::graph::StableNodeId yaw;
    wz::core::graph::StableNodeId pitch;
};

struct runtime_hierarchy_instance {
    wz::core::graph::NodeHandle root{wz::core::graph::INVALID_NODE};
    wz::core::graph::NodeHandle yaw{wz::core::graph::INVALID_NODE};
    wz::core::graph::NodeHandle pitch{wz::core::graph::INVALID_NODE};
};

using authoring_scene = basic_authoring_scene<hierarchy_node, hierarchy_joint, rigid_pose>;
using runtime_scene = basic_runtime_scene<hierarchy_node, hierarchy_joint, rigid_pose>;
using active_set = motion::active_motion_set<vector3, vector3>;

[[nodiscard]] hierarchy_instance instantiate_hierarchy(authoring_scene &scene,
                                                            const hierarchy_asset &asset,
                                                            rigid_pose spawn_pose);
[[nodiscard]] runtime_hierarchy_instance resolve_hierarchy(const runtime_scene &scene,
                                                                hierarchy_instance instance);

struct motion_update {
    double forward_speed{};
    double root_yaw_rate{};
    double yaw_rate{};
    double pitch_rate{};

    friend constexpr bool operator==(const motion_update &, const motion_update &) = default;
};

[[nodiscard]] motion::motion_error apply_command(active_set &active,
                                                 runtime_hierarchy_instance instance,
                                                 const motion_update &command,
                                                 rigid_policy &policy);
} // namespace scene_polytree::benchmarks::fixture
