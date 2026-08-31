#include <ScenePolytree/ScenePolytreePrefabNodeComponent.h>

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace ScenePolytree {
void ScenePolytreePrefabNodeComponent::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Enum<ScenePolytreeNodeType>()->Value("Transform",
                                                               ScenePolytreeNodeType::Transform);
        serializeContext->Enum<ScenePolytreeJointType>()
            ->Value("None", ScenePolytreeJointType::None)
            ->Value("Fixed", ScenePolytreeJointType::Fixed)
            ->Value("Yaw", ScenePolytreeJointType::Yaw)
            ->Value("Pitch", ScenePolytreeJointType::Pitch);
        serializeContext->Class<ScenePolytreePrefabNodeComponent, AZ::Component>()
            ->Version(1)
            ->Field("BindingId", &ScenePolytreePrefabNodeComponent::m_bindingId)
            ->Field("ParentBindingId", &ScenePolytreePrefabNodeComponent::m_parentBindingId)
            ->Field("NodeType", &ScenePolytreePrefabNodeComponent::m_nodeType)
            ->Field("JointType", &ScenePolytreePrefabNodeComponent::m_jointType);

        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<ScenePolytreePrefabNodeComponent>(
                    "Scene Polytree Prefab Node",
                    "Defines one logical scene-polytree node inside an O3DE Prefab.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreePrefabNodeComponent::m_bindingId, "Binding ID",
                              "Prefab-local stable logical node identifier.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreePrefabNodeComponent::m_parentBindingId,
                              "Parent Binding ID", "Empty for a logical root.")
                ->DataElement(AZ::Edit::UIHandlers::ComboBox,
                              &ScenePolytreePrefabNodeComponent::m_nodeType, "Node Type",
                              "Logical node implementation type.")
                ->DataElement(AZ::Edit::UIHandlers::ComboBox,
                              &ScenePolytreePrefabNodeComponent::m_jointType, "Joint Type",
                              "Relationship to the logical parent; roots use None.");
        }
    }
}

void ScenePolytreePrefabNodeComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreePrefabNodeService"));
}

void ScenePolytreePrefabNodeComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreePrefabNodeService"));
}

void ScenePolytreePrefabNodeComponent::GetRequiredServices(
    AZ::ComponentDescriptor::DependencyArrayType &required) {
    required.push_back(AZ_CRC_CE("TransformService"));
}
} // namespace ScenePolytree
