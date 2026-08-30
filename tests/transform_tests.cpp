#include <scene_polytree/scene_polytree.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace {
struct translation {
    int value{};

    friend constexpr bool operator==(const translation &, const translation &) = default;
};

struct translation_policy {
    using transform_type = translation;

    int *calls{};

    [[nodiscard]] translation compose(const translation &parent_world,
                                      const translation &local) noexcept {
        if (calls != nullptr) {
            ++*calls;
        }
        return {parent_world.value + local.value};
    }
};

static_assert(scene_polytree::TransformPolicy<translation_policy>);

using authoring_scene =
    scene_polytree::basic_authoring_scene<std::uint32_t, std::uint32_t, translation>;

int runtime_dirty_propagation() {
    authoring_scene authoring;
    const auto root_a = authoring.insert_root(10u, translation{10}).value();
    const auto child = authoring.insert_child(root_a, 20u, 1u, translation{2}).value();
    const auto grandchild = authoring.insert_child(child, 30u, 2u, translation{3}).value();
    const auto clean_sibling = authoring.insert_child(root_a, 40u, 3u, translation{20}).value();
    const auto root_b = authoring.insert_root(50u, translation{100}).value();

    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 1;
    }
    auto runtime = std::move(frozen).value();
    const auto runtime_root_a = runtime.identities().runtime_handle(root_a).value();
    const auto runtime_child = runtime.identities().runtime_handle(child).value();
    const auto runtime_grandchild = runtime.identities().runtime_handle(grandchild).value();
    const auto runtime_clean_sibling = runtime.identities().runtime_handle(clean_sibling).value();
    const auto runtime_root_b = runtime.identities().runtime_handle(root_b).value();

    scene_polytree::transform_evaluation_workspace workspace;
    auto initial_plan = scene_polytree::make_transform_evaluation_plan(runtime.topology(),
                                                                       runtime.state(), workspace);
    if (!initial_plan ||
        !std::ranges::equal(
            initial_plan.value().ordered_nodes,
            wz::core::graph::evaluation_plan(runtime.topology()).topological_order)) {
        return 2;
    }

    int calls = 0;
    translation_policy policy{&calls};
    const auto initial = scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                                             initial_plan.value(), policy);
    if (!initial || calls != 3 || runtime.state().world(runtime_root_a) != translation{10} ||
        runtime.state().world(runtime_child) != translation{12} ||
        runtime.state().world(runtime_grandchild) != translation{15} ||
        runtime.state().world(runtime_clean_sibling) != translation{30} ||
        runtime.state().world(runtime_root_b) != translation{100}) {
        return 3;
    }

    const auto unchanged_revision = runtime.state().record(runtime_clean_sibling).world_revision;
    const auto change_token = initial.world_revision;
    if (runtime.set_local(runtime_child, translation{5}) != scene_polytree::transform_error::none) {
        return 4;
    }

    auto changed_plan = scene_polytree::make_transform_evaluation_plan(runtime.topology(),
                                                                       runtime.state(), workspace);
    const std::array expected_changed{runtime_child, runtime_grandchild};
    if (!changed_plan ||
        !std::ranges::equal(changed_plan.value().dirty_roots, std::array{runtime_child}) ||
        !std::ranges::equal(changed_plan.value().ordered_nodes, expected_changed)) {
        return 5;
    }

    calls = 0;
    const auto changed = scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                                             changed_plan.value(), policy);
    if (!changed || calls != 2 || !std::ranges::equal(changed.changed_nodes, expected_changed) ||
        runtime.state().world(runtime_child) != translation{15} ||
        runtime.state().world(runtime_grandchild) != translation{18} ||
        runtime.state().record(runtime_clean_sibling).world_revision != unchanged_revision) {
        return 6;
    }

    std::vector<wz::core::graph::NodeHandle> change_scratch;
    const auto changes_since = scene_polytree::changed_transform_nodes_since(
        runtime.topology(), runtime.state(), change_token, change_scratch);
    if (!std::ranges::equal(changes_since, expected_changed)) {
        return 7;
    }

    auto clean_plan = scene_polytree::make_transform_evaluation_plan(runtime.topology(),
                                                                     runtime.state(), workspace);
    calls = 0;
    const auto clean = scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                                           clean_plan.value(), policy);
    if (!clean || !clean.changed_nodes.empty() || calls != 0) {
        return 8;
    }
    return 0;
}

