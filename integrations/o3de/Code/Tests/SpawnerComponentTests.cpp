#include <ScenePolytree/ScenePolytreeSpawnerComponent.h>

#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>
#include <AzTest/AzTest.h>
#include <Mocks/MockSpawnableEntitiesInterface.h>

#include <algorithm>
#include <ranges>

namespace ScenePolytree::Tests {
namespace {
using ::testing::_;
using ::testing::Return;

[[nodiscard]] AZ::Data::Asset<AzFramework::Spawnable> MakeSpawnerPrefab(AZ::u32 subId = 1) {
    const AZ::Data::AssetId assetId(AZ::Uuid("{45AF110A-BE8F-47F9-9A1C-4BC981920F75}"), subId);
    auto *spawnable =
        aznew AzFramework::Spawnable(assetId, AZ::Data::AssetData::AssetStatus::Ready);

    auto root = AZStd::make_unique<AZ::Entity>(AZ::EntityId(10001), "Root");
    auto *rootTransform = root->CreateComponent<AzFramework::TransformComponent>();
    rootTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 0.0f, 0.0f)));
    spawnable->GetEntities().push_back(AZStd::move(root));

    auto child = AZStd::make_unique<AZ::Entity>(AZ::EntityId(10002), "Child");
    auto *childTransform = child->CreateComponent<AzFramework::TransformComponent>();
    childTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 0.0f, 2.0f)));
    childTransform->SetParent(AZ::EntityId(10001));
    spawnable->GetEntities().push_back(AZStd::move(child));

    return {spawnable, AZ::Data::AssetLoadBehavior::Default};
}

class FakeRegistrationRequests final : public ScenePolytreeRegistrationRequests {
  public:
    struct Registration {
        RegistrationToken m_token;
        AZ::EntityId m_owner;
        AZ::EntityId m_scene;
        ScenePolytreePrefabRegistrationDescriptor m_descriptor;
    };

    FakeRegistrationRequests() { AZ::Interface<ScenePolytreeRegistrationRequests>::Register(this); }
    ~FakeRegistrationRequests() override {
        AZ::Interface<ScenePolytreeRegistrationRequests>::Unregister(this);
    }

    ScenePolytreeResultCode RegisterSceneEntity(AZ::EntityId, bool) override {
        return ScenePolytreeResultCode::Success;
    }
    void UnregisterSceneEntity(AZ::EntityId) override {}

    RegistrationResult
    RegisterPrefab(AZ::EntityId ownerEntity, AZ::EntityId targetScene,
                   const ScenePolytreePrefabRegistrationDescriptor &descriptor) override {
        const RegistrationToken token{m_nextToken++};
        m_registrations.push_back({token, ownerEntity, targetScene, descriptor});
        return {ScenePolytreeResultCode::Success, token};
    }

    void UnregisterPrefab(RegistrationToken token) override { m_unregistered.push_back(token); }

    [[nodiscard]] RegistrationToken TokenFor(AZ::EntityId owner) const {
        const auto found = std::ranges::find(m_registrations, owner, &Registration::m_owner);
        return found != m_registrations.end() ? found->m_token : RegistrationToken{};
    }

    AZStd::vector<Registration> m_registrations;
    AZStd::vector<RegistrationToken> m_unregistered;
    AZ::u64 m_nextToken{1};
};

class FakeSceneRequests final : public ScenePolytreeRequests {
  public:
    struct SlotState {
        AZ::u64 m_partition{};
        AZ::u32 m_slot{};
        AZ::u32 m_generation{1};
        bool m_reserved{};
    };

    struct PendingCommand {
        SceneCommandId m_id;
        SceneCommandType m_type;
        SlotHandle m_slot;
        AZ::Transform m_placement{AZ::Transform::CreateIdentity()};
        AZStd::vector<ScenePolytreeEntityBinding> m_bindings;
    };

    FakeSceneRequests() { AZ::Interface<ScenePolytreeRequests>::Register(this); }
    ~FakeSceneRequests() override { AZ::Interface<ScenePolytreeRequests>::Unregister(this); }

    SceneHandle CreateScene(const ScenePolytreeSceneDescriptor &) override { return {1}; }
    void DestroyScene(SceneHandle) override {}

    SlotResult ReserveSlot(SpawnerHandle spawner) override {
        auto available = std::ranges::find_if(m_slots, [&](const SlotState &slot) {
            return slot.m_partition == spawner.m_partition && !slot.m_reserved;
        });
        if (available == m_slots.end()) {
            const auto partitionSize =
                std::ranges::count(m_slots, spawner.m_partition, &SlotState::m_partition);
            if (static_cast<AZ::u32>(partitionSize) >= m_capacity) {
                return {ScenePolytreeResultCode::SlotUnavailable, {}};
            }
            const AZ::u32 slotIndex = static_cast<AZ::u32>(partitionSize);
            m_slots.push_back({spawner.m_partition, slotIndex, 1, false});
            available = m_slots.end() - 1;
        }
        available->m_reserved = true;
        return {ScenePolytreeResultCode::Success,
                {spawner, available->m_slot, available->m_generation}};
    }

