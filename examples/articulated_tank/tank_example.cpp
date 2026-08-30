#include "tank_example.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <scene_polytree/transform_evaluation.hpp>

namespace scene_polytree::examples::tank {
namespace {
[[nodiscard]] constexpr vector3 operator+(vector3 left, vector3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr vector3 operator*(vector3 value, double scalar) noexcept {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] constexpr quaternion multiply(quaternion left, quaternion right) noexcept {
    return {
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
    };
}

[[nodiscard]] quaternion normalized(quaternion value) noexcept {
    const auto magnitude =
        std::sqrt(value.w * value.w + value.x * value.x + value.y * value.y + value.z * value.z);
    return magnitude == 0.0 ? quaternion{}
                            : quaternion{value.w / magnitude, value.x / magnitude,
                                         value.y / magnitude, value.z / magnitude};
}

[[nodiscard]] constexpr quaternion conjugate(quaternion value) noexcept {
    return {value.w, -value.x, -value.y, -value.z};
}

[[nodiscard]] vector3 rotate(quaternion rotation, vector3 value) noexcept {
    const auto rotated =
        multiply(multiply(rotation, {0.0, value.x, value.y, value.z}), conjugate(rotation));
    return {rotated.x, rotated.y, rotated.z};
}

[[nodiscard]] quaternion rotation_delta(vector3 angular_velocity, double seconds) noexcept {
    const auto speed = std::sqrt(angular_velocity.x * angular_velocity.x +
                                 angular_velocity.y * angular_velocity.y +
                                 angular_velocity.z * angular_velocity.z);
    if (speed == 0.0) {
        return {};
    }
    const auto half_angle = speed * seconds * 0.5;
    const auto scale = std::sin(half_angle) / speed;
    return normalized({std::cos(half_angle), angular_velocity.x * scale, angular_velocity.y * scale,
                       angular_velocity.z * scale});
}

[[nodiscard]] rigid_pose compose_pose(const rigid_pose &parent, const rigid_pose &local) noexcept {
    return {
        parent.translation + rotate(parent.rotation, local.translation),
        normalized(multiply(parent.rotation, local.rotation)),
    };
}
} // namespace

rigid_pose rigid_policy::compose(const rigid_pose &parent_world, const rigid_pose &local) noexcept {
    if (composition_calls != nullptr) {
        ++*composition_calls;
    }
    return compose_pose(parent_world, local);
}

rigid_pose rigid_policy::integrate(const rigid_pose &local,
                                   const motion::motion_state<vector3, vector3> &state,
                                   motion::fixed_motion_step step) noexcept {
    if (integration_calls != nullptr) {
        ++*integration_calls;
    }
    const auto seconds = static_cast<double>(step.delta.count()) / 1'000'000'000.0;
    return {
        local.translation + state.linear_velocity * seconds,
        normalized(multiply(local.rotation, rotation_delta(state.angular_velocity, seconds))),
    };
}

bool rigid_policy::is_stationary(const motion::motion_state<vector3, vector3> &state) noexcept {
    return state.linear_velocity == vector3{} && state.angular_velocity == vector3{};
}

tank_instance instantiate_tank(authoring_scene &scene, const tank_asset &asset,
                               rigid_pose spawn_pose) {
    const auto hull =
        scene.insert_root(tank_node::hull, compose_pose(spawn_pose, asset.hull)).value();
    const auto turret =
        scene.insert_child(hull, tank_node::turret_pivot, tank_joint::turret, asset.turret_pivot)
            .value();
    const auto gun =
        scene.insert_child(turret, tank_node::gun_pivot, tank_joint::gun, asset.gun_pivot).value();
    return {hull, turret, gun};
}

runtime_tank_instance resolve_tank(const runtime_scene &scene, tank_instance instance) {
    return {
        scene.identities().runtime_handle(instance.hull).value(),
        scene.identities().runtime_handle(instance.turret).value(),
        scene.identities().runtime_handle(instance.gun).value(),
    };
}

tank_intent player_intent_producer::operator()(const player_input &input) const noexcept {
    return {input.move_axis, input.turn_axis, input.turret_axis, input.gun_axis};
}

tank_intent ai_intent_producer::operator()(const ai_goal &goal) const noexcept {
    return {goal.forward_speed, goal.hull_yaw_rate, goal.turret_yaw_rate, goal.gun_pitch_rate};
}

motion::motion_error apply_intent(active_set &active, runtime_tank_instance instance,
                                  const tank_intent &intent, rigid_policy &policy) {
    using update = motion::motion_update<vector3, vector3>;
    const std::array updates{
        update{instance.hull, {{0.0, intent.forward_speed, 0.0}, {0.0, 0.0, intent.hull_yaw_rate}}},
        update{instance.turret, {{}, {0.0, 0.0, intent.turret_yaw_rate}}},
        update{instance.gun, {{}, {intent.gun_pitch_rate, 0.0, 0.0}}},
    };
    return active.apply_updates(updates, policy);
}

bool approximately_equal(double left, double right, double tolerance) noexcept {
    return std::abs(left - right) <= tolerance;
}

bool run_demo() {
    authoring_scene authoring;
    const tank_asset asset;
    const auto player = instantiate_tank(authoring, asset, {{-5.0, 0.0, 0.0}, {}});
    const auto ai = instantiate_tank(authoring, asset, {{5.0, 0.0, 0.0}, {}});

    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return false;
    }
    auto runtime = std::move(frozen).value();
    const auto runtime_player = resolve_tank(runtime, player);
    const auto runtime_ai = resolve_tank(runtime, ai);

