#include "tank_example.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <ranges>
#include <utility>

namespace {
using namespace scene_polytree::examples::tank;

[[nodiscard]] bool pose_approximately_equal(const rigid_pose &left,
                                            const rigid_pose &right) noexcept {
    return approximately_equal(left.translation.x, right.translation.x) &&
           approximately_equal(left.translation.y, right.translation.y) &&
           approximately_equal(left.translation.z, right.translation.z) &&
           approximately_equal(left.rotation.w, right.rotation.w) &&
           approximately_equal(left.rotation.x, right.rotation.x) &&
           approximately_equal(left.rotation.y, right.rotation.y) &&
           approximately_equal(left.rotation.z, right.rotation.z);
}

int articulation_and_parent_motion() {
    authoring_scene authoring;
    const tank_asset asset;
    const auto authored = instantiate_tank(authoring, asset, {});
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 2;
    }
    auto runtime = std::move(frozen).value();
    const auto instance = resolve_tank(runtime, authored);
    active_set active{runtime.topology()};
    rigid_policy policy;
    scene_polytree::motion::fixed_step_sequence sequence{std::chrono::milliseconds{100}};
    scene_polytree::motion::motion_evaluation_workspace<rigid_pose> motion_workspace;
    scene_polytree::transform_evaluation_workspace transform_workspace;

    const auto initial = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    if (!initial || initial.changed_nodes.size() != 3) {
        return 3;
    }
    const auto hull_revision = runtime.state().record(instance.hull).world_revision;

    const tank_intent articulation{0.0, 0.0, 1.0, 0.5};
    if (apply_intent(active, instance, articulation, policy) !=
        scene_polytree::motion::motion_error::none) {
        return 4;
    }
    const auto articulated = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    const std::array expected_articulated{instance.turret, instance.gun};
    const auto &turret_local = runtime.state().local(instance.turret);
    const auto &gun_local = runtime.state().local(instance.gun);
    const auto &turret_world = runtime.state().world(instance.turret);
    const auto &gun_world = runtime.state().world(instance.gun);
    if (!articulated || !std::ranges::equal(articulated.integrated_nodes, expected_articulated) ||
        !std::ranges::equal(articulated.changed_nodes, expected_articulated) ||
        runtime.state().record(instance.hull).world_revision != hull_revision ||
        turret_local.translation != asset.turret_pivot.translation ||
        gun_local.translation != asset.gun_pivot.translation ||
        !approximately_equal(turret_local.rotation.w, std::cos(0.05)) ||
        !approximately_equal(turret_local.rotation.z, std::sin(0.05)) ||
        !approximately_equal(gun_local.rotation.w, std::cos(0.025)) ||
        !approximately_equal(gun_local.rotation.x, std::sin(0.025)) ||
        !approximately_equal(turret_world.rotation.w, std::cos(0.05)) ||
        !approximately_equal(turret_world.rotation.z, std::sin(0.05)) ||
        !approximately_equal(gun_world.translation.x, -std::sin(0.1)) ||
        !approximately_equal(gun_world.translation.y, std::cos(0.1)) ||
        !approximately_equal(gun_world.translation.z, 1.25)) {
        return 5;
    }

    const tank_intent hull_motion{3.0, 0.0, 0.0, 0.0};
    if (apply_intent(active, instance, hull_motion, policy) !=
        scene_polytree::motion::motion_error::none) {
        return 6;
    }
    const auto parent_moved = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    const std::array full_tank{instance.hull, instance.turret, instance.gun};
    if (!parent_moved ||
        !std::ranges::equal(parent_moved.integrated_nodes, std::array{instance.hull}) ||
        !std::ranges::equal(parent_moved.changed_nodes, full_tank) ||
        !approximately_equal(runtime.state().local(instance.hull).translation.y, 0.3)) {
        return 7;
    }
    return 0;
}

