#include <ScenePolytree/ScenePolytreeBus.h>

#include <AzCore/Math/MathUtils.h>

namespace ScenePolytree {
TankIntent MakePlayerTankIntent(const PlayerTankInput &input, const TankTuning &tuning) noexcept {
    return {
        AZ::GetClamp(input.m_moveAxis, -1.0f, 1.0f) * tuning.m_maxForwardSpeed,
        AZ::GetClamp(input.m_turnAxis, -1.0f, 1.0f) * tuning.m_maxHullYawRate,
        AZ::GetClamp(input.m_turretAxis, -1.0f, 1.0f) * tuning.m_maxTurretYawRate,
        AZ::GetClamp(input.m_gunAxis, -1.0f, 1.0f) * tuning.m_maxGunPitchRate,
    };
}

TankIntent MakeAiTankIntent(const AiTankGoal &goal) noexcept {
    return {goal.m_forwardSpeed, goal.m_hullYawRate, goal.m_turretYawRate, goal.m_gunPitchRate};
}
} // namespace ScenePolytree