int partial_evaluation_and_stale_plans() {
    authoring_scene authoring;
    const auto first_root = authoring.insert_root(1u, translation{10}).value();
    const auto first_child = authoring.insert_child(first_root, 2u, 1u, translation{1}).value();
    const auto second_root = authoring.insert_root(3u, translation{20}).value();
    const auto second_child = authoring.insert_child(second_root, 4u, 2u, translation{2}).value();

    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 20;
    }
    auto runtime = std::move(frozen).value();
    const auto first_root_runtime = runtime.identities().runtime_handle(first_root).value();
    const auto first = runtime.identities().runtime_handle(first_child).value();
    const auto second = runtime.identities().runtime_handle(second_child).value();
    scene_polytree::transform_evaluation_workspace workspace;
    translation_policy policy;
    auto initial_plan = scene_polytree::make_transform_evaluation_plan(runtime.topology(),
                                                                       runtime.state(), workspace);
    if (!scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                             initial_plan.value(), policy)) {
        return 21;
    }

    (void)runtime.set_local(first, translation{5});
    (void)runtime.set_local(second, translation{7});
    auto all_dirty_plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), workspace);
    if (!all_dirty_plan || all_dirty_plan.value().dirty_roots.size() != 2) {
        return 22;
    }
    const auto borrowed_scope = all_dirty_plan.value().dirty_roots.first(1);
    const auto selected = borrowed_scope.front();
    const auto pending = selected == first ? second : first;
    auto first_plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), workspace, borrowed_scope);
    if (!first_plan ||
        !std::ranges::equal(first_plan.value().ordered_nodes, std::array{selected})) {
        return 23;
    }

    const auto first_result = scene_polytree::evaluate_transforms(
        runtime.topology(), runtime.state(), first_plan.value(), policy);
    if (!first_result || runtime.state().record(pending).dirty == false) {
        return 24;
    }

    auto second_plan = scene_polytree::make_transform_evaluation_plan(runtime.topology(),
                                                                      runtime.state(), workspace);
    if (!second_plan || !std::ranges::equal(second_plan.value().dirty_roots, std::array{pending})) {
        return 25;
    }

    (void)runtime.set_local(selected, translation{8});
    const auto stale = scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                                           second_plan.value(), policy);
    if (stale.error != scene_polytree::transform_error::stale_plan) {
        return 26;
    }

    auto current = scene_polytree::make_transform_evaluation_plan(runtime.topology(),
                                                                  runtime.state(), workspace);
    if (!current || !scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                                         current.value(), policy)) {
        return 27;
    }

    (void)runtime.set_local(first_root_runtime, translation{12});
    (void)runtime.set_local(first, translation{9});
    const std::array first_scope{first};
    const auto invalid_nested_scope = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), workspace,
        std::span<const wz::core::graph::NodeHandle>{first_scope});
    if (invalid_nested_scope ||
        invalid_nested_scope.error() != scene_polytree::transform_error::invalid_scope) {
        return 28;
    }
    const std::array duplicate_scope{first_root_runtime, first_root_runtime};
    const auto invalid_duplicate_scope = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), workspace,
        std::span<const wz::core::graph::NodeHandle>{duplicate_scope});
    if (invalid_duplicate_scope ||
        invalid_duplicate_scope.error() != scene_polytree::transform_error::invalid_scope) {
        return 29;
    }
    return 0;
}

