#include "SceneInstance.h"

#include <AzTest/AzTest.h>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <vector>

namespace ScenePolytree::Tests {
namespace {
constexpr AZ::u32 TankCount = 4;

[[nodiscard]] TankEntityBindings MakeBindings(AZ::u32 index) {
    const AZ::u64 firstId = static_cast<AZ::u64>(index) * 5 + 1;
    return {
        AZ::EntityId(firstId),     AZ::EntityId(firstId + 1), AZ::EntityId(firstId + 2),
        AZ::EntityId(firstId + 3), AZ::EntityId(firstId + 4),
    };
}

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
        EXPECT_TRUE(scene.Bind(index, MakeBindings(index)));
        EXPECT_TRUE(scene.MarkReady(index));
    });
    EXPECT_TRUE(scene.SetActive(true));
}

struct WrittenTransform {
    AZ::EntityId m_entity;
    AZ::Transform m_world;
};

[[nodiscard]] const AZ::Transform *FindWritten(const std::vector<WrittenTransform> &written,
                                               AZ::EntityId entity) {
    const auto found = std::ranges::find(written, entity, &WrittenTransform::m_entity);
    return found != written.end() ? &found->m_world : nullptr;
}

[[nodiscard]] TankSceneDescriptor MakeSingleTankDescriptor(const AZ::Transform &spawn) {
    TankSceneDescriptor descriptor;
    descriptor.m_spawnTransforms.push_back(spawn);
    descriptor.m_fixedStepNanoseconds = 1'000'000'000;
    return descriptor;
}

[[nodiscard]] AZ::Transform MakeVisualOffset(const AZ::Vector3 &translation, float yaw,
                                             float scale) {
    AZ::Transform result =
        AZ::Transform::CreateTranslation(translation) * AZ::Transform::CreateRotationZ(yaw);
    result.SetUniformScale(scale);
    return result;
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

    const std::vector<AZ::EntityId> expected{AZ::EntityId(6), AZ::EntityId(7), AZ::EntityId(8)};
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

    const std::vector<AZ::EntityId> expected{AZ::EntityId(7), AZ::EntityId(8)};
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

    const std::vector<AZ::EntityId> expected{AZ::EntityId(8)};
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

    const std::vector<AZ::EntityId> expected{AZ::EntityId(12), AZ::EntityId(13)};
    EXPECT_EQ(written, expected);
}

TEST(ScenePolytreeSceneInstanceTests, ActivationWaitsForEverySpawnCompletion) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    const auto indices = std::views::iota(AZ::u32{}, TankCount);
    std::ranges::for_each(
        indices, [&](AZ::u32 index) { EXPECT_TRUE(scene->Bind(index, MakeBindings(index))); });
    EXPECT_FALSE(scene->SetActive(true));
    std::ranges::for_each(indices, [&](AZ::u32 index) { EXPECT_TRUE(scene->MarkReady(index)); });
    EXPECT_TRUE(scene->SetActive(true));
}

