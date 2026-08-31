#include "PrefabTopology.h"

#include <ScenePolytree/ScenePolytreePrefabNodeComponent.h>

#include <AzCore/Name/NameDictionary.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzTest/AzTest.h>

namespace ScenePolytree::Tests {
namespace {
class ScenePolytreeTestEnvironment final : public ::testing::Environment {
  public:
    void SetUp() override { AZ::NameDictionary::Create(); }
    void TearDown() override { AZ::NameDictionary::Destroy(); }
};

[[maybe_unused]] auto *const ScenePolytreeEnvironment =
    ::testing::AddGlobalTestEnvironment(new ScenePolytreeTestEnvironment());

[[nodiscard]] ScenePolytreeNodeDescriptor Node(const char *id, const char *parent,
                                               ScenePolytreeJointType joint) {
    return {AZ::Name(id), AZ::Name(parent), ScenePolytreeNodeType::Transform, joint,
            AZ::Transform::CreateIdentity()};
}
} // namespace

class ScenePolytreePrefabTopologyTests : public ::testing::Test {};

TEST_F(ScenePolytreePrefabTopologyTests, ExtractsLogicalTopologyFromSpawnableEntities) {
    AzFramework::Spawnable fixture;
    auto root = AZStd::make_unique<AZ::Entity>("RootEntity");
    auto *rootTransform = root->CreateComponent<AzFramework::TransformComponent>();
    rootTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f)));
    root->AddComponent(aznew ScenePolytreePrefabNodeComponent(AZ::Name("Root"), AZ::Name(),
                                                              ScenePolytreeJointType::None));
    fixture.GetEntities().push_back(AZStd::move(root));

    auto child = AZStd::make_unique<AZ::Entity>("ChildEntity");
    auto *childTransform = child->CreateComponent<AzFramework::TransformComponent>();
    childTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3::CreateAxisZ()));
    child->AddComponent(aznew ScenePolytreePrefabNodeComponent(AZ::Name("Child"), AZ::Name("Root"),
                                                               ScenePolytreeJointType::Fixed));
    fixture.GetEntities().push_back(AZStd::move(child));

    auto result = Internal::ExtractPrefabTopology(fixture);
    ASSERT_TRUE(result.IsSuccess());
    ASSERT_EQ(result.m_nodes.size(), 2);
    EXPECT_EQ(result.m_nodes[0].m_bindingId, AZ::Name("Root"));
    EXPECT_TRUE(
        result.m_nodes[0].m_initialLocal.GetTranslation().IsClose(AZ::Vector3(1.0f, 2.0f, 3.0f)));
    EXPECT_EQ(result.m_nodes[1].m_parentBindingId, AZ::Name("Root"));
}

TEST_F(ScenePolytreePrefabTopologyTests, NormalizesMultipleRootsBeforeTheirChildren) {
    auto result = Internal::ValidateAndNormalizeTopology(
        {Node("ChildB", "RootB", ScenePolytreeJointType::Fixed),
         Node("RootB", "", ScenePolytreeJointType::None),
         Node("ChildA", "RootA", ScenePolytreeJointType::Yaw),
         Node("RootA", "", ScenePolytreeJointType::None)});
    ASSERT_TRUE(result.IsSuccess());
    ASSERT_EQ(result.m_nodes.size(), 4);
    EXPECT_EQ(result.m_nodes[0].m_bindingId, AZ::Name("RootA"));
    EXPECT_EQ(result.m_nodes[1].m_bindingId, AZ::Name("RootB"));
    EXPECT_EQ(result.m_nodes[2].m_bindingId, AZ::Name("ChildA"));
    EXPECT_EQ(result.m_nodes[3].m_bindingId, AZ::Name("ChildB"));
}

TEST_F(ScenePolytreePrefabTopologyTests, RejectsMissingAndDuplicateMetadata) {
    EXPECT_EQ(Internal::ValidateAndNormalizeTopology({}).m_failure.m_code,
              ScenePolytreeResultCode::MissingTopologyMetadata);
    EXPECT_EQ(
        Internal::ValidateAndNormalizeTopology({Node("Root", "", ScenePolytreeJointType::None),
                                                Node("Root", "", ScenePolytreeJointType::None)})
            .m_failure.m_code,
        ScenePolytreeResultCode::DuplicateBindingId);
}

TEST_F(ScenePolytreePrefabTopologyTests, RejectsDanglingParentsAndCycles) {
    EXPECT_EQ(Internal::ValidateAndNormalizeTopology(
                  {Node("Child", "Absent", ScenePolytreeJointType::Fixed)})
                  .m_failure.m_code,
              ScenePolytreeResultCode::DanglingParent);
    EXPECT_EQ(
        Internal::ValidateAndNormalizeTopology({Node("A", "B", ScenePolytreeJointType::Fixed),
                                                Node("B", "A", ScenePolytreeJointType::Fixed)})
            .m_failure.m_code,
        ScenePolytreeResultCode::Cycle);
}

TEST_F(ScenePolytreePrefabTopologyTests, RejectsInvalidRootAndChildJointContracts) {
    EXPECT_EQ(
        Internal::ValidateAndNormalizeTopology({Node("Root", "", ScenePolytreeJointType::Fixed)})
            .m_failure.m_code,
        ScenePolytreeResultCode::UnsupportedJointType);
    EXPECT_EQ(Internal::ValidateAndNormalizeTopology(
                  {Node("Root", "", ScenePolytreeJointType::None),
                   Node("Child", "Root", ScenePolytreeJointType::None)})
                  .m_failure.m_code,
              ScenePolytreeResultCode::UnsupportedJointType);
}
} // namespace ScenePolytree::Tests
