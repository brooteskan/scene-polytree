#pragma once

#include "ScenePolytreeSystemComponent.h"

#include <ScenePolytree/ScenePolytreeComponent.h>
#include <ScenePolytree/ScenePolytreePrefabNodeComponent.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>
#include <ScenePolytree/Tank/AiTankIntentAdapterComponent.h>
#include <ScenePolytree/Tank/PlayerTankIntentAdapterComponent.h>
#include <ScenePolytree/Tank/ScenePolytreeTankSpawnerComponent.h>
#include <ScenePolytree/Tank/TankArticulationBindingComponent.h>
#include <ScenePolytree/Tank/TankNodeBindingComponent.h>

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