TEST(ScenePolytreeSceneInstanceTests, DuplicateTargetsAndStaleTankIndicesAreRejected) {
    auto scene = Internal::SceneInstance::Create(MakeDescriptor());
    ASSERT_NE(scene, nullptr);
    EXPECT_FALSE(scene->Bind(
        0, {AZ::EntityId(1), AZ::EntityId(1), AZ::EntityId(2), AZ::EntityId(3), AZ::EntityId(4)}));
    EXPECT_TRUE(scene->Bind(0, MakeBindings(0)));
    EXPECT_FALSE(scene->Bind(
        1, {AZ::EntityId(3), AZ::EntityId(6), AZ::EntityId(7), AZ::EntityId(8), AZ::EntityId(9)}));
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

TEST(ScenePolytreeSceneInstanceTests,
     BindProjectedPreservesScaledSourceOffsetsAndAuthoredPivotInvariants) {
    const AZ::Transform spawn = AZ::Transform::CreateTranslation(AZ::Vector3(10.0f, -4.0f, 3.0f)) *
                                AZ::Transform::CreateRotationZ(0.35f);
    const AZ::Transform turretLocal =
        AZ::Transform::CreateTranslation(AZ::Vector3(0.25f, 0.0f, 1.5f)) *
        AZ::Transform::CreateRotationZ(0.1f);
    const AZ::Transform gunLocal =
        AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 2.25f, 0.3f)) *
        AZ::Transform::CreateRotationY(-0.05f);
    const AZ::Transform turretPivotWorld = spawn * turretLocal;
    const AZ::Transform gunPivotWorld = turretPivotWorld * gunLocal;

    const AZ::Transform hullOffset =
        MakeVisualOffset(AZ::Vector3(-92.0f, 46.0f, 38.0f), 0.2f, 100.0f);
    const AZ::Transform turretOffset =
        MakeVisualOffset(AZ::Vector3(-103.0f, 7.0f, 0.6f), -0.15f, 100.0f);
    const AZ::Transform gunOffset =
        MakeVisualOffset(AZ::Vector3(0.0f, -57.0f, 57.0f), 0.05f, 100.0f);
    const std::array targetWorlds{
        spawn * hullOffset,
        turretPivotWorld * turretOffset,
        gunPivotWorld * gunOffset,
    };
    const std::array pivotWorlds{turretPivotWorld, gunPivotWorld};

    auto scene = Internal::SceneInstance::Create(MakeSingleTankDescriptor(spawn));
    ASSERT_NE(scene, nullptr);
    const TankEntityBindings bindings = MakeBindings(0);
    ASSERT_TRUE(scene->BindProjected(0, bindings, targetWorlds, pivotWorlds));
    ASSERT_TRUE(scene->MarkReady(0));
    ASSERT_TRUE(scene->SetActive(true));

    std::vector<WrittenTransform> initial;
    scene->Advance(std::chrono::nanoseconds::zero(),
                   [&](AZ::EntityId entity, const AZ::Transform &world) {
                       initial.push_back({entity, world});
                   });
    ASSERT_EQ(initial.size(), 3);
    ASSERT_NE(FindWritten(initial, bindings.m_hull), nullptr);
    ASSERT_NE(FindWritten(initial, bindings.m_turret), nullptr);
    ASSERT_NE(FindWritten(initial, bindings.m_gun), nullptr);
    EXPECT_TRUE(FindWritten(initial, bindings.m_hull)->IsClose(targetWorlds[0], 1.0e-3f));
    EXPECT_TRUE(FindWritten(initial, bindings.m_turret)->IsClose(targetWorlds[1], 1.0e-3f));
    EXPECT_TRUE(FindWritten(initial, bindings.m_gun)->IsClose(targetWorlds[2], 1.0e-3f));

    ASSERT_TRUE(scene->SubmitIntent(0, TankIntent{0.0f, 0.0f, AZ::Constants::HalfPi, 0.0f}));
    std::vector<WrittenTransform> yawed;
    scene->Advance(std::chrono::seconds(1), [&](AZ::EntityId entity, const AZ::Transform &world) {
        yawed.push_back({entity, world});
    });
    ASSERT_EQ(yawed.size(), 2);
    AZ::Transform yawedTurretLocal = turretLocal;
    yawedTurretLocal.SetRotation(
        (turretLocal.GetRotation() * AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi))
            .GetNormalized());
    const AZ::Transform yawedTurretPivot = spawn * yawedTurretLocal;
    const AZ::Transform yawedGunPivot = yawedTurretPivot * gunLocal;
    const AZ::Transform expectedTurret = yawedTurretPivot * turretOffset;
    const AZ::Transform expectedGunAfterYaw = yawedGunPivot * gunOffset;
    ASSERT_NE(FindWritten(yawed, bindings.m_turret), nullptr);
    ASSERT_NE(FindWritten(yawed, bindings.m_gun), nullptr);
    EXPECT_TRUE(FindWritten(yawed, bindings.m_turret)->IsClose(expectedTurret, 1.0e-3f));
    EXPECT_TRUE(FindWritten(yawed, bindings.m_gun)->IsClose(expectedGunAfterYaw, 1.0e-3f));
    EXPECT_TRUE(yawedTurretPivot.GetTranslation().IsClose(turretPivotWorld.GetTranslation()));
    EXPECT_NEAR((targetWorlds[1].GetTranslation() - turretPivotWorld.GetTranslation()).GetLength(),
                (expectedTurret.GetTranslation() - yawedTurretPivot.GetTranslation()).GetLength(),
                1.0e-3f);

    ASSERT_TRUE(scene->SubmitIntent(0, TankIntent{0.0f, 0.0f, 0.0f, AZ::Constants::HalfPi}));
    std::vector<WrittenTransform> pitched;
    scene->Advance(std::chrono::seconds(1), [&](AZ::EntityId entity, const AZ::Transform &world) {
        pitched.push_back({entity, world});
    });
    ASSERT_EQ(pitched.size(), 1);
    AZ::Transform pitchedGunLocal = gunLocal;
    pitchedGunLocal.SetRotation(
        (gunLocal.GetRotation() * AZ::Quaternion::CreateRotationX(AZ::Constants::HalfPi))
            .GetNormalized());
    const AZ::Transform pitchedGunPivot = yawedTurretPivot * pitchedGunLocal;
    const AZ::Transform expectedPitchedGun = pitchedGunPivot * gunOffset;
    ASSERT_NE(FindWritten(pitched, bindings.m_gun), nullptr);
    EXPECT_TRUE(FindWritten(pitched, bindings.m_gun)->IsClose(expectedPitchedGun, 1.0e-3f));
    EXPECT_NEAR(
        (expectedGunAfterYaw.GetTranslation() - yawedGunPivot.GetTranslation()).GetLength(),
        (expectedPitchedGun.GetTranslation() - pitchedGunPivot.GetTranslation()).GetLength(),
        1.0e-3f);
}