    ScenePolytreeResultCode PlaceSlot(SlotHandle, const AZ::Transform &) override {
        ++m_immediatePlaceCount;
        return ScenePolytreeResultCode::Success;
    }
    ScenePolytreeResultCode BindSlot(SlotHandle,
                                     const AZStd::vector<ScenePolytreeEntityBinding> &) override {
        ++m_immediateBindCount;
        return ScenePolytreeResultCode::Success;
    }
    ScenePolytreeResultCode UnbindSlot(SlotHandle) override {
        ++m_immediateUnbindCount;
        return ScenePolytreeResultCode::Success;
    }
    ScenePolytreeResultCode ResetSlot(SlotHandle) override {
        ++m_immediateResetCount;
        return ScenePolytreeResultCode::Success;
    }
    ScenePolytreeResultCode ReleaseSlot(SlotHandle slot) override {
        ++m_immediateReleaseCount;
        Release(slot);
        return ScenePolytreeResultCode::Success;
    }

    SceneCommandSubmission SubmitPlaceSlot(SlotHandle slot, const AZ::Transform &placement,
                                           AZ::EntityId) override {
        return Queue(SceneCommandType::PlaceSlot, slot, placement, {});
    }
    SceneCommandSubmission SubmitBindSlot(SlotHandle slot,
                                          const AZStd::vector<ScenePolytreeEntityBinding> &bindings,
                                          AZ::EntityId) override {
        return Queue(SceneCommandType::BindSlot, slot, AZ::Transform::CreateIdentity(), bindings);
    }
    SceneCommandSubmission SubmitUnbindSlot(SlotHandle slot, AZ::EntityId) override {
        return Queue(SceneCommandType::UnbindSlot, slot, AZ::Transform::CreateIdentity(), {});
    }
    SceneCommandSubmission SubmitResetSlot(SlotHandle slot, AZ::EntityId) override {
        return Queue(SceneCommandType::ResetSlot, slot, AZ::Transform::CreateIdentity(), {});
    }
    SceneCommandSubmission SubmitReleaseSlot(SlotHandle slot, AZ::EntityId) override {
        return Queue(SceneCommandType::ReleaseSlot, slot, AZ::Transform::CreateIdentity(), {});
    }

    NodeResult ResolveNode(SlotHandle, const AZ::Name &) const override { return {}; }
    bool SetSceneActive(SceneHandle, bool) override { return true; }
    bool RequestCorrection(const SceneCorrection &) override { return true; }
    SceneStatistics GetSceneStatistics(SceneHandle) const override { return {}; }
    bool IsSceneAlive(SceneHandle) const override { return true; }
    bool IsSceneReady(SceneHandle) const override { return true; }

    [[nodiscard]] PendingCommand Take(SceneCommandType expected) {
        EXPECT_FALSE(m_pending.empty());
        if (m_pending.empty()) {
            return {};
        }
        PendingCommand command = AZStd::move(m_pending.front());
        m_pending.erase(m_pending.begin());
        EXPECT_EQ(command.m_type, expected);
        return command;
    }

    void Complete(ScenePolytreeSpawnerComponent &component, SceneCommandType expected,
                  ScenePolytreeResultCode result = ScenePolytreeResultCode::Success) {
        const PendingCommand command = Take(expected);
        if (result == ScenePolytreeResultCode::Success &&
            command.m_type == SceneCommandType::ReleaseSlot) {
            Release(command.m_slot);
        }
        component.OnScenePolytreeCommandCompleted(command.m_id, command.m_type, result);
    }

    [[nodiscard]] AZ::u32 ReservedCount() const {
        return static_cast<AZ::u32>(std::ranges::count(m_slots, true, &SlotState::m_reserved));
    }

    SceneCommandSubmission Queue(SceneCommandType type, SlotHandle slot,
                                 const AZ::Transform &placement,
                                 AZStd::vector<ScenePolytreeEntityBinding> bindings) {
        const SceneCommandId command{m_nextCommand++};
        m_pending.push_back({command, type, slot, placement, AZStd::move(bindings)});
        return {ScenePolytreeResultCode::Success, command};
    }

