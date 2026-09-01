#pragma once

#include <ScenePolytree/ScenePolytreeControllerTypes.h>

#include <AzCore/RTTI/RTTI.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>

#include <chrono>
#include <span>

namespace ScenePolytree {
namespace Internal {
class ControllerCommandSinkAccess;
}

struct ScenePolytreeControllerFixedStep {
    AZ::u64 m_tick{};
    std::chrono::nanoseconds m_delta{};
};

class ScenePolytreeControllerCommandSink final {
  public:
    [[nodiscard]] ScenePolytreeControllerResultCode
    SetLocalTransform(ScenePolytreeControllerTargetToken target, const AZ::Transform &local);
    [[nodiscard]] ScenePolytreeControllerResultCode
    SetVelocity(ScenePolytreeControllerTargetToken target, const AZ::Vector3 &linear,
                const AZ::Vector3 &angular);
    [[nodiscard]] ScenePolytreeControllerResultCode
    StopMotion(ScenePolytreeControllerTargetToken target);
    [[nodiscard]] ScenePolytreeControllerResultCode
    GetLocalTransform(ScenePolytreeControllerTargetToken target, AZ::Transform &local) const;

  private:
    friend class Internal::ControllerCommandSinkAccess;
    void *m_runtime{};
    AZ::u64 m_epoch{};
};

class ScenePolytreeControllerBatch {
  public:
    AZ_RTTI(ScenePolytreeControllerBatch, "{5207D979-BD31-4F4A-B307-4D7EFFAD8918}");
    virtual ~ScenePolytreeControllerBatch() = default;

    [[nodiscard]] virtual ScenePolytreeControllerCreateResult
    CreateController(const InstanceHandle &instance, const AZ::Name &declarationId,
                     const ScenePolytreeControllerConfiguration &configuration,
                     std::span<const ScenePolytreeResolvedControllerTarget> targets,
                     ScenePolytreeControllerCommandSink &commands) = 0;
    virtual ScenePolytreeControllerResultCode
    CloseInput(ScenePolytreeControllerStateHandle state) = 0;
    virtual ScenePolytreeControllerResultCode
    DestroyController(ScenePolytreeControllerStateHandle state) = 0;
    virtual ScenePolytreeControllerResultCode
    SubmitInput(ScenePolytreeControllerStateHandle state, const ScenePolytreeControllerInput &input,
                ScenePolytreeControllerCommandSink &commands) = 0;
    [[nodiscard]] virtual bool HasRunningControllers() const noexcept = 0;
    virtual void FixedStepBatch(ScenePolytreeControllerFixedStep step,
                                ScenePolytreeControllerCommandSink &commands) noexcept = 0;
};

class ScenePolytreeControllerFactory {
  public:
    AZ_RTTI(ScenePolytreeControllerFactory, "{EBFB4378-85CC-445C-A0FA-033BE5EF6B16}");
    virtual ~ScenePolytreeControllerFactory() = default;

    [[nodiscard]] virtual ScenePolytreeControllerTypeId GetControllerTypeId() const noexcept = 0;
    [[nodiscard]] virtual AZStd::unique_ptr<ScenePolytreeControllerBatch> CreateBatch() const = 0;
};

class ScenePolytreeBehaviorProvider {
  public:
    AZ_RTTI(ScenePolytreeBehaviorProvider, "{B57C02E7-FCAC-468F-A1B0-D2D1CDAA2AC0}");
    virtual ~ScenePolytreeBehaviorProvider() = default;

    [[nodiscard]] virtual ScenePolytreeControllerDeclaration
    CopyScenePolytreeControllerDeclaration() const = 0;
};

class ScenePolytreeControllerRegistry {
  public:
    AZ_RTTI(ScenePolytreeControllerRegistry, "{E422755E-55AD-448C-9E2E-66E76A0BC3F3}");
    virtual ~ScenePolytreeControllerRegistry() = default;

    [[nodiscard]] virtual ScenePolytreeControllerFactoryRegistrationResult
    RegisterControllerFactory(AZStd::shared_ptr<const ScenePolytreeControllerFactory> factory) = 0;
    virtual ScenePolytreeControllerResultCode
    UnregisterControllerFactory(ScenePolytreeControllerFactoryRegistrationToken token) = 0;
};

class ScenePolytreeControllerRequests {
  public:
    AZ_RTTI(ScenePolytreeControllerRequests, "{79AD366F-1100-4AF6-A052-F05B19AD0C0D}");
    virtual ~ScenePolytreeControllerRequests() = default;

    [[nodiscard]] virtual ScenePolytreeControllerLookupResult
    FindController(InstanceHandle instance, const AZ::Name &declarationId) const = 0;
    virtual ScenePolytreeControllerResultCode
    SubmitControllerInput(ScenePolytreeControllerHandle controller,
                          const ScenePolytreeControllerInput &input) = 0;
};
} // namespace ScenePolytree
