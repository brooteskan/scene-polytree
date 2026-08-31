#include <ScenePolytree/Tank/TankArticulationBindingComponent.h>

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace ScenePolytree {
AZ_COMPONENT_IMPL(TankArticulationBindingComponent, "TankArticulationBindingComponent",
                  TankArticulationBindingComponentTypeId);

void TankArticulationBindingComponent::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<TankArticulationBindingComponent, AZ::Component>()
            ->Version(1)
            ->Field("TurretPivot", &TankArticulationBindingComponent::m_turretPivot)
            ->Field("GunPivot", &TankArticulationBindingComponent::m_gunPivot)
            ->Field("AssetToLogicalBasis",
                    &TankArticulationBindingComponent::m_assetToLogicalBasis);
        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<TankArticulationBindingComponent>(
                    "Scene Polytree Tank Articulation Binding",
                    "Defines authored pivot entities and the asset-to-logical basis.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &TankArticulationBindingComponent::m_turretPivot, "Turret Pivot",
                              "Authored pivot for turret yaw.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &TankArticulationBindingComponent::m_gunPivot, "Gun Pivot",
                              "Authored pivot for gun pitch.")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &TankArticulationBindingComponent::m_assetToLogicalBasis,
                    "Asset To Logical Basis",
                    "Rigid transform mapping the authored asset frame to +Y forward, +Z up.");
        }
    }
}

void TankArticulationBindingComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeTankArticulationBindingService"));
}

void TankArticulationBindingComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeTankArticulationBindingService"));
}

void TankArticulationBindingComponent::GetRequiredServices(
    AZ::ComponentDescriptor::DependencyArrayType &required) {
    required.push_back(AZ_CRC_CE("TransformService"));
}
} // namespace ScenePolytree
