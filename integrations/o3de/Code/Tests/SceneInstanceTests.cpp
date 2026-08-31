#include "SceneInstance.h"

#include <AzTest/AzTest.h>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <vector>

namespace ScenePolytree::Tests {
namespace {
constexpr AZ::u32 TankCount = 4;

[[nodiscard]] TankSceneDescriptor MakeDescriptor() {
    TankSceneDescriptor descriptor;
    const auto indices = std::views::iota(AZ::u32{}, TankCount);
    std::ranges::transform(indices, std::back_inserter(descriptor.m_spawnTransforms),
                           [](AZ::u32 index) {
                               return AZ::Transform::CreateTranslation(
                                   AZ::Vector3(static_cast<float>(index) * 10.0f, 0.0f, 0.0f));
                           });
    return descriptor;
}

void BindAndReady(Internal::SceneInstance &scene) {
    const auto indices = std::views::iota(AZ::u32{}, TankCount);
    std::ranges::for_each(indices, [&](AZ::u32 index) {
        const AZ::u64 firstId = static_cast<AZ::u64>(index) * 3 + 1;
        EXPECT_TRUE(scene.Bind(index, {
                                          AZ::EntityId(firstId),
                                          AZ::EntityId(firstId + 1),
                                          AZ::EntityId(firstId + 2),
                                      }));
        EXPECT_TRUE(scene.MarkReady(index));
    });
    EXPECT_TRUE(scene.SetActive(true));
}
} // namespace

TEST(ScenePolytreeSceneInstanceTests, OnePlayerAndThreeAiShareOneForest) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->TankCount(), TankCount);
    BindAndReady(*scene);

    std::vector<AZ::EntityId> written;
    scene->Advance(
        std::chrono::nanoseconds::zero(),
        [&](AZ::EntityId entityId, const AZ::Transform &) { written.push_back(entityId); });
    EXPECT_EQ(written.size(), TankCount * 3);
    EXPECT_EQ(scene->GetStatistics().m_lastSynchronizedNodeCount, TankCount * 3);
    EXPECT_FALSE(scene->NeedsTick());
}

TEST(ScenePolytreeSceneInstanceTests, SynchronizesOnlyChangedTankSubtree) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    BindAndReady(*scene);
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});

    ASSERT_TRUE(scene->SubmitIntent(1, TankIntent{2.0f, 0.0f, 0.0f, 0.0f}));
    std::vector<AZ::EntityId> written;
    scene->Advance(
        std::chrono::nanoseconds(16'666'667),
        [&](AZ::EntityId entityId, const AZ::Transform &) { written.push_back(entityId); });

    const std::vector<AZ::EntityId> expected{AZ::EntityId(4), AZ::EntityId(5), AZ::EntityId(6)};
    EXPECT_EQ(written, expected);
    EXPECT_EQ(scene->GetStatistics().m_activeMotionCount, 1);
}

TEST(ScenePolytreeSceneInstanceTests, TurretMotionSynchronizesTurretAndGunOnly) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    BindAndReady(*scene);
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});

    ASSERT_TRUE(scene->SubmitIntent(1, TankIntent{0.0f, 0.0f, 1.0f, 0.0f}));
    std::vector<AZ::EntityId> written;
    scene->Advance(
        std::chrono::nanoseconds(16'666'667),
        [&](AZ::EntityId entityId, const AZ::Transform &) { written.push_back(entityId); });

    const std::vector<AZ::EntityId> expected{AZ::EntityId(5), AZ::EntityId(6)};
    EXPECT_EQ(written, expected);
}

TEST(ScenePolytreeSceneInstanceTests, GunMotionSynchronizesGunOnly) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    BindAndReady(*scene);
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});

    ASSERT_TRUE(scene->SubmitIntent(1, TankIntent{0.0f, 0.0f, 0.0f, 1.0f}));
    std::vector<AZ::EntityId> written;
    scene->Advance(
        std::chrono::nanoseconds(16'666'667),
        [&](AZ::EntityId entityId, const AZ::Transform &) { written.push_back(entityId); });

    const std::vector<AZ::EntityId> expected{AZ::EntityId(6)};
    EXPECT_EQ(written, expected);
}

TEST(ScenePolytreeSceneInstanceTests, LocalCorrectionDirtiesOnlyNodeAndDescendants) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    BindAndReady(*scene);
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});

    ASSERT_TRUE(scene->CorrectLocal(
        2, TankNodeRole::Turret, AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 0.0f, 2.0f))));
    std::vector<AZ::EntityId> written;
    scene->Advance(
        std::chrono::nanoseconds::zero(),
        [&](AZ::EntityId entityId, const AZ::Transform &) { written.push_back(entityId); });

    const std::vector<AZ::EntityId> expected{AZ::EntityId(8), AZ::EntityId(9)};
    EXPECT_EQ(written, expected);
}

TEST(ScenePolytreeSceneInstanceTests, ActivationWaitsForEverySpawnCompletion) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    const auto indices = std::views::iota(AZ::u32{}, TankCount);
    std::ranges::for_each(indices, [&](AZ::u32 index) {
        const AZ::u64 firstId = static_cast<AZ::u64>(index) * 3 + 1;
        EXPECT_TRUE(scene->Bind(
            index, {AZ::EntityId(firstId), AZ::EntityId(firstId + 1), AZ::EntityId(firstId + 2)}));
    });
    EXPECT_FALSE(scene->SetActive(true));
    std::ranges::for_each(indices, [&](AZ::u32 index) { EXPECT_TRUE(scene->MarkReady(index)); });
    EXPECT_TRUE(scene->SetActive(true));
}

TEST(ScenePolytreeSceneInstanceTests, DuplicateTargetsAndStaleTankIndicesAreRejected) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    EXPECT_FALSE(scene->Bind(0, {AZ::EntityId(1), AZ::EntityId(1), AZ::EntityId(2)}));
    EXPECT_TRUE(scene->Bind(0, {AZ::EntityId(1), AZ::EntityId(2), AZ::EntityId(3)}));
    EXPECT_FALSE(scene->Bind(1, {AZ::EntityId(3), AZ::EntityId(4), AZ::EntityId(5)}));
    EXPECT_FALSE(scene->SubmitIntent(TankCount, TankIntent{}));
    EXPECT_FALSE(
        scene->CorrectLocal(TankCount, TankNodeRole::Hull, AZ::Transform::CreateIdentity()));
}

TEST(ScenePolytreeSceneInstanceTests, WorldCorrectionUsesDirectParentLookup) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    BindAndReady(*scene);
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});

    ASSERT_TRUE(scene->CorrectWorld(
        0, TankNodeRole::Gun, AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 3.0f, 2.0f))));
    std::vector<AZ::EntityId> written;
    scene->Advance(
        std::chrono::nanoseconds::zero(),
        [&](AZ::EntityId entityId, const AZ::Transform &) { written.push_back(entityId); });
    EXPECT_EQ(written, std::vector<AZ::EntityId>{AZ::EntityId(3)});
}

TEST(ScenePolytreeSceneInstanceTests, ZeroIntentLeavesTheSceneIdleAfterSynchronization) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    BindAndReady(*scene);
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});

    ASSERT_TRUE(scene->SubmitIntent(0, TankIntent{1.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(scene->NeedsTick());
    ASSERT_TRUE(scene->SubmitIntent(0, TankIntent{}));
    EXPECT_FALSE(scene->NeedsTick());
}
} // namespace ScenePolytree::Tests