    void Release(SlotHandle slot) {
        const auto found = std::ranges::find_if(m_slots, [&](const SlotState &state) {
            return state.m_partition == slot.m_spawner.m_partition && state.m_slot == slot.m_slot &&
                   state.m_generation == slot.m_generation;
        });
        if (found != m_slots.end()) {
            found->m_reserved = false;
            if (++found->m_generation == 0) {
                ++found->m_generation;
            }
        }
    }

    AZ::u32 m_capacity{1};
    AZ::u64 m_nextCommand{1};
    AZStd::vector<SlotState> m_slots;
    AZStd::vector<PendingCommand> m_pending;
    AZ::u32 m_immediatePlaceCount{};
    AZ::u32 m_immediateBindCount{};
    AZ::u32 m_immediateUnbindCount{};
    AZ::u32 m_immediateResetCount{};
    AZ::u32 m_immediateReleaseCount{};
};

struct SpawnSuccess {
    SpawnRequestId m_request;
    InstanceHandle m_instance;
    ScenePolytreeRequestContext m_context;
};

struct SpawnFailureNotification {
    SpawnRequestId m_request;
    ScenePolytreeSpawnFailure m_failure;
    ScenePolytreeRequestContext m_context;
};

struct DespawnSuccess {
    DespawnRequestId m_request;
    InstanceHandle m_instance;
};

class CapturingSpawnerNotifications final : public ScenePolytreeSpawnerNotificationBus::Handler {
  public:
    explicit CapturingSpawnerNotifications(AZ::EntityId entity) { BusConnect(entity); }
    ~CapturingSpawnerNotifications() override { BusDisconnect(); }

    void OnScenePolytreeSpawnerReady(SpawnerHandle spawner) override { m_ready.push_back(spawner); }
    void OnScenePolytreeSpawnerFailed(const ScenePolytreeSpawnerFailure &failure) override {
        m_spawnerFailures.push_back(failure);
    }
    void OnSpawnSucceeded(SpawnRequestId request, InstanceHandle instance,
                          const ScenePolytreeRequestContext &context) override {
        m_spawnSuccesses.push_back({request, instance, context});
    }
    void OnSpawnFailed(SpawnRequestId request, const ScenePolytreeSpawnFailure &failure,
                       const ScenePolytreeRequestContext &context) override {
        m_spawnFailures.push_back({request, failure, context});
    }
    void OnDespawnSucceeded(DespawnRequestId request, InstanceHandle instance) override {
        m_despawnSuccesses.push_back({request, instance});
    }
    void OnDespawnFailed(DespawnRequestId request, InstanceHandle instance,
                         const ScenePolytreeDespawnFailure &failure) override {
        m_despawnFailures.push_back({request, instance, failure});
    }

    struct DespawnFailureNotification {
        DespawnRequestId m_request;
        InstanceHandle m_instance;
        ScenePolytreeDespawnFailure m_failure;
    };

    AZStd::vector<SpawnerHandle> m_ready;
    AZStd::vector<ScenePolytreeSpawnerFailure> m_spawnerFailures;
    AZStd::vector<SpawnSuccess> m_spawnSuccesses;
    AZStd::vector<SpawnFailureNotification> m_spawnFailures;
    AZStd::vector<DespawnSuccess> m_despawnSuccesses;
    AZStd::vector<DespawnFailureNotification> m_despawnFailures;
};

class HarnessEntity final : public AZ::Entity {
  public:
    using AZ::Entity::Entity;
    void ActivateForTest(AZ::Component &component) { ActivateComponent(component); }
    void DeactivateForTest(AZ::Component &component) { DeactivateComponent(component); }
};

class SpawnerHarness final {
  public:
    SpawnerHarness(AZ::u64 entityId, const ScenePolytreeSpawnerConfig &configuration,
                   const AZ::Transform &world = AZ::Transform::CreateIdentity())
        : m_entity(AZ::EntityId(entityId), "SpawnerHarness"), m_world(world) {
        m_transform = m_entity.CreateComponent<AzFramework::TransformComponent>();
        m_component = m_entity.CreateComponent<ScenePolytreeSpawnerComponent>(configuration);
    }

    ~SpawnerHarness() {
        if (m_active) {
            m_component->Deactivate();
        }
        if (m_transformActive) {
            m_entity.DeactivateForTest(*m_transform);
        }
    }

    void Activate() {
        m_entity.Init();
        m_entity.ActivateForTest(*m_transform);
        m_transformActive = true;
        m_transform->SetWorldTM(m_world);
        m_component->Activate();
        m_active = true;
    }

    void Deactivate() {
        if (m_active) {
            m_component->Deactivate();
            m_active = false;
        }
    }

    [[nodiscard]] AZ::EntityId Id() const { return m_entity.GetId(); }

    HarnessEntity m_entity;
    AzFramework::TransformComponent *m_transform{};
    ScenePolytreeSpawnerComponent *m_component{};
    AZ::Transform m_world{AZ::Transform::CreateIdentity()};
    bool m_transformActive{};
    bool m_active{};
};

