#include "tank_example.hpp"

#include <array>
#include <chrono>
#include <ranges>
#include <utility>

namespace {
using namespace scene_polytree::examples::tank;

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
    if (!articulated || !std::ranges::equal(articulated.integrated_nodes, expected_articulated) ||
        !std::ranges::equal(articulated.changed_nodes, expected_articulated) ||
        runtime.state().record(instance.hull).world_revision != hull_revision ||
        turret_local.translation != asset.turret_pivot.translation ||
        gun_local.translation != asset.gun_pivot.translation || turret_local.rotation.z == 0.0 ||
        gun_local.rotation.x == 0.0) {
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
} // namespace

int main() {
    if (!scene_polytree::examples::tank::run_demo()) {
        return 1;
    }
    return articulation_and_parent_motion();
}
