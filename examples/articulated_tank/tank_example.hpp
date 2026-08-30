#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include <graph/handles.h>

#include <scene_polytree/authoring_scene.hpp>
#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_freeze.hpp>

namespace scene_polytree::examples::tank {
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

enum class tank_node : std::uint8_t {
    hull,
    turret_pivot,
    gun_pivot,
};

enum class tank_joint : std::uint8_t {
    turret,
    gun,
};

struct tank_asset {
    rigid_pose hull;
    rigid_pose turret_pivot{{0.0, 0.0, 1.0}, {}};
    rigid_pose gun_pivot{{0.0, 1.0, 0.25}, {}};
};

struct tank_instance {
    wz::core::graph::StableNodeId hull;
    wz::core::graph::StableNodeId turret;
    wz::core::graph::StableNodeId gun;
};

struct runtime_tank_instance {
    wz::core::graph::NodeHandle hull{wz::core::graph::INVALID_NODE};
    wz::core::graph::NodeHandle turret{wz::core::graph::INVALID_NODE};
    wz::core::graph::NodeHandle gun{wz::core::graph::INVALID_NODE};
};

using authoring_scene = basic_authoring_scene<tank_node, tank_joint, rigid_pose>;
using runtime_scene = basic_runtime_scene<tank_node, tank_joint, rigid_pose>;
using active_set = motion::active_motion_set<vector3, vector3>;

[[nodiscard]] tank_instance instantiate_tank(authoring_scene &scene, const tank_asset &asset,
                                             rigid_pose spawn_pose);

[[nodiscard]] runtime_tank_instance resolve_tank(const runtime_scene &scene,
                                                 tank_instance instance);

struct tank_intent {
    double forward_speed{};
    double hull_yaw_rate{};
    double turret_yaw_rate{};
    double gun_pitch_rate{};

    friend constexpr bool operator==(const tank_intent &, const tank_intent &) = default;
};

struct player_input {
    double move_axis{};
    double turn_axis{};
    double turret_axis{};
    double gun_axis{};
};

struct ai_goal {
    double forward_speed{};
    double hull_yaw_rate{};
    double turret_yaw_rate{};
    double gun_pitch_rate{};
};

struct player_intent_producer {
    [[nodiscard]] tank_intent operator()(const player_input &input) const noexcept;
};

struct ai_intent_producer {
    [[nodiscard]] tank_intent operator()(const ai_goal &goal) const noexcept;
};

[[nodiscard]] motion::motion_error apply_intent(active_set &active, runtime_tank_instance instance,
                                                const tank_intent &intent, rigid_policy &policy);

[[nodiscard]] bool approximately_equal(double left, double right,
                                       double tolerance = 1.0e-9) noexcept;

[[nodiscard]] bool run_demo();
} // namespace scene_polytree::examples::tank
