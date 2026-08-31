#include "ScenePolytreeSystemComponent.h"

#include <ScenePolytree/ScenePolytreeComponent.h>

#include <AzTest/AzTest.h>

namespace ScenePolytree::Tests {
namespace {
[[nodiscard]] ScenePolytreeSceneDescriptor MakeSharedSystemDescriptor() {
    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_partitions = {{5,
                                2,
                                {{AZ::Name("Root"), AZ::Name(), ScenePolytreeNodeType::Transform,
                                  ScenePolytreeJointType::None, AZ::Transform::CreateIdentity()}}}};
    return descriptor;
}

[[nodiscard]] AZ::Data::Asset<AzFramework::Spawnable> MakePrefabAsset(AZ::u32 subId) {
    return {AZ::Data::AssetId(AZ::Uuid("{7DD06ADF-4F6A-4DFD-BB06-211217FD21C1}"), subId),
            azrtti_typeid<AzFramework::Spawnable>()};
}

class CapturingSceneComponent final : public ScenePolytreeComponentRequestBus::Handler {
  public:
    explicit CapturingSceneComponent(AZ::EntityId entity) { BusConnect(entity); }
    ~CapturingSceneComponent() override { BusDisconnect(); }

    void
    BeginBuild(const AZStd::vector<ResolvedScenePolytreeRegistration> &registrations) override {
        ++m_buildCount;
        m_registrations = registrations;
    }
    void FailBuild(const ScenePolytreeFailure &failure) override { m_failure = failure; }
    ScenePolytreeLifecycle GetLifecycle() const override {
        return ScenePolytreeLifecycle::Collecting;
    }
    ScenePolytreeFailure GetFailure() const override { return m_failure; }

    AZStd::vector<ResolvedScenePolytreeRegistration> m_registrations;
    ScenePolytreeFailure m_failure;
    AZ::u32 m_buildCount{};
};

class CapturingRegistrationNotifications final
    : public ScenePolytreeRegistrationNotificationBus::Handler {
  public:
    explicit CapturingRegistrationNotifications(AZ::EntityId entity) { BusConnect(entity); }
    ~CapturingRegistrationNotifications() override { BusDisconnect(); }

    void OnScenePolytreeRegistrationReady(RegistrationToken, SpawnerHandle) override {}
    void OnScenePolytreeRegistrationFailed(RegistrationToken token,
                                           const ScenePolytreeFailure &failure) override {
        m_failedToken = token;
        m_failure = failure;
    }

    RegistrationToken m_failedToken;
    ScenePolytreeFailure m_failure;
};

class ActiveSystem final {
  public:
    ActiveSystem() { m_system.Activate(); }
    ~ActiveSystem() { m_system.Deactivate(); }