TEST(ScenePolytreeSceneInstanceTests, ForwardMotionFollowsBasisConvertedVisualFacing) {
    const AZ::Transform spawn = AZ::Transform::CreateIdentity();
    const AZ::Transform assetToLogical = AZ::Transform::CreateRotationZ(AZ::Constants::Pi);
    const AZ::Transform turretPivot =
        assetToLogical * AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, -0.75f, 1.5f));
    const AZ::Transform gunPivot =
        assetToLogical * AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, -2.0f, 1.7f));
    const std::array targetWorlds{assetToLogical, turretPivot, gunPivot};
    const std::array pivotWorlds{turretPivot, gunPivot};

    auto scene = Internal::SceneInstance::Create(MakeSingleTankDescriptor(spawn));
    ASSERT_NE(scene, nullptr);
    const TankEntityBindings bindings = MakeBindings(0);
    ASSERT_TRUE(scene->BindProjected(0, bindings, targetWorlds, pivotWorlds));
    ASSERT_TRUE(scene->MarkReady(0));
    ASSERT_TRUE(scene->SetActive(true));
    scene->Advance(std::chrono::nanoseconds::zero(), [](AZ::EntityId, const AZ::Transform &) {});

    EXPECT_TRUE(assetToLogical.TransformVector(-AZ::Vector3::CreateAxisY())
                    .IsClose(AZ::Vector3::CreateAxisY()));
    ASSERT_TRUE(scene->SubmitIntent(0, TankIntent{1.0f, 0.0f, 0.0f, 0.0f}));
    std::vector<WrittenTransform> moved;
    scene->Advance(std::chrono::seconds(1), [&](AZ::EntityId entity, const AZ::Transform &world) {
        moved.push_back({entity, world});
    });
    const AZ::Transform expectedHull =
        AZ::Transform::CreateTranslation(AZ::Vector3::CreateAxisY()) * assetToLogical;
    ASSERT_NE(FindWritten(moved, bindings.m_hull), nullptr);
    EXPECT_TRUE(FindWritten(moved, bindings.m_hull)->IsClose(expectedHull, 1.0e-4f));
}
} // namespace ScenePolytree::Tests