class RuntimeEntity final : public AZ::Entity {
  public:
    using AZ::Entity::Entity;
    void MarkActive() { SetState(State::Active); }
    void MarkInactive() { SetState(State::Init); }
};

class RuntimePrefabInstance final {
  public:
    RuntimePrefabInstance(bool includeChild = true, bool attachTransformParent = true) {
        auto root = AZStd::make_unique<RuntimeEntity>(AZ::EntityId(11001), "Root");
        auto *rootTransform = root->CreateComponent<AzFramework::TransformComponent>();
        rootTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 0.0f, 0.0f)));
        m_entities.push_back(AZStd::move(root));

        if (includeChild) {
            auto child = AZStd::make_unique<RuntimeEntity>(AZ::EntityId(11002), "Child");
            auto *childTransform = child->CreateComponent<AzFramework::TransformComponent>();
            childTransform->SetLocalTM(
                AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 0.0f, 2.0f)));
            if (attachTransformParent) {
                childTransform->SetParent(AZ::EntityId(11001));
            }
            m_entities.push_back(AZStd::move(child));
        }

        std::ranges::transform(m_entities, std::back_inserter(m_view),
                               [](const auto &entity) { return entity.get(); });
    }

    ~RuntimePrefabInstance() {
        std::ranges::for_each(m_entities, [](const auto &entity) {
            if (entity->GetState() == AZ::Entity::State::Active) {
                entity->MarkInactive();
            }
        });
    }

    void PreInsert(AzFramework::EntitySpawnTicket::Id ticket,
                   AzFramework::SpawnAllEntitiesOptionalArgs &arguments) {
        ASSERT_TRUE(static_cast<bool>(arguments.m_preInsertionCallback));
        arguments.m_preInsertionCallback(
            ticket, AzFramework::SpawnableEntityContainerView(m_view.data(), m_view.size()));
        std::ranges::for_each(m_entities, [](const auto &entity) { entity->MarkActive(); });
    }

    void Complete(AzFramework::EntitySpawnTicket::Id ticket,
                  AzFramework::SpawnAllEntitiesOptionalArgs &arguments) {
        ASSERT_TRUE(static_cast<bool>(arguments.m_completionCallback));
        arguments.m_completionCallback(
            ticket, AzFramework::SpawnableConstEntityContainerView(m_view.data(), m_view.size()));
    }

    [[nodiscard]] AZ::Transform World(const AZ::Name &bindingId) const {
        const auto found =
            std::ranges::find_if(m_entities, [&](const AZStd::unique_ptr<RuntimeEntity> &entity) {
                return AZ::Name(entity->GetName()) == bindingId;
            });
        const auto *transform = (*found)->FindComponent<AzFramework::TransformComponent>();
        return const_cast<AzFramework::TransformComponent *>(transform)->GetWorldTM();
    }

    [[nodiscard]] AZ::EntityId Parent(const AZ::Name &bindingId) const {
        const auto found =
            std::ranges::find_if(m_entities, [&](const AZStd::unique_ptr<RuntimeEntity> &entity) {
                return AZ::Name(entity->GetName()) == bindingId;
            });
        auto *transform = (*found)->FindComponent<AzFramework::TransformComponent>();
        return transform->GetParentId();
    }

    AZStd::vector<AZStd::unique_ptr<RuntimeEntity>> m_entities;
    AZStd::vector<AZ::Entity *> m_view;
};

class SpawnerServices final {
  public:
    SpawnerServices() {
        AzFramework::MockSpawnableEntitiesInterface::InstallDefaultReturns(m_spawnables);
        ON_CALL(m_spawnables, GetTicketId(_)).WillByDefault(Return(701));
    }

    FakeRegistrationRequests m_registry;
    FakeSceneRequests m_scene;
    AzFramework::NiceSpawnableEntitiesInterfaceMock m_spawnables;
};

[[nodiscard]] ScenePolytreeSpawnerConfig
MakeConfig(AZ::u32 capacity = 1,
           SpawnTriggerMode trigger = SpawnTriggerMode::OnReadyAndExternalRequests,
           AZ::u32 initialCount = 0) {
    ScenePolytreeSpawnerConfig configuration;
    configuration.m_prefab = MakeSpawnerPrefab();
    configuration.m_capacity = capacity;
    configuration.m_triggerMode = trigger;
    configuration.m_initialSpawnCount = initialCount;
    return configuration;
}

[[nodiscard]] ScenePolytreeSpawnRequest DefaultSpawnRequest() {
    ScenePolytreeSpawnRequest request;
    return request;
}

