#include "benchmark_fixture.hpp"

#include <array>
#include <cmath>

namespace scene_polytree::benchmarks::fixture {
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

articulated_instance instantiate_articulation(authoring_scene &scene,
                                              const articulated_asset &asset,
                                              rigid_pose spawn_pose) {
    const auto root =
        scene.insert_root(articulated_node::root, compose_pose(spawn_pose, asset.root)).value();
    const auto yaw = scene
                         .insert_child(root, articulated_node::yaw_pivot, articulated_joint::yaw,
                                       asset.yaw_pivot)
                         .value();
    const auto pitch = scene
                           .insert_child(yaw, articulated_node::pitch_pivot,
                                         articulated_joint::pitch, asset.pitch_pivot)
                           .value();
    return {root, yaw, pitch};
}

runtime_articulated_instance resolve_articulation(const runtime_scene &scene,
                                                  articulated_instance instance) {
    return {
        scene.identities().runtime_handle(instance.root).value(),
        scene.identities().runtime_handle(instance.yaw).value(),
        scene.identities().runtime_handle(instance.pitch).value(),
    };
}

motion::motion_error apply_command(active_set &active, runtime_articulated_instance instance,
                                   const motion_command &command, rigid_policy &policy) {
    using update = motion::motion_update<vector3, vector3>;
    const std::array updates{
        update{instance.root,
               {{0.0, command.forward_speed, 0.0}, {0.0, 0.0, command.root_yaw_rate}}},
        update{instance.yaw, {{}, {0.0, 0.0, command.yaw_rate}}},
        update{instance.pitch, {{}, {command.pitch_rate, 0.0, 0.0}}},
    };
    return active.apply_updates(updates, policy);
}
} // namespace scene_polytree::benchmarks::fixture
