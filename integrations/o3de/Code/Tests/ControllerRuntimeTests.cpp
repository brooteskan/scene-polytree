#include "SceneInstance.h"

#include <AzCore/Component/Entity.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/UserSettings/UserSettingsComponent.h>
#include <AzFramework/Application/Application.h>
#include <AzFramework/Spawnable/SpawnableEntitiesManager.h>
#include <AzTest/AzTest.h>

#include <chrono>
#include <ranges>

namespace ScenePolytree::Tests {
namespace {
constexpr auto FixedStep = std::chrono::milliseconds(10);

[[nodiscard]] AZStd::vector<ScenePolytreeNodeDescriptor> MakeControllerTopology() {
    return {
        {AZ::Name("Root"), AZ::Name(), AZ::Transform::CreateIdentity()},
        {AZ::Name("Driven"), AZ::Name("Root"), AZ::Transform::CreateIdentity()},
    };
}

class TestControllerConfiguration final : public ScenePolytreeControllerConfiguration {
  public:
    AZ_RTTI(TestControllerConfiguration, "{D34FD9E3-B2D2-4A0C-8BB2-C09400A6B20D}",
            ScenePolytreeControllerConfiguration);

    [[nodiscard]] AZStd::shared_ptr<const ScenePolytreeControllerConfiguration>
    Clone() const override {
        return AZStd::make_shared<TestControllerConfiguration>(*this);
    }

    float m_distancePerSecond{100.0f};
    bool m_failConstruction{};
    bool m_returnInvalidState{};
};

class TestControllerInput final : public ScenePolytreeControllerInput {
  public:
    AZ_RTTI(TestControllerInput, "{9F6A0541-A2E5-4C2F-9C61-25D53406851C}",
            ScenePolytreeControllerInput);

    float m_distancePerSecond{};
};

struct ControllerTelemetry {
    AZ::u32 m_batchesCreated{};
    AZ::u32 m_controllersCreated{};
    AZ::u32 m_controllersDestroyed{};
    AZ::u32 m_fixedStepBatchCalls{};
    AZ::u32 m_controllerVisits{};
    AZ::u32 m_commandErrors{};
    AZ::u64 m_lastTick{};
    AZStd::vector<SceneNodeHandle> m_resolvedNodes;
};

class TestControllerBatch final : public ScenePolytreeControllerBatch {
  public:
    AZ_RTTI(TestControllerBatch, "{3BE03700-E8D6-4330-9323-95F85B531B07}",
            ScenePolytreeControllerBatch);

    explicit TestControllerBatch(AZStd::shared_ptr<ControllerTelemetry> telemetry)
        : m_telemetry(AZStd::move(telemetry)) {}

    [[nodiscard]] ScenePolytreeControllerCreateResult
    CreateController(const InstanceHandle &instance, const AZ::Name &,
                     const ScenePolytreeControllerConfiguration &configuration,
                     std::span<const ScenePolytreeResolvedControllerTarget> targets,
                     ScenePolytreeControllerCommandSink &commands) override {
        const auto *typed = azrtti_cast<const TestControllerConfiguration *>(&configuration);
        if (typed == nullptr || targets.size() != 1) {
            return {ScenePolytreeControllerResultCode::InvalidConfiguration, {}};
        }
        if (typed->m_failConstruction) {
            return {ScenePolytreeControllerResultCode::ConstructionFailed, {}};
        }
        const AZ::u32 index = aznumeric_cast<AZ::u32>(m_states.size());
        const auto motionResult = commands.SetVelocity(
            targets.front().m_token, AZ::Vector3(typed->m_distancePerSecond, 0.0f, 0.0f),
            AZ::Vector3::CreateZero());
        if (motionResult != ScenePolytreeControllerResultCode::Success) {
            ++m_telemetry->m_commandErrors;
            return {motionResult, {}};
        }
        if (typed->m_returnInvalidState) {
            return {ScenePolytreeControllerResultCode::Success, {}};
        }
        m_states.push_back({instance, targets.front().m_token});
        m_telemetry->m_resolvedNodes.push_back(targets.front().m_node);
        ++m_telemetry->m_controllersCreated;
        return {ScenePolytreeControllerResultCode::Success, {index, 1}};
    }

