#pragma once

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeController.h>

#include <AzCore/Component/EntityId.h>
#include <AzCore/std/containers/vector.h>

namespace ScenePolytree {
class ScenePolytreeControllerLifecycleRequests {
  public:
    AZ_RTTI(ScenePolytreeControllerLifecycleRequests, "{2959CE2D-53DE-42CF-82C4-EAC33C5F61DE}");
    virtual ~ScenePolytreeControllerLifecycleRequests() = default;

    [[nodiscard]] virtual SceneCommandSubmission
    SubmitAttachControllers(InstanceHandle instance,
                            AZStd::vector<ScenePolytreeControllerDeclaration> declarations,
                            AZ::EntityId completionEntity) = 0;
    [[nodiscard]] virtual SceneCommandSubmission
    SubmitDetachControllers(InstanceHandle instance, AZ::EntityId completionEntity) = 0;
    virtual ScenePolytreeControllerResultCode CloseControllerInput(InstanceHandle instance) = 0;
    virtual ScenePolytreeControllerResultCode
    DestroyControllersImmediately(InstanceHandle instance) = 0;
};
} // namespace ScenePolytree