int reparent_and_freeze_remapping() {
    authoring_scene authoring;
    const auto first_root = authoring.insert_root(1u, translation{10}).value();
    const auto moved = authoring.insert_child(first_root, 2u, 1u, translation{5}).value();
    const auto descendant = authoring.insert_child(moved, 3u, 2u, translation{2}).value();
    const auto second_root = authoring.insert_root(4u, translation{100}).value();
    const auto stationary = authoring.insert_child(second_root, 5u, 3u, translation{20}).value();

    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto first_freeze = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!first_freeze) {
        return 40;
    }
    auto first_runtime = std::move(first_freeze).value();
    scene_polytree::transform_evaluation_workspace workspace;
    translation_policy policy;
    auto first_plan = scene_polytree::make_transform_evaluation_plan(
        first_runtime.topology(), first_runtime.state(), workspace);
    if (!scene_polytree::evaluate_transforms(first_runtime.topology(), first_runtime.state(),
                                             first_plan.value(), policy)) {
        return 41;
    }

    const auto old_stationary = first_runtime.identities().runtime_handle(stationary).value();
    const auto stationary_revision = first_runtime.state().record(old_stationary).world_revision;
    if (!authoring.reparent(moved, second_root, 10u)) {
        return 42;
    }

    auto second_freeze = scene_polytree::freeze_scene(authoring, freeze_workspace, first_runtime);
    if (!second_freeze) {
        return 43;
    }
    auto second_runtime = std::move(second_freeze).value();
    const auto runtime_moved = second_runtime.identities().runtime_handle(moved).value();
    const auto runtime_descendant = second_runtime.identities().runtime_handle(descendant).value();
    const auto runtime_stationary = second_runtime.identities().runtime_handle(stationary).value();
    auto moved_plan = scene_polytree::make_transform_evaluation_plan(
        second_runtime.topology(), second_runtime.state(), workspace);
    const std::array moved_subtree{runtime_moved, runtime_descendant};
    if (!moved_plan || !std::ranges::equal(moved_plan.value().ordered_nodes, moved_subtree)) {
        return 44;
    }

    const auto moved_result = scene_polytree::evaluate_transforms(
        second_runtime.topology(), second_runtime.state(), moved_plan.value(), policy);
    if (!moved_result || second_runtime.state().world(runtime_moved) != translation{105} ||
        second_runtime.state().world(runtime_descendant) != translation{107} ||
        second_runtime.state().record(runtime_stationary).world_revision != stationary_revision) {
        return 45;
    }

    if (!authoring.reparent(moved, second_root, 11u, 0u)) {
        return 46;
    }
    auto reordered_freeze =
        scene_polytree::freeze_scene(authoring, freeze_workspace, second_runtime);
    if (!reordered_freeze) {
        return 47;
    }
    auto reordered_runtime = std::move(reordered_freeze).value();
    auto reordered_plan = scene_polytree::make_transform_evaluation_plan(
        reordered_runtime.topology(), reordered_runtime.state(), workspace);
    if (!reordered_plan || !reordered_plan.value().ordered_nodes.empty()) {
        return 48;
    }

    const auto reordered_moved = reordered_runtime.identities().runtime_handle(moved).value();
    const auto reordered_descendant =
        reordered_runtime.identities().runtime_handle(descendant).value();
    (void)reordered_runtime.set_local(reordered_moved, translation{999});
    auto authoritative_freeze =
        scene_polytree::freeze_scene(authoring, freeze_workspace, reordered_runtime);
    if (!authoritative_freeze) {
        return 49;
    }
    auto authoritative_runtime = std::move(authoritative_freeze).value();
    auto authoritative_plan = scene_polytree::make_transform_evaluation_plan(
        authoritative_runtime.topology(), authoritative_runtime.state(), workspace);
    const std::array authoritative_subtree{reordered_moved, reordered_descendant};
    if (!authoritative_plan ||
        !std::ranges::equal(authoritative_plan.value().ordered_nodes, authoritative_subtree) ||
        authoritative_runtime.state().local(reordered_moved) != translation{5} ||
        !scene_polytree::evaluate_transforms(authoritative_runtime.topology(),
                                             authoritative_runtime.state(),
                                             authoritative_plan.value(), policy) ||
        authoritative_runtime.state().world(reordered_moved) != translation{105} ||
        authoritative_runtime.state().world(reordered_descendant) != translation{107}) {
        return 50;
    }

    if (!authoring.detach_to_root(moved, 0u)) {
        return 51;
    }
    auto detached_freeze =
        scene_polytree::freeze_scene(authoring, freeze_workspace, authoritative_runtime);
    if (!detached_freeze) {
        return 52;
    }
    auto detached_runtime = std::move(detached_freeze).value();
    auto detached_plan = scene_polytree::make_transform_evaluation_plan(
        detached_runtime.topology(), detached_runtime.state(), workspace);
    const auto detached_moved = detached_runtime.identities().runtime_handle(moved).value();
    if (!scene_polytree::evaluate_transforms(detached_runtime.topology(), detached_runtime.state(),
                                             detached_plan.value(), policy) ||
        detached_runtime.state().world(detached_moved) != translation{5}) {
        return 53;
    }
    return 0;
}