    ScenePolytreeControllerResultCode
    CloseInput(ScenePolytreeControllerStateHandle state) override {
        State *record = Find(state);
        if (record == nullptr) {
            return ScenePolytreeControllerResultCode::StaleHandle;
        }
        record->m_inputClosed = true;
        return ScenePolytreeControllerResultCode::Success;
    }

    ScenePolytreeControllerResultCode
    DestroyController(ScenePolytreeControllerStateHandle state) override {
        State *record = Find(state);
        if (record == nullptr) {
            return ScenePolytreeControllerResultCode::StaleHandle;
        }
        record->m_active = false;
        ++record->m_generation;
        ++m_telemetry->m_controllersDestroyed;
        return ScenePolytreeControllerResultCode::Success;
    }

    ScenePolytreeControllerResultCode
    SubmitInput(ScenePolytreeControllerStateHandle state, const ScenePolytreeControllerInput &input,
                ScenePolytreeControllerCommandSink &commands) override {
        State *record = Find(state);
        if (record == nullptr) {
            return ScenePolytreeControllerResultCode::StaleHandle;
        }
        if (record->m_inputClosed) {
            return ScenePolytreeControllerResultCode::InputClosed;
        }
        const auto *typed = azrtti_cast<const TestControllerInput *>(&input);
        if (typed == nullptr) {
            return ScenePolytreeControllerResultCode::InvalidInput;
        }
        return commands.SetVelocity(record->m_target,
                                    AZ::Vector3(typed->m_distancePerSecond, 0.0f, 0.0f),
                                    AZ::Vector3::CreateZero());
    }

    [[nodiscard]] bool HasRunningControllers() const noexcept override { return false; }

    void FixedStepBatch(ScenePolytreeControllerFixedStep step,
                        ScenePolytreeControllerCommandSink &) noexcept override {
        ++m_telemetry->m_fixedStepBatchCalls;
        m_telemetry->m_lastTick = step.m_tick;
    }

  private:
    struct State {
        InstanceHandle m_instance;
        ScenePolytreeControllerTargetToken m_target;
        AZ::u32 m_generation{1};
        bool m_active{true};
        bool m_inputClosed{};
    };

    [[nodiscard]] State *Find(ScenePolytreeControllerStateHandle state) {
        if (!state.IsValid() || state.m_index >= m_states.size()) {
            return nullptr;
        }
        State &record = m_states[state.m_index];
        return record.m_active && record.m_generation == state.m_generation ? &record : nullptr;
    }

    AZStd::shared_ptr<ControllerTelemetry> m_telemetry;
    AZStd::vector<State> m_states;
};

class TestControllerFactory final : public ScenePolytreeControllerFactory {
  public:
    AZ_RTTI(TestControllerFactory, "{EFDC9CC9-3144-408F-8468-A9B718048213}",
            ScenePolytreeControllerFactory);

    TestControllerFactory(ScenePolytreeControllerTypeId typeId,
                          AZStd::shared_ptr<ControllerTelemetry> telemetry)
        : m_typeId(typeId), m_telemetry(AZStd::move(telemetry)) {}

    [[nodiscard]] ScenePolytreeControllerTypeId GetControllerTypeId() const noexcept override {
        return m_typeId;
    }

    [[nodiscard]] AZStd::unique_ptr<ScenePolytreeControllerBatch> CreateBatch() const override {
        ++m_telemetry->m_batchesCreated;
        return AZStd::make_unique<TestControllerBatch>(m_telemetry);
    }

