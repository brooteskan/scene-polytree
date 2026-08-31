#pragma once

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>

namespace ScenePolytree {
class PlayerTankIntentAdapterComponent final : public AZ::Component,
                                               public AzFramework::InputChannelEventListener,
                                               public TankAdapterRequestBus::Handler {
  public:
    AZ_COMPONENT_DECL(PlayerTankIntentAdapterComponent);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);

    void Activate() override;
    void Deactivate() override;
    void AssignTank(TankHandle tank) override;

  protected:
    bool OnInputChannelEventFiltered(const AzFramework::InputChannel &inputChannel) override;

  private:
    void SubmitCurrentIntent();

    TankHandle m_tank;
    TankTuning m_tuning;
    PlayerTankInput m_input;
    float m_forwardKey{};
    float m_backwardKey{};
    float m_leftKey{};
    float m_rightKey{};
    float m_turretLeftKey{};
    float m_turretRightKey{};
    float m_gunUpKey{};
    float m_gunDownKey{};
};
} // namespace ScenePolytree
