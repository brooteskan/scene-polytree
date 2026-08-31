#pragma once

#include <ScenePolytree/ScenePolytreeTypeIds.h>
#include <ScenePolytree/ScenePolytreeTypes.h>

#include <AzCore/Component/Component.h>

namespace ScenePolytree {
class ScenePolytreePrefabNodeComponent final : public AZ::Component {
  public:
    AZ_COMPONENT(ScenePolytreePrefabNodeComponent, ScenePolytreePrefabNodeComponentTypeId);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);
    static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType &required);

    ScenePolytreePrefabNodeComponent() = default;
    ScenePolytreePrefabNodeComponent(AZ::Name bindingId, AZ::Name parentBindingId,
                                     ScenePolytreeJointType jointType)
        : m_bindingId(AZStd::move(bindingId)), m_parentBindingId(AZStd::move(parentBindingId)),
          m_jointType(jointType) {}

    [[nodiscard]] const AZ::Name &GetBindingId() const noexcept { return m_bindingId; }
    [[nodiscard]] const AZ::Name &GetParentBindingId() const noexcept { return m_parentBindingId; }
    [[nodiscard]] ScenePolytreeNodeType GetNodeType() const noexcept { return m_nodeType; }
    [[nodiscard]] ScenePolytreeJointType GetJointType() const noexcept { return m_jointType; }

    void Activate() override {}
    void Deactivate() override {}

  private:
    AZ::Name m_bindingId;
    AZ::Name m_parentBindingId;
    ScenePolytreeNodeType m_nodeType{ScenePolytreeNodeType::Transform};
    ScenePolytreeJointType m_jointType{ScenePolytreeJointType::None};
};
} // namespace ScenePolytree