void MakeReady(SpawnerHarness &spawner, FakeRegistrationRequests &registry, AZ::u64 partition = 1) {
    spawner.m_component->OnScenePolytreeRegistrationReady(registry.TokenFor(spawner.Id()),
                                                          {SceneHandle{9}, partition, 1});
}

void FinishCancelledSpawn(SpawnerHarness &spawner, FakeSceneRequests &scene) {
    scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    scene.Complete(*spawner.m_component, SceneCommandType::UnbindSlot);
    scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    scene.Complete(*spawner.m_component, SceneCommandType::ReleaseSlot);
    AZ::TickBus::ExecuteQueuedEvents();
}
} // namespace

TEST(ScenePolytreeSpawnerComponentTests,
     InvalidPrefabAndPreReadyRequestsProduceTypedObservableFailures) {
    SpawnerServices services;
    ScenePolytreeSpawnerConfig invalidConfiguration;
    SpawnerHarness invalidSpawner(12001, invalidConfiguration);
    CapturingSpawnerNotifications invalidNotifications(invalidSpawner.Id());
    invalidSpawner.Activate();
    EXPECT_EQ(invalidSpawner.m_component->GetSpawnerLifecycle(),
              ScenePolytreeSpawnerLifecycle::Failed);
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(invalidNotifications.m_spawnerFailures.size(), 1);
    EXPECT_EQ(invalidNotifications.m_spawnerFailures.front().m_error, SpawnError::InvalidPrefab);

    SpawnerHarness spawner(12002, MakeConfig());
    CapturingSpawnerNotifications notifications(spawner.Id());
    spawner.Activate();
    EXPECT_EQ(spawner.m_component->GetSpawnerLifecycle(),
              ScenePolytreeSpawnerLifecycle::Registering);
    const ScenePolytreeSpawnRequest request{
        {AZ::EntityId(12003), 77}, false, AZ::Transform::CreateIdentity()};
    const SpawnRequestId requestId = spawner.m_component->Spawn(request);
    EXPECT_TRUE(requestId.IsValid());
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(notifications.m_spawnFailures.size(), 1);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_request, requestId);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_failure.m_error, SpawnError::NotReady);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_context.m_correlation, 77);
    EXPECT_TRUE(services.m_scene.m_pending.empty());
}

TEST(ScenePolytreeSpawnerComponentTests,
     ReadyIsRegistrationDrivenAndOnReadyRejectsExternalRequests) {
    SpawnerServices services;
    const ScenePolytreeSpawnerConfig configuration = MakeConfig(1, SpawnTriggerMode::OnReady, 0);
    SpawnerHarness spawner(12101, configuration);
    CapturingSpawnerNotifications notifications(spawner.Id());
    spawner.Activate();

    ASSERT_EQ(services.m_registry.m_registrations.size(), 1);
    const auto &registered = services.m_registry.m_registrations.front().m_descriptor;
    EXPECT_EQ(registered.m_prefab.GetId(), configuration.m_prefab.GetId());
    EXPECT_EQ(registered.m_capacity, 1);
    EXPECT_TRUE(notifications.m_ready.empty());

    MakeReady(spawner, services.m_registry);
    EXPECT_EQ(spawner.m_component->GetSpawnerLifecycle(), ScenePolytreeSpawnerLifecycle::Ready);
    ASSERT_EQ(notifications.m_ready.size(), 1);
    EXPECT_TRUE(services.m_scene.m_pending.empty());

    const SpawnRequestId request = spawner.m_component->Spawn(DefaultSpawnRequest());
    EXPECT_TRUE(request.IsValid());
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(notifications.m_spawnFailures.size(), 1);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_failure.m_error,
              SpawnError::ExternalRequestsDisabled);
}

TEST(ScenePolytreeSpawnerComponentTests,
     InitialInstancesShareOneSpawnerWorldPlacementAndExternalOnlySkipsThem) {
    SpawnerServices services;
    services.m_scene.m_capacity = 2;
    const AZ::Transform spawnerWorld =
        AZ::Transform::CreateTranslation(AZ::Vector3(4.0f, 5.0f, 6.0f));
    SpawnerHarness spawner(12201, MakeConfig(2, SpawnTriggerMode::OnReadyAndExternalRequests, 2),
                           spawnerWorld);
    spawner.Activate();
    EXPECT_CALL(services.m_spawnables, SpawnAllEntities(_, _)).Times(0);
    MakeReady(spawner, services.m_registry);

    EXPECT_EQ(services.m_scene.ReservedCount(), 2);
    ASSERT_EQ(services.m_scene.m_pending.size(), 2);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    const auto firstPlacement = services.m_scene.Take(SceneCommandType::PlaceSlot);
    const auto secondPlacement = services.m_scene.Take(SceneCommandType::PlaceSlot);
    EXPECT_TRUE(firstPlacement.m_placement.IsClose(spawnerWorld));
    EXPECT_TRUE(secondPlacement.m_placement.IsClose(spawnerWorld));

    SpawnerHarness externalOnly(12202, MakeConfig(2, SpawnTriggerMode::ExternalRequestsOnly, 2),
                                spawnerWorld);
    externalOnly.Activate();
    MakeReady(externalOnly, services.m_registry, 2);
    EXPECT_EQ(externalOnly.m_component->GetSpawnerLifecycle(),
              ScenePolytreeSpawnerLifecycle::Ready);
    EXPECT_EQ(services.m_scene.ReservedCount(), 2);
}