  private:
    ScenePolytreeControllerTypeId m_typeId;
    AZStd::shared_ptr<ControllerTelemetry> m_telemetry;
};

[[nodiscard]] ScenePolytreeControllerDeclaration
MakeDeclaration(const AZ::Name &id, ScenePolytreeControllerTypeId typeId, AZ::EntityId target,
                bool failConstruction = false, bool returnInvalidState = false) {
    auto configuration = AZStd::make_shared<TestControllerConfiguration>();
    configuration->m_failConstruction = failConstruction;
    configuration->m_returnInvalidState = returnInvalidState;
    ScenePolytreeControllerDeclaration declaration;
    declaration.m_declarationId = id;
    declaration.m_typeId = typeId;
    declaration.m_configuration = AZStd::move(configuration);
    declaration.m_targets.push_back(
        {AZ::Name("Driven"), target, AZ::Name(), ScenePolytreeControllerTargetAccess::ReadWrite});
    return declaration;
}

[[nodiscard]] std::unique_ptr<Internal::SceneInstance> MakeControllerScene(AZ::u32 capacity = 2) {
    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_partitions = {{7, capacity, MakeControllerTopology()}};
    descriptor.m_fixedStepNanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(FixedStep).count();
    descriptor.m_maxCatchUpSteps = 4;
    return Internal::SceneInstance::Create(descriptor);
}

void BindControllerSlot(Internal::SceneInstance &scene, SlotHandle slot, AZ::u64 entityBase) {
    ASSERT_EQ(scene.BindSlot(slot, {{AZ::Name("Root"), AZ::EntityId(entityBase),
                                     AZ::Transform::CreateIdentity()},
                                    {AZ::Name("Driven"), AZ::EntityId(entityBase + 1),
                                     AZ::Transform::CreateIdentity()}}),
              ScenePolytreeResultCode::Success);
}

class RemappedBehaviorProvider final : public AZ::Component, public ScenePolytreeBehaviorProvider {
  public:
    AZ_COMPONENT(RemappedBehaviorProvider, "{1C4F72CB-B5DF-479D-A4B0-DF55ADBBA0B0}",
                 ScenePolytreeBehaviorProvider);

    static void Reflect(AZ::ReflectContext *context) {
        if (auto *serialize = azrtti_cast<AZ::SerializeContext *>(context)) {
            serialize->Class<RemappedBehaviorProvider, AZ::Component>()->Version(1)->Field(
                "Target", &RemappedBehaviorProvider::m_target);
        }
    }

    void Activate() override {}
    void Deactivate() override {}

    [[nodiscard]] ScenePolytreeControllerDeclaration
    CopyScenePolytreeControllerDeclaration() const override {
        ScenePolytreeControllerDeclaration declaration;
        declaration.m_declarationId = AZ::Name("Remapped");
        declaration.m_typeId = azrtti_typeid<TestControllerBatch>();
        declaration.m_configuration = AZStd::make_shared<TestControllerConfiguration>();
        declaration.m_targets.push_back({AZ::Name("Rotor"), m_target, AZ::Name{},
                                         ScenePolytreeControllerTargetAccess::ReadWrite});
        return declaration;
    }

    AZ::EntityId m_target;
};

class RemapTestApplication final : public AzFramework::Application {
  public:
    void SetSettingsRegistrySpecializations(
        AZ::SettingsRegistryInterface::Specializations &specializations) override {
        Application::SetSettingsRegistrySpecializations(specializations);
        specializations.Append("test");
        specializations.Append("scene_polytree_controller_remap");
    }

    [[nodiscard]] AZ::ComponentTypeList GetRequiredSystemComponents() const override { return {}; }
};

class ScenePolytreeControllerRemapTests : public ::testing::Test {
  public:
    void SetUp() override {
        m_application = AZStd::make_unique<RemapTestApplication>();
        AZ::ComponentApplication::Descriptor descriptor;
        AZ::ComponentApplication::StartupParameters startup;
        startup.m_loadSettingsRegistry = false;
        startup.m_loadAssetCatalog = false;
        startup.m_loadStaticModules = false;
        startup.m_loadDynamicModules = false;
        m_application->Start(descriptor, startup);
        m_application->RegisterComponentDescriptor(RemappedBehaviorProvider::CreateDescriptor());
        m_ownedManager = AZStd::make_unique<AzFramework::SpawnableEntitiesManager>();
        m_manager = m_ownedManager.get();
    }

