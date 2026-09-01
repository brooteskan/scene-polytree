#pragma once

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeSpawnerBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>

namespace ScenePolytree {
class ScenePolytreeSpawnerConfig final : public AZ::ComponentConfig {
  public:
    AZ_CLASS_ALLOCATOR(ScenePolytreeSpawnerConfig, AZ::SystemAllocator);
    AZ_RTTI(ScenePolytreeSpawnerConfig, ScenePolytreeSpawnerConfigTypeId, AZ::ComponentConfig);

    static void Reflect(AZ::ReflectContext *context);

    AZ::EntityId m_sceneEntity{};
    AZ::Data::Asset<AzFramework::Spawnable> m_prefab;
    AZ::u32 m_capacity{1};
    DefaultPlacement m_defaultPlacement{DefaultPlacement::SpawnerWorldTransform};
    SpawnTriggerMode m_triggerMode{SpawnTriggerMode::OnReadyAndExternalRequests};
    AZ::u32 m_initialSpawnCount{};
};

class ScenePolytreeSpawnerComponent final
    : public AZ::Component,
      public ScenePolytreeSpawnerRequestBus::Handler,
      public ScenePolytreeRegistrationNotificationBus::Handler,
      public ScenePolytreeCommandNotificationBus::Handler,
      public Internal::ScenePolytreeSpawnerAsyncNotificationBus::Handler {
  public:
    AZ_COMPONENT(ScenePolytreeSpawnerComponent, ScenePolytreeSpawnerComponentTypeId);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);
    static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType &required);

    ScenePolytreeSpawnerComponent();
    explicit ScenePolytreeSpawnerComponent(const ScenePolytreeSpawnerConfig &configuration);
    ~ScenePolytreeSpawnerComponent() override;

    void Activate() override;
    void Deactivate() override;
    bool ReadInConfig(const AZ::ComponentConfig *baseConfig) override;
    bool WriteOutConfig(AZ::ComponentConfig *outBaseConfig) const override;

    SpawnRequestId Spawn(const ScenePolytreeSpawnRequest &request) override;
    DespawnRequestId Despawn(InstanceHandle instance) override;
    bool CancelSpawn(SpawnRequestId request) override;
    ScenePolytreeSpawnerLifecycle GetSpawnerLifecycle() const override;

    void OnScenePolytreeRegistrationReady(RegistrationToken token, SpawnerHandle spawner) override;
    void OnScenePolytreeRegistrationFailed(RegistrationToken token,
                                           const ScenePolytreeFailure &failure) override;
    void OnScenePolytreeCommandCompleted(SceneCommandId command, SceneCommandType type,
                                         ScenePolytreeResultCode result) override;
    void OnScenePolytreeSpawnCompleted(AZ::u32 spawnerGeneration, SpawnRequestId request,
                                       AZ::u32 ticketId, SpawnError error,
                                       AZStd::vector<ScenePolytreeEntityBinding> bindings) override;
    void OnScenePolytreeDespawnCompleted(AZ::u32 spawnerGeneration, SpawnRequestId request,
                                         AZ::u32 ticketId) override;

  private:
    struct RuntimeState;

    [[nodiscard]] SpawnRequestId AllocateSpawnRequest();
    [[nodiscard]] DespawnRequestId AllocateDespawnRequest();
    void EnterFailure(SpawnError error, const ScenePolytreeFailure &registrationFailure = {});
    void BeginInitialSpawns();
    void BeginSpawn(SpawnRequestId requestId, const ScenePolytreeSpawnRequest &request,
                    const AZ::Transform *initialPlacement = nullptr);
    void SubmitReset(SpawnRequestId requestId);
    void SubmitPlace(SpawnRequestId requestId);
    void BeginEntitySpawn(SpawnRequestId requestId);
    void SubmitBind(SpawnRequestId requestId, AZStd::vector<ScenePolytreeEntityBinding> bindings);
    void BeginSpawnFailure(SpawnRequestId requestId, const ScenePolytreeSpawnFailure &failure);
    void SubmitUnbind(SpawnRequestId requestId);
    void BeginEntityDespawn(SpawnRequestId requestId);
    void SubmitCleanupReset(SpawnRequestId requestId);
    void SubmitRelease(SpawnRequestId requestId);
    void FinishRelease(SpawnRequestId requestId, ScenePolytreeResultCode result);
    void QueueSpawnFailure(SpawnRequestId requestId, const ScenePolytreeSpawnFailure &failure,
                           const ScenePolytreeRequestContext &context) const;
    void QueueDespawnFailure(DespawnRequestId requestId, InstanceHandle instance,
                             const ScenePolytreeDespawnFailure &failure) const;

    ScenePolytreeSpawnerConfig m_configuration;
    AZStd::unique_ptr<RuntimeState> m_runtime;
};
} // namespace ScenePolytree