TEST(ScenePolytreeSpawnerComponentTests,
     CancellationBeforeInsertionCleansAndReleasesTheReservedSlot) {
    SpawnerServices services;
    SpawnerHarness spawner(12301, MakeConfig());
    CapturingSpawnerNotifications notifications(spawner.Id());
    spawner.Activate();
    MakeReady(spawner, services.m_registry);

    const SpawnRequestId request = spawner.m_component->Spawn(DefaultSpawnRequest());
    ASSERT_TRUE(request.IsValid());
    EXPECT_EQ(services.m_scene.ReservedCount(), 1);
    EXPECT_TRUE(spawner.m_component->CancelSpawn(request));
    EXPECT_CALL(services.m_spawnables, SpawnAllEntities(_, _)).Times(0);
    FinishCancelledSpawn(spawner, services.m_scene);

    ASSERT_EQ(notifications.m_spawnFailures.size(), 1);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_request, request);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_failure.m_error, SpawnError::Cancelled);
    EXPECT_EQ(services.m_scene.ReservedCount(), 0);
    EXPECT_FALSE(spawner.m_component->CancelSpawn(request));
}

TEST(ScenePolytreeSpawnerComponentTests,
     SpawnDespawnCapacityReuseAndExplicitPlacementFollowCompletedTransitions) {
    SpawnerServices services;
    SpawnerHarness spawner(12401, MakeConfig());
    CapturingSpawnerNotifications notifications(spawner.Id());
    spawner.Activate();
    MakeReady(spawner, services.m_registry);

    AZStd::unique_ptr<AzFramework::SpawnAllEntitiesOptionalArgs> spawnArguments;
    EXPECT_CALL(services.m_spawnables, SpawnAllEntities(_, _))
        .WillOnce([&](AzFramework::EntitySpawnTicket &,
                      AzFramework::SpawnAllEntitiesOptionalArgs arguments) {
            spawnArguments = AZStd::make_unique<AzFramework::SpawnAllEntitiesOptionalArgs>(
                AZStd::move(arguments));
        });

    ScenePolytreeSpawnRequest request;
    request.m_context = {AZ::EntityId(12402), 901};
    request.m_hasExplicitWorldTransform = true;
    request.m_worldTransform = AZ::Transform::CreateTranslation(AZ::Vector3(10.0f, 20.0f, 30.0f));
    const SpawnRequestId spawnRequest = spawner.m_component->Spawn(request);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::PlaceSlot);
    ASSERT_NE(spawnArguments, nullptr);

    RuntimePrefabInstance runtime;
    runtime.PreInsert(701, *spawnArguments);
    EXPECT_FALSE(spawner.m_component->CancelSpawn(spawnRequest));
    EXPECT_TRUE(
        runtime.World(AZ::Name("Root")).GetTranslation().IsClose(AZ::Vector3(11.0f, 20.0f, 30.0f)));
    EXPECT_TRUE(runtime.World(AZ::Name("Child"))
                    .GetTranslation()
                    .IsClose(AZ::Vector3(11.0f, 20.0f, 32.0f)));
    EXPECT_FALSE(runtime.Parent(AZ::Name("Child")).IsValid());
    runtime.Complete(701, *spawnArguments);
    AZ::TickBus::ExecuteQueuedEvents();

    const auto bind = services.m_scene.Take(SceneCommandType::BindSlot);
    ASSERT_EQ(bind.m_bindings.size(), 2);
    EXPECT_EQ(bind.m_bindings[0].m_bindingId, AZ::Name("Root"));
    EXPECT_EQ(bind.m_bindings[1].m_bindingId, AZ::Name("Root/Child"));
    EXPECT_TRUE(std::ranges::all_of(bind.m_bindings, [](const auto &binding) {
        return binding.m_nodeToEntity.IsClose(AZ::Transform::CreateIdentity());
    }));
    spawner.m_component->OnScenePolytreeCommandCompleted(bind.m_id, bind.m_type,
                                                         ScenePolytreeResultCode::Success);
    ASSERT_EQ(notifications.m_spawnSuccesses.size(), 1);
    const InstanceHandle instance = notifications.m_spawnSuccesses.front().m_instance;
    EXPECT_EQ(notifications.m_spawnSuccesses.front().m_context.m_correlation, 901);

    const SpawnRequestId exhausted = spawner.m_component->Spawn(DefaultSpawnRequest());
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(notifications.m_spawnFailures.size(), 1);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_request, exhausted);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_failure.m_error, SpawnError::NoCapacity);

    EXPECT_CALL(services.m_spawnables, DespawnAllEntities(_, _))
        .WillOnce([](AzFramework::EntitySpawnTicket &ticket,
                     AzFramework::DespawnAllEntitiesOptionalArgs arguments) {
            arguments.m_completionCallback(ticket.GetId());
        });
    const DespawnRequestId despawn = spawner.m_component->Despawn(instance);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::UnbindSlot);
    AZ::TickBus::ExecuteQueuedEvents();
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ReleaseSlot);
    ASSERT_EQ(notifications.m_despawnSuccesses.size(), 1);
    EXPECT_EQ(notifications.m_despawnSuccesses.front().m_request, despawn);
    EXPECT_EQ(services.m_scene.ReservedCount(), 0);

    const DespawnRequestId stale = spawner.m_component->Despawn(instance);
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(notifications.m_despawnFailures.size(), 1);
    EXPECT_EQ(notifications.m_despawnFailures.front().m_request, stale);
    EXPECT_EQ(notifications.m_despawnFailures.front().m_failure.m_error,
              DespawnError::StaleInstance);

    const SpawnRequestId reused = spawner.m_component->Spawn(request);
    ASSERT_TRUE(reused.IsValid());
    ASSERT_FALSE(services.m_scene.m_pending.empty());
    const SlotHandle reusedSlot = services.m_scene.m_pending.front().m_slot;
    EXPECT_EQ(reusedSlot.m_slot, instance.m_slot.m_slot);
    EXPECT_NE(reusedSlot.m_generation, instance.m_slot.m_generation);
}

