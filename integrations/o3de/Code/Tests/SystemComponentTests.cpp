#include "ScenePolytreeSystemComponent.h"

#include <AzTest/AzTest.h>

namespace ScenePolytree::Tests {
namespace {
[[nodiscard]] TankSceneDescriptor MakeSystemDescriptor() {
    TankSceneDescriptor descriptor;
    descriptor.m_spawnTransforms.assign(4, AZ::Transform::CreateIdentity());
    return descriptor;
}
} // namespace

TEST(ScenePolytreeSystemComponentTests, MutationsAreQueuedUntilTheSystemTick) {
    ScenePolytreeSystemComponent system;
    const SceneHandle scene = system.CreateTankScene(MakeSystemDescriptor());
    ASSERT_TRUE(scene.IsValid());
    EXPECT_TRUE(system.IsSceneAlive(scene));
    EXPECT_EQ(system.GetSceneStatistics(scene).m_tankCount, 0);

    system.OnTick(0.0f, AZ::ScriptTimePoint{});
    EXPECT_EQ(system.GetSceneStatistics(scene).m_tankCount, 4);

    system.DestroyScene(scene);
    EXPECT_FALSE(system.IsSceneAlive(scene));
    EXPECT_FALSE(system.SubmitTankIntent(TankHandle{scene, 0}, TankIntent{}));
    system.OnTick(0.0f, AZ::ScriptTimePoint{});
    EXPECT_EQ(system.GetSceneStatistics(scene).m_tankCount, 0);
}

TEST(ScenePolytreeSystemComponentTests, UsesTheDocumentedCentralTickOrder) {
    ScenePolytreeSystemComponent system;
    EXPECT_EQ(system.GetTickOrder(), AZ::TICK_GAME + 1);
}
} // namespace ScenePolytree::Tests
