#pragma once

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>

namespace ScenePolytree {
class TankNodeBindingComponent final : public AZ::Component {
  public:
    AZ_COMPONENT_DECL(TankNodeBindingComponent);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);
    static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType &required);

    TankNodeBindingComponent() = default;
    explicit TankNodeBindingComponent(TankNodeRole role) : m_role(role) {}

    [[nodiscard]] TankNodeRole GetRole() const noexcept { return m_role; }

    void Activate() override {}
    void Deactivate() override {}

  private:
    TankNodeRole m_role{TankNodeRole::Hull};
};
} // namespace ScenePolytree