TEST(ScenePolytreeSpawnerComponentTests, DeclaresSpawnerAndTransformServices) {
    AZ::ComponentDescriptor::DependencyArrayType provided;
    AZ::ComponentDescriptor::DependencyArrayType required;
    ScenePolytreeSpawnerComponent::GetProvidedServices(provided);
    ScenePolytreeSpawnerComponent::GetRequiredServices(required);
    EXPECT_NE(std::ranges::find(provided, AZ_CRC_CE("ScenePolytreeSpawnerService")),
              provided.end());
    EXPECT_NE(std::ranges::find(required, AZ_CRC_CE("TransformService")), required.end());
}

TEST(ScenePolytreeSpawnerComponentTests,
     HierarchyMismatchAndSceneCommandFailuresPerformCompleteTypedCleanup) {
    SpawnerServices services;
    SpawnerHarness spawner(12501, MakeConfig());
    CapturingSpawnerNotifications notifications(spawner.Id());
    spawner.Activate();
    MakeReady(spawner, services.m_registry);

    const SpawnRequestId commandFailure = spawner.m_component->Spawn(DefaultSpawnRequest());
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot,
                              ScenePolytreeResultCode::StaleHandle);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::UnbindSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ReleaseSlot);
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(notifications.m_spawnFailures.size(), 1);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_request, commandFailure);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_failure.m_error,
              SpawnError::SceneCommandFailed);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_failure.m_sceneResult,
              ScenePolytreeResultCode::StaleHandle);
    EXPECT_EQ(services.m_scene.ReservedCount(), 0);

    AZStd::unique_ptr<AzFramework::SpawnAllEntitiesOptionalArgs> spawnArguments;
    EXPECT_CALL(services.m_spawnables, SpawnAllEntities(_, _))
        .WillOnce([&](AzFramework::EntitySpawnTicket &,
                      AzFramework::SpawnAllEntitiesOptionalArgs arguments) {
            spawnArguments = AZStd::make_unique<AzFramework::SpawnAllEntitiesOptionalArgs>(
                AZStd::move(arguments));
        });
    const SpawnRequestId hierarchyMismatch = spawner.m_component->Spawn(DefaultSpawnRequest());
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::PlaceSlot);
    ASSERT_NE(spawnArguments, nullptr);
    RuntimePrefabInstance incompleteRuntime(false, false);
    incompleteRuntime.PreInsert(701, *spawnArguments);
    incompleteRuntime.Complete(701, *spawnArguments);
    AZ::TickBus::ExecuteQueuedEvents();

    EXPECT_CALL(services.m_spawnables, DespawnAllEntities(_, _))
        .WillOnce([](AzFramework::EntitySpawnTicket &ticket,
                     AzFramework::DespawnAllEntitiesOptionalArgs arguments) {
            arguments.m_completionCallback(ticket.GetId());
        });
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::UnbindSlot);
    AZ::TickBus::ExecuteQueuedEvents();
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ReleaseSlot);
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(notifications.m_spawnFailures.size(), 2);
    EXPECT_EQ(notifications.m_spawnFailures.back().m_request, hierarchyMismatch);
    EXPECT_EQ(notifications.m_spawnFailures.back().m_failure.m_error,
              SpawnError::HierarchyMismatch);
    EXPECT_EQ(services.m_scene.ReservedCount(), 0);
}