    void TearDown() override {
        ProcessQueue();
        m_ownedManager.reset();
        m_manager = nullptr;
        m_application.reset();
    }

    void ProcessQueue() {
        if (m_manager == nullptr) {
            return;
        }
        const auto attempts = std::views::iota(AZ::u32{}, AZ::u32{1'000});
        (void)std::ranges::find_if(attempts, [&](AZ::u32) {
            return m_manager->ProcessQueue(
                       AzFramework::SpawnableEntitiesManager::CommandQueuePriority::High |
                       AzFramework::SpawnableEntitiesManager::CommandQueuePriority::Regular) ==
                   AzFramework::SpawnableEntitiesManager::CommandQueueStatus::NoCommandsLeft;
        });
    }

    AZStd::unique_ptr<RemapTestApplication> m_application;
    AZStd::unique_ptr<AzFramework::SpawnableEntitiesManager> m_ownedManager;
    AzFramework::SpawnableEntitiesManager *m_manager{};
};
} // namespace

TEST(ScenePolytreeControllerRuntimeTests,
     OneShotStartAndEventUpdatePersistentMotionWithoutFixedStepCallbacks) {
    auto scene = MakeControllerScene();
    ASSERT_NE(scene, nullptr);
    const SpawnerHandle spawner{SceneHandle{81}, 7, 1};
    const SlotResult firstSlot = scene->ReserveSlot(spawner);
    const SlotResult secondSlot = scene->ReserveSlot(spawner);
    ASSERT_TRUE(firstSlot.IsSuccess());
    ASSERT_TRUE(secondSlot.IsSuccess());
    BindControllerSlot(*scene, firstSlot.m_handle, 20'000);
    BindControllerSlot(*scene, secondSlot.m_handle, 30'000);

    const InstanceHandle first{firstSlot.m_handle, 11};
    const InstanceHandle second{secondSlot.m_handle, 11};
    const auto typeId = AZ::TypeId("{7842E1A6-352B-45C5-90C6-9885575736CD}");
    auto telemetry = AZStd::make_shared<ControllerTelemetry>();
    auto factory = AZStd::make_shared<TestControllerFactory>(typeId, telemetry);
    const Internal::SceneInstance::ControllerFactoryResolver resolver =
        [factory, typeId](ScenePolytreeControllerTypeId requested) {
            return requested == typeId
                       ? AZStd::shared_ptr<const ScenePolytreeControllerFactory>(factory)
                       : AZStd::shared_ptr<const ScenePolytreeControllerFactory>{};
        };

    EXPECT_EQ(
        scene->AttachControllers(
            first, {MakeDeclaration(AZ::Name("Drive"), typeId, AZ::EntityId(20'001))}, resolver),
        ScenePolytreeResultCode::Success);
    EXPECT_EQ(
        scene->AttachControllers(
            second, {MakeDeclaration(AZ::Name("Drive"), typeId, AZ::EntityId(30'001))}, resolver),
        ScenePolytreeResultCode::Success);
    ASSERT_EQ(telemetry->m_resolvedNodes.size(), 2);
    EXPECT_EQ(telemetry->m_resolvedNodes[0].m_slot, firstSlot.m_handle);
    EXPECT_EQ(telemetry->m_resolvedNodes[1].m_slot, secondSlot.m_handle);
    EXPECT_EQ(telemetry->m_batchesCreated, 1);
    EXPECT_EQ(telemetry->m_controllersCreated, 2);
    const auto firstController = scene->FindController(first, AZ::Name("Drive"));
    ASSERT_TRUE(firstController.IsSuccess());
    TestControllerInput firstInput;
    firstInput.m_distancePerSecond = 200.0f;
    EXPECT_EQ(scene->SubmitControllerInput(firstController.m_handle, firstInput),
              ScenePolytreeControllerResultCode::Success);

    scene->SetActive(true);
    EXPECT_TRUE(scene->NeedsTick());
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});
    EXPECT_EQ(telemetry->m_fixedStepBatchCalls, 0);
    AZ::Transform firstWorld = AZ::Transform::CreateIdentity();
    AZ::Transform secondWorld = AZ::Transform::CreateIdentity();
    bool wroteFirst{};
    bool wroteSecond{};
    scene->Advance(FixedStep, [&](AZ::EntityId entity, const AZ::Transform &world) {
        if (entity == AZ::EntityId(20'001)) {
            firstWorld = world;
            wroteFirst = true;
        } else if (entity == AZ::EntityId(30'001)) {
            secondWorld = world;
            wroteSecond = true;
        }
    });
    EXPECT_EQ(telemetry->m_fixedStepBatchCalls, 0);
    EXPECT_EQ(telemetry->m_controllerVisits, 0);
    EXPECT_EQ(telemetry->m_commandErrors, 0);
    EXPECT_TRUE(wroteFirst);
    EXPECT_TRUE(wroteSecond);
    EXPECT_TRUE(firstWorld.GetTranslation().IsClose(AZ::Vector3(2.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(secondWorld.GetTranslation().IsClose(AZ::Vector3(1.0f, 0.0f, 0.0f)));
    scene->Advance(FixedStep * 3, [](AZ::EntityId, const AZ::Transform &) {});
    EXPECT_EQ(telemetry->m_fixedStepBatchCalls, 0);
    EXPECT_EQ(telemetry->m_controllerVisits, 0);
}

TEST(ScenePolytreeControllerRuntimeTests,
     InputClosureDetachAndReattachRejectEveryStalePublicHandle) {
    auto scene = MakeControllerScene(1);
    ASSERT_NE(scene, nullptr);
    const SlotResult slot = scene->ReserveSlot({SceneHandle{82}, 7, 1});
    ASSERT_TRUE(slot.IsSuccess());
    BindControllerSlot(*scene, slot.m_handle, 40'000);
    const InstanceHandle instance{slot.m_handle, 12};
    const auto typeId = AZ::TypeId("{B87DA2F2-3F28-48D8-80BA-778AD8014E52}");
    auto telemetry = AZStd::make_shared<ControllerTelemetry>();
    auto factory = AZStd::make_shared<TestControllerFactory>(typeId, telemetry);
    const Internal::SceneInstance::ControllerFactoryResolver resolver =
        [factory](ScenePolytreeControllerTypeId) {
            return AZStd::shared_ptr<const ScenePolytreeControllerFactory>(factory);
        };

    ASSERT_EQ(
        scene->AttachControllers(
            instance, {MakeDeclaration(AZ::Name("Drive"), typeId, AZ::EntityId(40'001))}, resolver),
        ScenePolytreeResultCode::Success);
    const auto original = scene->FindController(instance, AZ::Name("Drive"));
    ASSERT_TRUE(original.IsSuccess());
    TestControllerInput input;
    input.m_distancePerSecond = 200.0f;
    EXPECT_EQ(scene->SubmitControllerInput(original.m_handle, input),
              ScenePolytreeControllerResultCode::Success);
    EXPECT_EQ(scene->CloseControllerInput(instance), ScenePolytreeControllerResultCode::Success);
    EXPECT_EQ(scene->SubmitControllerInput(original.m_handle, input),
              ScenePolytreeControllerResultCode::InputClosed);
    EXPECT_EQ(scene->UnbindSlot(slot.m_handle), ScenePolytreeResultCode::ControllersActive);
    EXPECT_EQ(scene->DetachControllers(instance), ScenePolytreeControllerResultCode::Success);
    EXPECT_EQ(scene->SubmitControllerInput(original.m_handle, input),
              ScenePolytreeControllerResultCode::StaleHandle);

    ASSERT_EQ(
        scene->AttachControllers(
            instance, {MakeDeclaration(AZ::Name("Drive"), typeId, AZ::EntityId(40'001))}, resolver),
        ScenePolytreeResultCode::Success);
    const auto replacement = scene->FindController(instance, AZ::Name("Drive"));
    ASSERT_TRUE(replacement.IsSuccess());
    EXPECT_NE(replacement.m_handle, original.m_handle);
    EXPECT_EQ(scene->SubmitControllerInput(original.m_handle, input),
              ScenePolytreeControllerResultCode::StaleHandle);
    EXPECT_EQ(scene->SubmitControllerInput(replacement.m_handle, input),
              ScenePolytreeControllerResultCode::Success);
    EXPECT_EQ(scene->DetachControllers(instance), ScenePolytreeControllerResultCode::Success);
    EXPECT_EQ(telemetry->m_controllersDestroyed, 2);
    EXPECT_EQ(scene->UnbindSlot(slot.m_handle), ScenePolytreeResultCode::Success);
}

TEST(ScenePolytreeControllerRuntimeTests,
     InvalidMissingCrossInstanceConflictAndConstructionFailuresAreTransactional) {
    auto scene = MakeControllerScene();
    ASSERT_NE(scene, nullptr);
    const SpawnerHandle spawner{SceneHandle{83}, 7, 1};
    const SlotResult firstSlot = scene->ReserveSlot(spawner);
    const SlotResult secondSlot = scene->ReserveSlot(spawner);
    ASSERT_TRUE(firstSlot.IsSuccess());
    ASSERT_TRUE(secondSlot.IsSuccess());
    BindControllerSlot(*scene, firstSlot.m_handle, 50'000);
    BindControllerSlot(*scene, secondSlot.m_handle, 60'000);
    const InstanceHandle first{firstSlot.m_handle, 13};
    const auto typeId = AZ::TypeId("{BD37613E-F89A-44A4-B594-8A67B829F114}");
    auto telemetry = AZStd::make_shared<ControllerTelemetry>();
    auto factory = AZStd::make_shared<TestControllerFactory>(typeId, telemetry);
    const Internal::SceneInstance::ControllerFactoryResolver resolver =
        [factory](ScenePolytreeControllerTypeId) {
            return AZStd::shared_ptr<const ScenePolytreeControllerFactory>(factory);
        };

    EXPECT_EQ(
        scene->AttachControllers(
            first, {MakeDeclaration(AZ::Name("Missing"), typeId, AZ::EntityId(99'999))}, resolver),
        ScenePolytreeResultCode::ControllerTargetNotFound);
    EXPECT_EQ(
        scene->AttachControllers(
            first, {MakeDeclaration(AZ::Name("Cross"), typeId, AZ::EntityId(60'001))}, resolver),
        ScenePolytreeResultCode::ControllerTargetOutsideInstance);
    auto left = MakeDeclaration(AZ::Name("Left"), typeId, AZ::EntityId(50'001));
    auto right = MakeDeclaration(AZ::Name("Right"), typeId, AZ::EntityId(50'001));
    EXPECT_EQ(scene->AttachControllers(first, {left, right}, resolver),
              ScenePolytreeResultCode::ControllerWriteConflict);
    EXPECT_EQ(
        scene->AttachControllers(
            first, {MakeDeclaration(AZ::Name("NoFactory"), typeId, AZ::EntityId(50'001))}, {}),
        ScenePolytreeResultCode::ControllerFactoryNotFound);
    EXPECT_EQ(scene->AttachControllers(
                  first,
                  {MakeDeclaration(AZ::Name("Construction"), typeId, AZ::EntityId(50'001), true)},
                  resolver),
              ScenePolytreeResultCode::ControllerConstructionFailed);
    EXPECT_EQ(scene->AttachControllers(first,
                                       {MakeDeclaration(AZ::Name("InvalidState"), typeId,
                                                        AZ::EntityId(50'001), false, true)},
                                       resolver),
              ScenePolytreeResultCode::ControllerConstructionFailed);
    EXPECT_EQ(scene->GetStatistics().m_activeMotionCount, 0);
    EXPECT_EQ(scene->UnbindSlot(firstSlot.m_handle), ScenePolytreeResultCode::Success);
    EXPECT_EQ(telemetry->m_controllersCreated, 0);
}

TEST(ScenePolytreeControllerRuntimeTests,
     StructurallyDifferentPartitionsUseIndependentControllerLibrariesInOneScene) {
    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_partitions = {
        {7, 1, MakeControllerTopology()},
        {8,
         1,
         {{AZ::Name("Base"), AZ::Name(), AZ::Transform::CreateIdentity()},
          {AZ::Name("Arm"), AZ::Name("Base"), AZ::Transform::CreateIdentity()},
          {AZ::Name("Tool"), AZ::Name("Arm"), AZ::Transform::CreateIdentity()}}},
    };
    descriptor.m_fixedStepNanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(FixedStep).count();
    auto scene = Internal::SceneInstance::Create(descriptor);
    ASSERT_NE(scene, nullptr);
    const SlotResult firstSlot = scene->ReserveSlot({SceneHandle{84}, 7, 1});
    const SlotResult secondSlot = scene->ReserveSlot({SceneHandle{84}, 8, 1});
    ASSERT_TRUE(firstSlot.IsSuccess());
    ASSERT_TRUE(secondSlot.IsSuccess());
    BindControllerSlot(*scene, firstSlot.m_handle, 80'000);
    ASSERT_EQ(scene->BindSlot(
                  secondSlot.m_handle,
                  {{AZ::Name("Base"), AZ::EntityId(90'000), AZ::Transform::CreateIdentity()},
                   {AZ::Name("Arm"), AZ::EntityId(90'001), AZ::Transform::CreateIdentity()},
                   {AZ::Name("Tool"), AZ::EntityId(90'002), AZ::Transform::CreateIdentity()}}),
              ScenePolytreeResultCode::Success);

    const auto firstType = AZ::TypeId("{C1200A20-B8E6-4DF2-ACB3-FC24B1DBAD9C}");
    const auto secondType = AZ::TypeId("{30643590-42C5-4A2A-BAA9-D1D790125B38}");
    auto firstTelemetry = AZStd::make_shared<ControllerTelemetry>();
    auto secondTelemetry = AZStd::make_shared<ControllerTelemetry>();
    auto firstFactory = AZStd::make_shared<TestControllerFactory>(firstType, firstTelemetry);
    auto secondFactory = AZStd::make_shared<TestControllerFactory>(secondType, secondTelemetry);
    const Internal::SceneInstance::ControllerFactoryResolver resolver =
        [firstFactory, secondFactory, firstType](ScenePolytreeControllerTypeId requested) {
            return requested == firstType
                       ? AZStd::shared_ptr<const ScenePolytreeControllerFactory>(firstFactory)
                       : AZStd::shared_ptr<const ScenePolytreeControllerFactory>(secondFactory);
        };
    const InstanceHandle first{firstSlot.m_handle, 14};
    const InstanceHandle second{secondSlot.m_handle, 15};
    ASSERT_EQ(scene->AttachControllers(
                  first,
                  {MakeDeclaration(AZ::Name("FirstLibrary"), firstType, AZ::EntityId(80'001))},
                  resolver),
              ScenePolytreeResultCode::Success);
    ASSERT_EQ(scene->AttachControllers(
                  second,
                  {MakeDeclaration(AZ::Name("SecondLibrary"), secondType, AZ::EntityId(90'002))},
                  resolver),
              ScenePolytreeResultCode::Success);
    ASSERT_EQ(firstTelemetry->m_resolvedNodes.size(), 1);
    ASSERT_EQ(secondTelemetry->m_resolvedNodes.size(), 1);
    EXPECT_EQ(firstTelemetry->m_resolvedNodes.front().m_bindingId, AZ::Name("Driven"));
    EXPECT_EQ(secondTelemetry->m_resolvedNodes.front().m_bindingId, AZ::Name("Tool"));

    scene->SetActive(true);
    scene->Advance(FixedStep, [](AZ::EntityId, const AZ::Transform &) {});
    EXPECT_EQ(firstTelemetry->m_fixedStepBatchCalls, 0);
    EXPECT_EQ(secondTelemetry->m_fixedStepBatchCalls, 0);
    EXPECT_EQ(scene->GetStatistics().m_activeMotionCount, 2);
}

TEST_F(ScenePolytreeControllerRemapTests,
       RootOwnedBehaviorTargetReferencesRemapIndependentlyForEverySpawnAllCall) {
    const AZ::Data::AssetId assetId(AZ::Uuid("{130F0C59-90F2-45A6-A63A-C2D364BC8E20}"), 1);
    auto *spawnable =
        aznew AzFramework::Spawnable(assetId, AZ::Data::AssetData::AssetStatus::Ready);
    auto root = AZStd::make_unique<AZ::Entity>(AZ::EntityId(70'001), "Root");
    auto *provider = root->CreateComponent<RemappedBehaviorProvider>();
    provider->m_target = AZ::EntityId(70'002);
    spawnable->GetEntities().push_back(AZStd::move(root));
    auto driven = AZStd::make_unique<AZ::Entity>(AZ::EntityId(70'002), "Driven");
    spawnable->GetEntities().push_back(AZStd::move(driven));
    AZ::Data::Asset<AzFramework::Spawnable> asset(spawnable, AZ::Data::AssetLoadBehavior::Default);
    AzFramework::EntitySpawnTicket ticket(asset);

    struct ObservedInstance {
        AZ::EntityId m_root;
        AZ::EntityId m_target;
    };
    AZStd::vector<ObservedInstance> observed;
    std::ranges::for_each(std::views::iota(AZ::u32{}, AZ::u32{2}), [&](AZ::u32) {
        AzFramework::SpawnAllEntitiesOptionalArgs arguments;
        arguments.m_completionCallback =
            [&observed](AzFramework::EntitySpawnTicket::Id,
                        AzFramework::SpawnableConstEntityContainerView entities) {
                ASSERT_EQ(entities.size(), 2);
                const AZ::Entity *spawnedRoot = entities[0];
                const AZ::Entity *spawnedTarget = entities[1];
                const auto *spawnedProvider =
                    spawnedRoot->FindComponent<RemappedBehaviorProvider>();
                ASSERT_NE(spawnedProvider, nullptr);
                const auto declaration = spawnedProvider->CopyScenePolytreeControllerDeclaration();
                ASSERT_EQ(declaration.m_targets.size(), 1);
                EXPECT_EQ(declaration.m_targets.front().m_slot, AZ::Name("Rotor"));
                EXPECT_EQ(declaration.m_targets.front().m_prefabEntity, spawnedTarget->GetId());
                EXPECT_NE(declaration.m_targets.front().m_prefabEntity, AZ::EntityId(70'002));
                EXPECT_EQ(spawnedTarget->FindComponent<RemappedBehaviorProvider>(), nullptr);
                EXPECT_NE(spawnedTarget->GetId(), AZ::EntityId(70'002));
                observed.push_back(
                    {spawnedRoot->GetId(), declaration.m_targets.front().m_prefabEntity});
            };
        m_manager->SpawnAllEntities(ticket, AZStd::move(arguments));
    });
    ProcessQueue();

    ASSERT_EQ(observed.size(), 2);
    EXPECT_NE(observed[0].m_root, observed[1].m_root);
    EXPECT_NE(observed[0].m_target, observed[1].m_target);
}
} // namespace ScenePolytree::Tests
