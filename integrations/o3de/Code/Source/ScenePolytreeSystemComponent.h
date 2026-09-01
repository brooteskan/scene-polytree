#pragma once

#include "SceneInstance.h"

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeControllerBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/parallel/mutex.h>
#include <AzFramework/Entity/GameEntityContextBus.h>
#include <AzFramework/Spawnable/RootSpawnableInterface.h>

#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ScenePolytree {
class ScenePolytreeSystemComponent final
    : public AZ::Component,
      public AZ::TickBus::Handler,
      public ScenePolytreeRequestBus::Handler,
      public ScenePolytreeRegistrationRequests,
      public ScenePolytreeControllerRegistry,
      public ScenePolytreeControllerRequests,
      public ScenePolytreeControllerLifecycleRequests,
      public AzFramework::GameEntityContextEventBus::Handler,
      public AzFramework::RootSpawnableNotificationBus::Handler {
  public:
    AZ_COMPONENT(ScenePolytreeSystemComponent, ScenePolytreeSystemComponentTypeId);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);

    void Activate() override;
    void Deactivate() override;

    SceneHandle CreateScene(const ScenePolytreeSceneDescriptor &descriptor) override;
    void DestroyScene(SceneHandle scene) override;
    SlotResult ReserveSlot(SpawnerHandle spawner) override;
    ScenePolytreeResultCode PlaceSlot(SlotHandle slot, const AZ::Transform &rootWorld) override;
    ScenePolytreeResultCode
    BindSlot(SlotHandle slot, const AZStd::vector<ScenePolytreeEntityBinding> &bindings) override;
    ScenePolytreeResultCode UnbindSlot(SlotHandle slot) override;
    ScenePolytreeResultCode ResetSlot(SlotHandle slot) override;
    ScenePolytreeResultCode ReleaseSlot(SlotHandle slot) override;
    SceneCommandSubmission SubmitPlaceSlot(SlotHandle slot, const AZ::Transform &rootWorld,
                                           AZ::EntityId completionEntity) override;
    SceneCommandSubmission SubmitBindSlot(SlotHandle slot,
                                          const AZStd::vector<ScenePolytreeEntityBinding> &bindings,
                                          AZ::EntityId completionEntity) override;
    SceneCommandSubmission SubmitUnbindSlot(SlotHandle slot,
                                            AZ::EntityId completionEntity) override;
    SceneCommandSubmission SubmitResetSlot(SlotHandle slot, AZ::EntityId completionEntity) override;
    SceneCommandSubmission SubmitReleaseSlot(SlotHandle slot,
                                             AZ::EntityId completionEntity) override;
    NodeResult ResolveNode(SlotHandle slot, const AZ::Name &bindingId) const override;
    bool SetSceneActive(SceneHandle scene, bool active) override;
    bool RequestCorrection(const SceneCorrection &correction) override;
    SceneStatistics GetSceneStatistics(SceneHandle scene) const override;
    bool IsSceneAlive(SceneHandle scene) const override;
    bool IsSceneReady(SceneHandle scene) const override;

    ScenePolytreeResultCode RegisterSceneEntity(AZ::EntityId sceneEntity, bool isDefault) override;
    void UnregisterSceneEntity(AZ::EntityId sceneEntity) override;
    RegistrationResult
    RegisterPrefab(AZ::EntityId ownerEntity, AZ::EntityId targetScene,
                   const ScenePolytreePrefabRegistrationDescriptor &descriptor) override;
    void UnregisterPrefab(RegistrationToken token) override;

    ScenePolytreeControllerFactoryRegistrationResult RegisterControllerFactory(
        AZStd::shared_ptr<const ScenePolytreeControllerFactory> factory) override;
    ScenePolytreeControllerResultCode
    UnregisterControllerFactory(ScenePolytreeControllerFactoryRegistrationToken token) override;
    ScenePolytreeControllerLookupResult
    FindController(InstanceHandle instance, const AZ::Name &declarationId) const override;
    ScenePolytreeControllerResultCode
    SubmitControllerInput(ScenePolytreeControllerHandle controller,
                          const ScenePolytreeControllerInput &input) override;
    SceneCommandSubmission
    SubmitAttachControllers(InstanceHandle instance,
                            AZStd::vector<ScenePolytreeControllerDeclaration> declarations,
                            AZ::EntityId completionEntity) override;
    SceneCommandSubmission SubmitDetachControllers(InstanceHandle instance,
                                                   AZ::EntityId completionEntity) override;
    ScenePolytreeControllerResultCode CloseControllerInput(InstanceHandle instance) override;
    ScenePolytreeControllerResultCode
    DestroyControllersImmediately(InstanceHandle instance) override;

    void OnGameEntitiesStarted() override;
    void OnGameEntitiesReset() override;
    void OnRootSpawnableReady(AZ::Data::Asset<AzFramework::Spawnable> rootSpawnable,
                              AZ::u32 generation) override;

    void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
    int GetTickOrder() override;

  private:
    struct CreateCommand {
        SceneHandle m_scene;
        ScenePolytreeSceneDescriptor m_descriptor;
    };
    struct DestroyCommand {
        SceneHandle m_scene;
    };
    struct PlaceSlotCommand {
        SlotHandle m_slot;
        AZ::Transform m_rootWorld{AZ::Transform::CreateIdentity()};
        SceneCommandId m_command;
        AZ::EntityId m_completionEntity;
    };
    struct BindSlotCommand {
        SlotHandle m_slot;
        AZStd::vector<ScenePolytreeEntityBinding> m_bindings;
        SceneCommandId m_command;
        AZ::EntityId m_completionEntity;
    };
    struct UnbindSlotCommand {
        SlotHandle m_slot;
        SceneCommandId m_command;
        AZ::EntityId m_completionEntity;
    };
    struct ResetSlotCommand {
        SlotHandle m_slot;
        SceneCommandId m_command;
        AZ::EntityId m_completionEntity;
    };
    struct ReleaseSlotCommand {
        SlotHandle m_slot;
        SceneCommandId m_command;
        AZ::EntityId m_completionEntity;
    };
    struct AttachControllersCommand {
        InstanceHandle m_instance;
        AZStd::vector<ScenePolytreeControllerDeclaration> m_declarations;
        SceneCommandId m_command;
        AZ::EntityId m_completionEntity;
    };
    struct DetachControllersCommand {
        InstanceHandle m_instance;
        SceneCommandId m_command;
        AZ::EntityId m_completionEntity;
    };
    struct ActiveCommand {
        SceneHandle m_scene;
        bool m_active{};
    };
    struct CorrectionCommand {
        SceneCorrection m_correction;
    };
    using Command = std::variant<CreateCommand, DestroyCommand, PlaceSlotCommand, BindSlotCommand,
                                 UnbindSlotCommand, ResetSlotCommand, ReleaseSlotCommand,
                                 AttachControllersCommand, DetachControllersCommand, ActiveCommand,
                                 CorrectionCommand>;

    enum class SceneLife : AZ::u8 { Pending, Alive, Destroying };
    struct SceneEntry {
        std::unique_ptr<Internal::SceneInstance> m_instance;
        SceneLife m_life{SceneLife::Pending};
    };

    struct RegisteredSceneEntity {
        AZ::EntityId m_entity;
        bool m_default{};
    };

    struct PendingPrefabRegistration {
        RegistrationToken m_token;
        AZ::EntityId m_ownerEntity;
        AZ::EntityId m_targetScene;
        ScenePolytreePrefabRegistrationDescriptor m_descriptor;
    };

    struct RegisteredControllerFactory {
        ScenePolytreeControllerFactoryRegistrationToken m_token;
        AZStd::shared_ptr<const ScenePolytreeControllerFactory> m_factory;
    };

    [[nodiscard]] Internal::SceneInstance *FindScene(SceneHandle scene);
    [[nodiscard]] const Internal::SceneInstance *FindScene(SceneHandle scene) const;
    [[nodiscard]] bool AcceptsCommands(SceneHandle scene) const;
    void Enqueue(Command command);
    void DrainCommands();
    void Process(const CreateCommand &command);
    void Process(const DestroyCommand &command);
    void Process(const PlaceSlotCommand &command);
    void Process(const BindSlotCommand &command);
    void Process(const UnbindSlotCommand &command);
    void Process(const ResetSlotCommand &command);
    void Process(const ReleaseSlotCommand &command);
    void Process(AttachControllersCommand &command);
    void Process(const DetachControllersCommand &command);
    void Process(const ActiveCommand &command);
    void Process(const CorrectionCommand &command);
    void Complete(SceneCommandId command, AZ::EntityId completionEntity, SceneCommandType type,
                  ScenePolytreeResultCode result);
    void RefreshTickConnection();

    mutable AZStd::recursive_mutex m_mutex;
    std::unordered_map<AZ::u64, SceneEntry> m_scenes;
    std::vector<Command> m_commands;
    AZStd::vector<RegisteredSceneEntity> m_registeredSceneEntities;
    AZStd::vector<PendingPrefabRegistration> m_pendingPrefabRegistrations;
    AZStd::vector<RegisteredControllerFactory> m_controllerFactories;
    AZ::u64 m_nextSceneId{1};
    AZ::u64 m_nextRegistrationId{1};
    AZ::u64 m_nextCommandId{1};
    AZ::u32 m_nextControllerFactoryGeneration{1};
    bool m_collectionClosed{};
};
} // namespace ScenePolytree