    ScenePolytreeSystemComponent m_system;
};
} // namespace

TEST(ScenePolytreeSystemComponentTests,
     TwoRegistrationsResolveDeterministicallyIntoOneSharedScene) {
    ScenePolytreeSystemComponent system;
    const AZ::EntityId sceneEntity(500);
    CapturingSceneComponent scene(sceneEntity);
    EXPECT_EQ(system.RegisterSceneEntity(sceneEntity, true), ScenePolytreeResultCode::Success);

    const auto registeredSecond = system.RegisterPrefab(
        AZ::EntityId(302), AZ::EntityId{}, {MakePrefabAsset(2), 2, AZ::Name("Second")});
    const auto registeredFirst = system.RegisterPrefab(AZ::EntityId(301), AZ::EntityId{},
                                                       {MakePrefabAsset(1), 3, AZ::Name("First")});
    ASSERT_TRUE(registeredSecond.IsSuccess());
    ASSERT_TRUE(registeredFirst.IsSuccess());

    system.OnRootSpawnableReady({}, 1);
    ASSERT_EQ(scene.m_buildCount, 1);
    ASSERT_EQ(scene.m_registrations.size(), 2);
    EXPECT_EQ(scene.m_registrations[0].m_ownerEntity, AZ::EntityId(301));
    EXPECT_EQ(scene.m_registrations[0].m_partition, 1);
    EXPECT_EQ(scene.m_registrations[0].m_descriptor.m_capacity, 3);
    EXPECT_EQ(scene.m_registrations[1].m_ownerEntity, AZ::EntityId(302));
    EXPECT_EQ(scene.m_registrations[1].m_partition, 2);
    EXPECT_EQ(scene.m_registrations[1].m_descriptor.m_capacity, 2);
}

TEST(ScenePolytreeSystemComponentTests,
     InvalidAndLateRegistrationsReturnTypedFailuresWithoutBuilding) {
    ScenePolytreeSystemComponent system;
    const AZ::EntityId owner(601);
    CapturingRegistrationNotifications notifications(owner);
    const auto zeroCapacity =
        system.RegisterPrefab(owner, AZ::EntityId{}, {MakePrefabAsset(3), 0, AZ::Name("Invalid")});
    EXPECT_EQ(zeroCapacity.m_code, ScenePolytreeResultCode::ZeroCapacity);

    const auto unresolved = system.RegisterPrefab(owner, AZ::EntityId{},
                                                  {MakePrefabAsset(4), 1, AZ::Name("Unresolved")});
    ASSERT_TRUE(unresolved.IsSuccess());
    system.OnRootSpawnableReady({}, 1);
    EXPECT_EQ(notifications.m_failedToken, unresolved.m_token);
    EXPECT_EQ(notifications.m_failure.m_code, ScenePolytreeResultCode::MissingDefaultScene);

    const auto late =
        system.RegisterPrefab(owner, AZ::EntityId{}, {MakePrefabAsset(5), 1, AZ::Name("Late")});
    EXPECT_EQ(late.m_code, ScenePolytreeResultCode::RegistrationClosed);
}

TEST(ScenePolytreeSystemComponentTests,
     PrematureGameEntitiesStartedEventDoesNotCloseRootRegistration) {
    ScenePolytreeSystemComponent system;
    system.OnGameEntitiesStarted();

    const AZ::EntityId sceneEntity(701);
    CapturingSceneComponent scene(sceneEntity);
    EXPECT_EQ(system.RegisterSceneEntity(sceneEntity, true), ScenePolytreeResultCode::Success);
    const auto registration =
        system.RegisterPrefab(AZ::EntityId(702), AZ::EntityId{},
                              {MakePrefabAsset(6), 1, AZ::Name("AfterGameEntitiesStarted")});
    EXPECT_TRUE(registration.IsSuccess());

    system.OnRootSpawnableReady({}, 1);
    EXPECT_EQ(scene.m_buildCount, 1);
    EXPECT_EQ(scene.m_registrations.size(), 1);
}

TEST(ScenePolytreeSystemComponentTests, LevelComponentOwnsAndDestroysOneRuntimeScene) {
    ActiveSystem activeSystem;
    ScenePolytreeComponentConfig configuration;
    configuration.m_permanentNodes = {
        {AZ::Name("LevelRoot"), AZ::Name(), ScenePolytreeNodeType::Transform,
         ScenePolytreeJointType::None, AZ::Transform::CreateIdentity()},
    };
    ScenePolytreeComponent component(configuration);

    component.BeginBuild({});
    const SceneHandle scene = component.GetSceneHandle();
    EXPECT_TRUE(scene.IsValid());
    EXPECT_EQ(component.GetLifecycle(), ScenePolytreeLifecycle::Building);
    EXPECT_FALSE(activeSystem.m_system.IsSceneReady(scene));

    activeSystem.m_system.OnTick(0.0f, AZ::ScriptTimePoint{});
    EXPECT_EQ(component.GetLifecycle(), ScenePolytreeLifecycle::Ready);
    EXPECT_TRUE(activeSystem.m_system.IsSceneReady(scene));

    component.Deactivate();
    EXPECT_EQ(component.GetLifecycle(), ScenePolytreeLifecycle::Destroying);
    EXPECT_FALSE(activeSystem.m_system.IsSceneAlive(scene));
    activeSystem.m_system.OnTick(0.0f, AZ::ScriptTimePoint{});
    EXPECT_FALSE(activeSystem.m_system.IsSceneReady(scene));
}

TEST(ScenePolytreeSystemComponentTests, SharedSceneBecomesReadyOnTickAndOwnsPartitions) {
    ScenePolytreeSystemComponent system;
    const SceneHandle scene = system.CreateScene(MakeSharedSystemDescriptor());
    ASSERT_TRUE(scene.IsValid());
    EXPECT_FALSE(system.IsSceneReady(scene));
    EXPECT_EQ(system.ReserveSlot({scene, 5, 1}).m_code, ScenePolytreeResultCode::SceneNotReady);

    system.OnTick(0.0f, AZ::ScriptTimePoint{});
    EXPECT_TRUE(system.IsSceneReady(scene));
    const SlotResult slot = system.ReserveSlot({scene, 5, 1});
    ASSERT_TRUE(slot.IsSuccess());
    EXPECT_EQ(system.GetSceneStatistics(scene).m_partitionCount, 1);
    EXPECT_EQ(system.GetSceneStatistics(scene).m_reservedSlotCount, 1);

    system.DestroyScene(scene);
    EXPECT_EQ(system.ResetSlot(slot.m_handle), ScenePolytreeResultCode::SceneNotFound);
}

TEST(ScenePolytreeSystemComponentTests, MutationsAreQueuedUntilTheSystemTick) {
    ScenePolytreeSystemComponent system;
    const SceneHandle scene = system.CreateScene(MakeSharedSystemDescriptor());
    ASSERT_TRUE(scene.IsValid());
    EXPECT_TRUE(system.IsSceneAlive(scene));
    EXPECT_EQ(system.GetSceneStatistics(scene).m_nodeCount, 0);

    system.OnTick(0.0f, AZ::ScriptTimePoint{});
    EXPECT_EQ(system.GetSceneStatistics(scene).m_nodeCount, 2);

    system.DestroyScene(scene);
    EXPECT_FALSE(system.IsSceneAlive(scene));
    EXPECT_FALSE(system.SetSceneActive(scene, true));
    system.OnTick(0.0f, AZ::ScriptTimePoint{});
    EXPECT_EQ(system.GetSceneStatistics(scene).m_nodeCount, 0);
}

TEST(ScenePolytreeSystemComponentTests, UsesTheDocumentedCentralTickOrder) {
    ScenePolytreeSystemComponent system;
    EXPECT_EQ(system.GetTickOrder(), AZ::TICK_GAME + 1);
}
} // namespace ScenePolytree::Tests