int deep_and_wide_topologies() {
    constexpr std::uint32_t deep_count = 10'000;
    authoring_scene deep_authoring;
    auto current = deep_authoring.insert_root(0u, translation{1}).value();
    std::vector<wz::core::graph::StableNodeId> deep_ids{current};
    deep_ids.reserve(deep_count);
    std::ranges::for_each(std::views::iota(1u, deep_count), [&](std::uint32_t value) {
        current = deep_authoring.insert_child(current, value, value, translation{1}).value();
        deep_ids.push_back(current);
    });

    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto deep_freeze = scene_polytree::freeze_scene(deep_authoring, freeze_workspace);
    if (!deep_freeze) {
        return 60;
    }
    auto deep_runtime = std::move(deep_freeze).value();
    scene_polytree::transform_evaluation_workspace workspace;
    translation_policy policy;
    auto initial = scene_polytree::make_transform_evaluation_plan(deep_runtime.topology(),
                                                                  deep_runtime.state(), workspace);
    if (!scene_polytree::evaluate_transforms(deep_runtime.topology(), deep_runtime.state(),
                                             initial.value(), policy)) {
        return 61;
    }

    const auto midpoint =
        deep_runtime.identities().runtime_handle(deep_ids[deep_count / 2]).value();
    (void)deep_runtime.set_local(midpoint, translation{2});
    auto suffix = scene_polytree::make_transform_evaluation_plan(deep_runtime.topology(),
                                                                 deep_runtime.state(), workspace);
    if (!suffix || suffix.value().ordered_nodes.size() != deep_count / 2 ||
        suffix.value().ordered_nodes.front() != midpoint) {
        return 62;
    }
    int deep_calls = 0;
    translation_policy counting_policy{&deep_calls};
    if (!scene_polytree::evaluate_transforms(deep_runtime.topology(), deep_runtime.state(),
                                             suffix.value(), counting_policy) ||
        deep_calls != static_cast<int>(deep_count / 2)) {
        return 63;
    }

    constexpr std::uint32_t wide_count = 1'000;
    authoring_scene wide_authoring;
    const auto wide_root = wide_authoring.insert_root(0u, translation{1}).value();
    std::vector<wz::core::graph::StableNodeId> wide_ids;
    wide_ids.reserve(wide_count);
    std::ranges::for_each(std::views::iota(0u, wide_count), [&](std::uint32_t value) {
        wide_ids.push_back(
            wide_authoring.insert_child(wide_root, value + 1u, value, translation{1}).value());
    });
    auto wide_freeze = scene_polytree::freeze_scene(wide_authoring, freeze_workspace);
    if (!wide_freeze) {
        return 64;
    }
    auto wide_runtime = std::move(wide_freeze).value();
    auto wide_initial = scene_polytree::make_transform_evaluation_plan(
        wide_runtime.topology(), wide_runtime.state(), workspace);
    (void)scene_polytree::evaluate_transforms(wide_runtime.topology(), wide_runtime.state(),
                                              wide_initial.value(), policy);
    const auto wide_leaf =
        wide_runtime.identities().runtime_handle(wide_ids[wide_count / 2]).value();
    (void)wide_runtime.set_local(wide_leaf, translation{2});
    auto leaf_plan = scene_polytree::make_transform_evaluation_plan(
        wide_runtime.topology(), wide_runtime.state(), workspace);
    if (!leaf_plan || !std::ranges::equal(leaf_plan.value().ordered_nodes, std::array{wide_leaf})) {
        return 65;
    }

    const auto wide_runtime_root = wide_runtime.identities().runtime_handle(wide_root).value();
    (void)wide_runtime.set_local(wide_runtime_root, translation{2});
    auto root_plan = scene_polytree::make_transform_evaluation_plan(
        wide_runtime.topology(), wide_runtime.state(), workspace);
    if (!root_plan || root_plan.value().dirty_roots.size() != 1 ||
        root_plan.value().dirty_roots.front() != wide_runtime_root ||
        root_plan.value().ordered_nodes.size() != wide_count + 1u) {
        return 66;
    }
    return 0;
}
} // namespace

int main() {
    const auto first = runtime_dirty_propagation();
    if (first != 0) {
        return first;
    }
    const auto second = partial_evaluation_and_stale_plans();
    if (second != 0) {
        return second;
    }
    const auto third = reparent_and_freeze_remapping();
    if (third != 0) {
        return third;
    }
    return deep_and_wide_topologies();
}
