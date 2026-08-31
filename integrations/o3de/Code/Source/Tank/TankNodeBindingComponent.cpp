#include <ScenePolytree/Tank/TankNodeBindingComponent.h>

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace ScenePolytree {
AZ_COMPONENT_IMPL(TankNodeBindingComponent, "TankNodeBindingComponent",
                  TankNodeBindingComponentTypeId);

void TankNodeBindingComponent::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Enum<TankNodeRole>()
            ->Value("Hull", TankNodeRole::Hull)
            ->Value("Turret", TankNodeRole::Turret)
            ->Value("Gun", TankNodeRole::Gun);
        serializeContext->Class<TankNodeBindingComponent, AZ::Component>()->Version(1)->Field(
            "Role", &TankNodeBindingComponent::m_role);
        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<TankNodeBindingComponent>(
                    "Scene Polytree Tank Node Binding",
                    "Marks a parentless visual entity as a tank projection target.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->DataElement(AZ::Edit::UIHandlers::ComboBox, &TankNodeBindingComponent::m_role,
                              "Role", "Logical tank node projected to this entity.");
        }
    }
}

void TankNodeBindingComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeTankNodeBindingService"));
}

void TankNodeBindingComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeTankNodeBindingService"));
}

void TankNodeBindingComponent::GetRequiredServices(
    AZ::ComponentDescriptor::DependencyArrayType &required) {
    required.push_back(AZ_CRC_CE("TransformService"));
}
} // namespace ScenePolytree
