#include "AzTransformPolicy.h"

#include <AzCore/Math/Quaternion.h>

namespace ScenePolytree::Internal {
AzTransformValue AzTransformPolicy::compose(const AzTransformValue &parentWorld,
                                            const AzTransformValue &local) noexcept {
    return AzTransformValue(parentWorld.m_value * local.m_value);
}

AzTransformValue AzTransformPolicy::integrate(
    const AzTransformValue &local,
    const scene_polytree::motion::motion_state<AZ::Vector3, AZ::Vector3> &state,
    scene_polytree::motion::fixed_motion_step step) noexcept {
    const float seconds = static_cast<float>(step.delta.count()) / 1'000'000'000.0f;
    AZ::Transform result = local.m_value;
    result.SetTranslation(local.m_value.GetTranslation() +
                          local.m_value.GetRotation().TransformVector(state.linear_velocity) *
                              seconds);
    const AZ::Quaternion delta =
        AZ::Quaternion::CreateFromEulerAnglesRadians(state.angular_velocity * seconds);
    result.SetRotation((local.m_value.GetRotation() * delta).GetNormalized());
    return AzTransformValue(result);
}

bool AzTransformPolicy::is_stationary(
    const scene_polytree::motion::motion_state<AZ::Vector3, AZ::Vector3> &state) noexcept {
    return state.linear_velocity.IsZero() && state.angular_velocity.IsZero();
}
} // namespace ScenePolytree::Internal
