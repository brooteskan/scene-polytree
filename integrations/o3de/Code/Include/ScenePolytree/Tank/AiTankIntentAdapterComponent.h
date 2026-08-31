#pragma once

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>

namespace ScenePolytree {
class AiTankIntentAdapterComponent final : public AZ::Component,
                                           public TankAdapterRequestBus::Handler,
                                           public AiTankGoalRequestBus::Handler {
  public:
    AZ_COMPONENT_DECL(AiTankIntentAdapterComponent);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);

    void Activate() override;
    void Deactivate() override;
    void AssignTank(TankHandle tank) override;
    void SubmitGoal(const AiTankGoal &goal) override;

  private:
    void SubmitCurrentGoal();

    TankHandle m_tank;
    AiTankGoal m_goal;
};
} // namespace ScenePolytree