    active_set active{runtime.topology()};
    std::size_t integration_calls = 0;
    std::size_t composition_calls = 0;
    rigid_policy policy{&integration_calls, &composition_calls};
    const player_input player_command{2.0, 0.25, -0.5, 0.125};
    const ai_goal ai_command{2.0, 0.25, -0.5, 0.125};
    const auto player_intent = player_intent_producer{}(player_command);
    const auto ai_intent = ai_intent_producer{}(ai_command);
    if (player_intent != ai_intent ||
        apply_intent(active, runtime_player, player_intent, policy) != motion::motion_error::none ||
        apply_intent(active, runtime_ai, ai_intent, policy) != motion::motion_error::none) {
        return false;
    }

    motion::fixed_step_sequence sequence{std::chrono::milliseconds{100}};
    motion::motion_evaluation_workspace<rigid_pose> motion_workspace;
    transform_evaluation_workspace transform_workspace;
    const auto first =
        motion::advance_motion_scene(runtime.topology(), runtime.state(), active, sequence,
                                     motion_workspace, transform_workspace, policy, policy);
    if (!first || first.integrated_nodes.size() != 6 || first.changed_nodes.size() != 6 ||
        runtime.state().local(runtime_player.turret) != runtime.state().local(runtime_ai.turret) ||
        runtime.state().local(runtime_player.gun) != runtime.state().local(runtime_ai.gun) ||
        !approximately_equal(runtime.state().local(runtime_player.hull).translation.y, 0.2) ||
        !approximately_equal(runtime.state().local(runtime_ai.hull).translation.y, 0.2)) {
        return false;
    }

    if (apply_intent(active, runtime_player, {}, policy) != motion::motion_error::none ||
        apply_intent(active, runtime_ai, {}, policy) != motion::motion_error::none ||
        !active.empty()) {
        return false;
    }
    integration_calls = 0;
    composition_calls = 0;
    const auto stationary =
        motion::advance_motion_scene(runtime.topology(), runtime.state(), active, sequence,
                                     motion_workspace, transform_workspace, policy, policy);
    return stationary && stationary.integrated_nodes.empty() && stationary.changed_nodes.empty() &&
           integration_calls == 0 && composition_calls == 0 && sequence.next_tick() == 2;
}
} // namespace scene_polytree::examples::tank
