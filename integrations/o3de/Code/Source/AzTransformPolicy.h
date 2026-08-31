#pragma once

#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>

#include <scene_polytree/motion/motion.hpp>

namespace ScenePolytree::Internal {
//! O3DE's AZ::Transform assignment is not declared noexcept. The core runtime deliberately
//! requires noexcept state replacement, so the host adapter owns that guarantee explicitly.
struct AzTransformValue {
    AZ::Transform m_value{AZ::Transform::CreateIdentity()};

    AzTransformValue() = default;
    explicit AzTransformValue(const AZ::Transform &value) noexcept : m_value(value) {}
    AzTransformValue(const AzTransformValue &other) noexcept : m_value(other.m_value) {}
    AzTransformValue(AzTransformValue &&other) noexcept : m_value(other.m_value) {}
    AzTransformValue &operator=(const AzTransformValue &other) noexcept {
        m_value = other.m_value;
        return *this;
    }
    AzTransformValue &operator=(AzTransformValue &&other) noexcept {
        m_value = other.m_value;
        return *this;
    }
};

struct AzTransformPolicy {
    using transform_type = AzTransformValue;
    using linear_velocity_type = AZ::Vector3;
    using angular_velocity_type = AZ::Vector3;

    [[nodiscard]] AzTransformValue compose(const AzTransformValue &parentWorld,
                                           const AzTransformValue &local) noexcept;
    [[nodiscard]] AzTransformValue
    integrate(const AzTransformValue &local,
              const scene_polytree::motion::motion_state<AZ::Vector3, AZ::Vector3> &state,
              scene_polytree::motion::fixed_motion_step step) noexcept;
    [[nodiscard]] bool is_stationary(
        const scene_polytree::motion::motion_state<AZ::Vector3, AZ::Vector3> &state) noexcept;
};
} // namespace ScenePolytree::Internal
