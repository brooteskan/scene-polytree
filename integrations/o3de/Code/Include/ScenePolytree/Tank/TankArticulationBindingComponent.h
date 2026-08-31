#pragma once

#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Transform.h>

namespace ScenePolytree {
class TankArticulationBindingComponent final : public AZ::Component {
  public:
    AZ_COMPONENT_DECL(TankArticulationBindingComponent);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);
    static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType &required);

    TankArticulationBindingComponent() = default;
    TankArticulationBindingComponent(AZ::EntityId turretPivot, AZ::EntityId gunPivot,
                                     const AZ::Transform &assetToLogicalBasis)
        : m_turretPivot(turretPivot), m_gunPivot(gunPivot),
          m_assetToLogicalBasis(assetToLogicalBasis) {}

    [[nodiscard]] AZ::EntityId GetTurretPivot() const noexcept { return m_turretPivot; }
    [[nodiscard]] AZ::EntityId GetGunPivot() const noexcept { return m_gunPivot; }
    [[nodiscard]] const AZ::Transform &GetAssetToLogicalBasis() const noexcept {
        return m_assetToLogicalBasis;
    }

    void Activate() override {}
    void Deactivate() override {}

  private:
    AZ::EntityId m_turretPivot;
    AZ::EntityId m_gunPivot;
    AZ::Transform m_assetToLogicalBasis{AZ::Transform::CreateIdentity()};
};
} // namespace ScenePolytree
