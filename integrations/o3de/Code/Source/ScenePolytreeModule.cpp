#include "ScenePolytreeModule.h"

namespace ScenePolytree {
ScenePolytreeModule::ScenePolytreeModule() {
    m_descriptors.insert(m_descriptors.end(),
                         {
                             ScenePolytreeSystemComponent::CreateDescriptor(),
                             PlayerTankIntentAdapterComponent::CreateDescriptor(),
                             AiTankIntentAdapterComponent::CreateDescriptor(),
                             ScenePolytreeTankSpawnerComponent::CreateDescriptor(),
                             TankNodeBindingComponent::CreateDescriptor(),
                         });
}

AZ::ComponentTypeList ScenePolytreeModule::GetRequiredSystemComponents() const {
    return {azrtti_typeid<ScenePolytreeSystemComponent>()};
}
} // namespace ScenePolytree

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), ScenePolytree::ScenePolytreeModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_ScenePolytree, ScenePolytree::ScenePolytreeModule)
#endif
