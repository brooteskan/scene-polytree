#pragma once

#include "ScenePolytreeSystemComponent.h"

#include <ScenePolytree/ScenePolytreeComponent.h>
#include <ScenePolytree/ScenePolytreeSpawnerComponent.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

namespace ScenePolytree {
class ScenePolytreeModule final : public AZ::Module {
  public:
    AZ_RTTI(ScenePolytreeModule, ScenePolytreeModuleTypeId, AZ::Module);
    AZ_CLASS_ALLOCATOR(ScenePolytreeModule, AZ::SystemAllocator);

    ScenePolytreeModule();
    AZ::ComponentTypeList GetRequiredSystemComponents() const override;
};
} // namespace ScenePolytree
