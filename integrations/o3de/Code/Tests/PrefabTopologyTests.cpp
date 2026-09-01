#include "PrefabTopology.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Name/NameDictionary.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/SpawnableAssetHandler.h>
#include <AzTest/AzTest.h>

namespace ScenePolytree::Tests {
namespace {
class ScenePolytreeTestEnvironment final : public ::testing::Environment {
  public:
    void SetUp() override {
        AZ::NameDictionary::Create();
        if (!AZ::Data::AssetManager::IsReady()) {
            AZ::Data::AssetManager::Descriptor descriptor;
            AZ::Data::AssetManager::Create(descriptor);
            m_ownsAssetManager = true;
        }
        m_spawnableHandler = AZStd::make_unique<AzFramework::SpawnableAssetHandler>();
        AZ::Data::AssetManager::Instance().RegisterHandler(m_spawnableHandler.get(),
                                                           azrtti_typeid<AzFramework::Spawnable>());
    }
    void TearDown() override {
        AZ::Data::AssetManager::Instance().UnregisterHandler(m_spawnableHandler.get());
        m_spawnableHandler.reset();
        if (m_ownsAssetManager) {
            AZ::Data::AssetManager::Destroy();
        }
        AZ::NameDictionary::Destroy();
    }

  private:
    AZStd::unique_ptr<AzFramework::SpawnableAssetHandler> m_spawnableHandler;
    bool m_ownsAssetManager{};
};

[[maybe_unused]] auto *const ScenePolytreeEnvironment =
    ::testing::AddGlobalTestEnvironment(new ScenePolytreeTestEnvironment());

[[nodiscard]] ScenePolytreeNodeDescriptor Node(const char *id, const char *parent) {
    return {AZ::Name(id), AZ::Name(parent), AZ::Transform::CreateIdentity()};
}
} // namespace

class ScenePolytreePrefabTopologyTests : public ::testing::Test {};

TEST_F(ScenePolytreePrefabTopologyTests, ExtractsLogicalTopologyFromSpawnableEntities) {
    AzFramework::Spawnable fixture;
    auto root = AZStd::make_unique<AZ::Entity>("Root/Entity");
    auto *rootTransform = root->CreateComponent<AzFramework::TransformComponent>();
    rootTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f)));
    const AZ::EntityId rootId = root->GetId();
    fixture.GetEntities().push_back(AZStd::move(root));

    auto child = AZStd::make_unique<AZ::Entity>("Child%Entity#1");
    auto *childTransform = child->CreateComponent<AzFramework::TransformComponent>();
    const AZ::Transform childLocal = AZ::Transform::CreateFromQuaternionAndTranslation(
        AZ::Quaternion::CreateFromEulerAnglesRadians(AZ::Vector3(0.3f, -0.2f, 0.7f)),
        AZ::Vector3(4.0f, 5.0f, 6.0f));
    childTransform->SetLocalTM(childLocal);
    childTransform->SetParent(rootId);
    const AZ::EntityId childId = child->GetId();
    fixture.GetEntities().push_back(AZStd::move(child));

    fixture.GetEntities().push_back(AZStd::make_unique<AZ::Entity>("NonTransformData"));

    auto result = Internal::ExtractPrefabTopology(fixture);
    ASSERT_TRUE(result.IsSuccess());
    ASSERT_EQ(result.m_nodes.size(), 2);
    ASSERT_EQ(result.m_entities.size(), 2);
    EXPECT_EQ(result.m_nodes[0].m_bindingId, AZ::Name("Root%2FEntity"));
    EXPECT_EQ(result.m_entities[0], rootId);
    EXPECT_TRUE(
        result.m_nodes[0].m_initialLocal.GetTranslation().IsClose(AZ::Vector3(1.0f, 2.0f, 3.0f)));
    EXPECT_EQ(result.m_nodes[1].m_bindingId, AZ::Name("Root%2FEntity/Child%25Entity#1"));
    EXPECT_EQ(result.m_nodes[1].m_parentBindingId, AZ::Name("Root%2FEntity"));
    EXPECT_TRUE(result.m_nodes[1].m_initialLocal.IsClose(childLocal));
    EXPECT_EQ(result.m_entities[1], childId);
}

TEST_F(ScenePolytreePrefabTopologyTests, NormalizesMultipleRootsBeforeTheirChildren) {
    auto result = Internal::ValidateAndNormalizeTopology(
        {Node("ChildB", "RootB"), Node("RootB", ""), Node("ChildA", "RootA"), Node("RootA", "")});
    ASSERT_TRUE(result.IsSuccess());
    ASSERT_EQ(result.m_nodes.size(), 4);
    EXPECT_EQ(result.m_nodes[0].m_bindingId, AZ::Name("RootA"));
    EXPECT_EQ(result.m_nodes[1].m_bindingId, AZ::Name("RootB"));
    EXPECT_EQ(result.m_nodes[2].m_bindingId, AZ::Name("ChildA"));
    EXPECT_EQ(result.m_nodes[3].m_bindingId, AZ::Name("ChildB"));
}

TEST_F(ScenePolytreePrefabTopologyTests, RejectsEmptyAndDuplicateTopology) {
    EXPECT_EQ(Internal::ValidateAndNormalizeTopology({}).m_failure.m_code,
              ScenePolytreeResultCode::EmptyPrefabHierarchy);
    EXPECT_EQ(Internal::ValidateAndNormalizeTopology({Node("Root", ""), Node("Root", "")})
                  .m_failure.m_code,
              ScenePolytreeResultCode::DuplicateBindingId);
}

TEST_F(ScenePolytreePrefabTopologyTests, RejectsDanglingParentsAndCycles) {
    EXPECT_EQ(Internal::ValidateAndNormalizeTopology({Node("Child", "Absent")}).m_failure.m_code,
              ScenePolytreeResultCode::DanglingParent);
    EXPECT_EQ(
        Internal::ValidateAndNormalizeTopology({Node("A", "B"), Node("B", "A")}).m_failure.m_code,
        ScenePolytreeResultCode::Cycle);
}

TEST_F(ScenePolytreePrefabTopologyTests, RejectsDuplicateSiblingHierarchyPaths) {
    AzFramework::Spawnable fixture;
    auto root = AZStd::make_unique<AZ::Entity>("Root");
    root->CreateComponent<AzFramework::TransformComponent>();
    const AZ::EntityId rootId = root->GetId();
    fixture.GetEntities().push_back(AZStd::move(root));
    std::ranges::for_each(std::views::iota(0, 2), [&](int) {
        auto child = AZStd::make_unique<AZ::Entity>("Pivot");
        auto *transform = child->CreateComponent<AzFramework::TransformComponent>();
        transform->SetParent(rootId);
        fixture.GetEntities().push_back(AZStd::move(child));
    });

    EXPECT_EQ(Internal::ExtractPrefabTopology(fixture).m_failure.m_code,
              ScenePolytreeResultCode::DuplicateBindingId);
}
} // namespace ScenePolytree::Tests
