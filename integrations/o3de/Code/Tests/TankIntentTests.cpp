#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeComponent.h>
#include <ScenePolytree/Tank/AiTankIntentAdapterComponent.h>
#include <ScenePolytree/Tank/PlayerTankIntentAdapterComponent.h>
#include <ScenePolytree/Tank/ScenePolytreeTankSpawnerComponent.h>
#include <ScenePolytree/Tank/TankNodeBindingComponent.h>

#include <AzCore/Component/TickBus.h>
#include <AzTest/AzTest.h>

#include <type_traits>

namespace ScenePolytree::Tests {
static_assert(!std::is_base_of_v<AZ::TickBus::Handler, PlayerTankIntentAdapterComponent>);
static_assert(!std::is_base_of_v<AZ::TickBus::Handler, AiTankIntentAdapterComponent>);
static_assert(!std::is_base_of_v<AZ::TickBus::Handler, ScenePolytreeTankSpawnerComponent>);
static_assert(!std::is_base_of_v<AZ::TickBus::Handler, ScenePolytreeComponent>);
static_assert(!std::is_base_of_v<AZ::TickBus::Handler, TankNodeBindingComponent>);

TEST(ScenePolytreeTankIntentTests, PlayerAndAiProduceTheSameIntentContract) {
    const TankTuning unitTuning{1.0f, 1.0f, 1.0f, 1.0f};
    const TankIntent player = MakePlayerTankIntent({0.5f, -0.25f, 0.75f, -1.0f}, unitTuning);
    const TankIntent ai = MakeAiTankIntent({0.5f, -0.25f, 0.75f, -1.0f});
    EXPECT_EQ(player, ai);
}

TEST(ScenePolytreeTankIntentTests, PlayerAxesAreClampedBeforeTuning) {
    const TankIntent result =
        MakePlayerTankIntent({2.0f, -2.0f, 3.0f, -3.0f}, TankTuning{4.0f, 5.0f, 6.0f, 7.0f});
    EXPECT_EQ(result, (TankIntent{4.0f, -5.0f, 6.0f, -7.0f}));
}
} // namespace ScenePolytree::Tests

AZ_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
