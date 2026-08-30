#pragma once

#include <concepts>
#include <type_traits>

#include <scene_polytree/motion/fixed_step.hpp>
#include <scene_polytree/motion/motion_state.hpp>

namespace scene_polytree::motion {
template <class Policy>
concept MotionPolicy =
    requires(Policy &policy, const typename Policy::transform_type &local,
             const motion_state<typename Policy::linear_velocity_type,
                                typename Policy::angular_velocity_type> &state,
             fixed_motion_step step) {
        typename Policy::transform_type;
        typename Policy::linear_velocity_type;
        typename Policy::angular_velocity_type;
        {
            policy.integrate(local, state, step)
        } noexcept -> std::same_as<typename Policy::transform_type>;
        { policy.is_stationary(state) } noexcept -> std::convertible_to<bool>;
    } &&
    std::copy_constructible<typename Policy::transform_type> &&
    std::is_nothrow_copy_assignable_v<typename Policy::transform_type> &&
    std::is_nothrow_move_assignable_v<typename Policy::transform_type>;
} // namespace scene_polytree::motion
