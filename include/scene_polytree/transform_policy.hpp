#pragma once

#include <concepts>
#include <type_traits>

namespace scene_polytree {
template <class Policy>
concept TransformPolicy =
    requires(Policy &policy, const typename Policy::transform_type &parent_world,
             const typename Policy::transform_type &local) {
        typename Policy::transform_type;
        {
            policy.compose(parent_world, local)
        } noexcept -> std::same_as<typename Policy::transform_type>;
    } &&
    std::copy_constructible<typename Policy::transform_type> &&
    std::is_nothrow_copy_assignable_v<typename Policy::transform_type> &&
    std::is_nothrow_move_assignable_v<typename Policy::transform_type>;
} // namespace scene_polytree