TEST(ScenePolytreeSpawnerComponentTests,
     DeactivationCancelsPreCommitWorkAndLateCallbacksAreHarmless) {
    SpawnerServices services;
    SpawnerHarness spawner(12601, MakeConfig());
    CapturingSpawnerNotifications notifications(spawner.Id());
    spawner.Activate();
    MakeReady(spawner, services.m_registry);

    AZStd::unique_ptr<AzFramework::SpawnAllEntitiesOptionalArgs> spawnArguments;
    EXPECT_CALL(services.m_spawnables, SpawnAllEntities(_, _))
        .WillOnce([&](AzFramework::EntitySpawnTicket &,
                      AzFramework::SpawnAllEntitiesOptionalArgs arguments) {
            spawnArguments = AZStd::make_unique<AzFramework::SpawnAllEntitiesOptionalArgs>(
                AZStd::move(arguments));
        });
    (void)spawner.m_component->Spawn(DefaultSpawnRequest());
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::ResetSlot);
    services.m_scene.Complete(*spawner.m_component, SceneCommandType::PlaceSlot);
    ASSERT_NE(spawnArguments, nullptr);

    EXPECT_CALL(services.m_spawnables, DespawnAllEntities(_, _)).Times(1);
    spawner.Deactivate();
    EXPECT_EQ(spawner.m_component->GetSpawnerLifecycle(), ScenePolytreeSpawnerLifecycle::Inactive);
    EXPECT_EQ(services.m_scene.m_immediateUnbindCount, 1);
    EXPECT_EQ(services.m_scene.m_immediateResetCount, 1);
    EXPECT_EQ(services.m_scene.m_immediateReleaseCount, 1);
    EXPECT_EQ(services.m_scene.ReservedCount(), 0);

    RuntimePrefabInstance lateRuntime(true, false);
    lateRuntime.PreInsert(701, *spawnArguments);
    lateRuntime.Complete(701, *spawnArguments);
    AZ::TickBus::ExecuteQueuedEvents();
    EXPECT_TRUE(notifications.m_spawnSuccesses.empty());
    EXPECT_TRUE(notifications.m_spawnFailures.empty());

    const SpawnRequestId afterShutdown = spawner.m_component->Spawn(DefaultSpawnRequest());
    AZ::TickBus::ExecuteQueuedEvents();
    ASSERT_EQ(notifications.m_spawnFailures.size(), 1);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_request, afterShutdown);
    EXPECT_EQ(notifications.m_spawnFailures.front().m_failure.m_error, SpawnError::ShuttingDown);
}

TEST(ScenePolytreeSpawnerComponentTests, TwoSpawnersReserveIsolatedSharedForestPartitions) {
    SpawnerServices services;
    ScenePolytreeSpawnerConfig firstConfiguration = MakeConfig();
    ScenePolytreeSpawnerConfig secondConfiguration = MakeConfig();
    secondConfiguration.m_prefab = MakeSpawnerPrefab(2);
    SpawnerHarness first(12701, firstConfiguration);
    SpawnerHarness second(12702, secondConfiguration);
    first.Activate();
    second.Activate();
    MakeReady(first, services.m_registry, 11);
    MakeReady(second, services.m_registry, 22);

    EXPECT_CALL(services.m_spawnables, SpawnAllEntities(_, _)).Times(0);
    (void)first.m_component->Spawn(DefaultSpawnRequest());
    (void)second.m_component->Spawn(DefaultSpawnRequest());
    ASSERT_EQ(services.m_scene.m_pending.size(), 2);
    EXPECT_EQ(services.m_scene.m_pending[0].m_slot.m_spawner.m_partition, 11);
    EXPECT_EQ(services.m_scene.m_pending[1].m_slot.m_spawner.m_partition, 22);
    EXPECT_NE(services.m_scene.m_pending[0].m_slot.m_spawner,
              services.m_scene.m_pending[1].m_slot.m_spawner);
    EXPECT_EQ(services.m_scene.ReservedCount(), 2);
}
} // namespace ScenePolytree::Tests
