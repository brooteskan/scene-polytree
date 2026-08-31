#pragma once

#include "SceneInstance.h"

#include <ScenePolytree/ScenePolytreeBus.h>
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
    };
    struct BindSlotCommand {
        SlotHandle m_slot;
        AZStd::vector<ScenePolytreeEntityBinding> m_bindings;
    };
    struct UnbindSlotCommand {
        SlotHandle m_slot;
    };
    struct ResetSlotCommand {
        SlotHandle m_slot;
    };
    struct ReleaseSlotCommand {
        SlotHandle m_slot;
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
                                 ActiveCommand, CorrectionCommand>;

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
    void Process(const ActiveCommand &command);
    void Process(const CorrectionCommand &command);
    void RefreshTickConnection();

    mutable AZStd::recursive_mutex m_mutex;
    std::unordered_map<AZ::u64, SceneEntry> m_scenes;
    std::vector<Command> m_commands;
    AZStd::vector<RegisteredSceneEntity> m_registeredSceneEntities;
    AZStd::vector<PendingPrefabRegistration> m_pendingPrefabRegistrations;
    AZ::u64 m_nextSceneId{1};
    AZ::u64 m_nextRegistrationId{1};
    bool m_collectionClosed{};
};
} // namespace ScenePolytree