int equivalent_controller_replay() {
    authoring_scene authoring;
    const tank_asset asset;
    const rigid_pose shared_spawn{{2.0, -3.0, 0.5}, {}};
    const auto player = instantiate_tank(authoring, asset, shared_spawn);
    const auto ai = instantiate_tank(authoring, asset, shared_spawn);
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 10;
    }
    auto runtime = std::move(frozen).value();
    const auto runtime_player = resolve_tank(runtime, player);
    const auto runtime_ai = resolve_tank(runtime, ai);

    active_set active{runtime.topology()};
    std::size_t integration_calls = 0;
    std::size_t composition_calls = 0;
    rigid_policy policy{&integration_calls, &composition_calls};
    const player_input player_command{2.0, 0.25, -0.5, 0.125};
    const ai_goal ai_command{2.0, 0.25, -0.5, 0.125};
    const auto player_intent = player_intent_producer{}(player_command);
    const auto ai_intent = ai_intent_producer{}(ai_command);
    if (player_intent != ai_intent ||
        apply_intent(active, runtime_player, player_intent, policy) !=
            scene_polytree::motion::motion_error::none ||
        apply_intent(active, runtime_ai, ai_intent, policy) !=
            scene_polytree::motion::motion_error::none ||
        active.size() != 6) {
        return 11;
    }

    scene_polytree::motion::fixed_step_sequence sequence{std::chrono::milliseconds{100}};
    scene_polytree::motion::motion_evaluation_workspace<rigid_pose> motion_workspace;
    scene_polytree::transform_evaluation_workspace transform_workspace;
    const auto instances_match = [&] {
        return pose_approximately_equal(runtime.state().local(runtime_player.hull),
                                        runtime.state().local(runtime_ai.hull)) &&
               pose_approximately_equal(runtime.state().local(runtime_player.turret),
                                        runtime.state().local(runtime_ai.turret)) &&
               pose_approximately_equal(runtime.state().local(runtime_player.gun),
                                        runtime.state().local(runtime_ai.gun)) &&
               pose_approximately_equal(runtime.state().world(runtime_player.hull),
                                        runtime.state().world(runtime_ai.hull)) &&
               pose_approximately_equal(runtime.state().world(runtime_player.turret),
                                        runtime.state().world(runtime_ai.turret)) &&
               pose_approximately_equal(runtime.state().world(runtime_player.gun),
                                        runtime.state().world(runtime_ai.gun));
    };
    const auto advance_and_compare = [&](std::uint64_t expected_tick) {
        const auto result = scene_polytree::motion::advance_motion_scene(
            runtime.topology(), runtime.state(), active, sequence, motion_workspace,
            transform_workspace, policy, policy);
        return result && result.step.tick == expected_tick &&
               result.step.delta == std::chrono::milliseconds{100} &&
               result.integrated_nodes.size() == 6 && result.changed_nodes.size() == 6 &&
               instances_match();
    };
    if (!advance_and_compare(0) || !advance_and_compare(1) || !advance_and_compare(2) ||
        !approximately_equal(runtime.state().local(runtime_player.hull).translation.y, -2.4)) {
        return 12;
    }

    if (apply_intent(active, runtime_player, {}, policy) !=
            scene_polytree::motion::motion_error::none ||
        apply_intent(active, runtime_ai, {}, policy) !=
            scene_polytree::motion::motion_error::none ||
        !active.empty()) {
        return 13;
    }
    integration_calls = 0;
    composition_calls = 0;
    const auto stationary = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    if (!stationary || !stationary.integrated_nodes.empty() ||
        !stationary.changed_nodes.empty() || integration_calls != 0 || composition_calls != 0 ||
        sequence.next_tick() != 4) {
        return 14;
    }
    return 0;
}
} // namespace

int main() {
    if (!scene_polytree::examples::tank::run_demo()) {
        return 1;
    }
    const auto articulation = articulation_and_parent_motion();
    if (articulation != 0) {
        return articulation;
    }
    return equivalent_controller_replay();
}
