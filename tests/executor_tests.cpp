#include <scene_polytree/cpu_task_executor.hpp>
#include <scene_polytree/scene_polytree.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>

namespace {
struct translation {
    std::uint64_t value{};
    friend constexpr bool operator==(const translation &, const translation &) = default;
};

struct translation_policy {
    using transform_type = translation;

    [[nodiscard]] translation compose(const translation &parent,
                                      const translation &local) noexcept {
        return {parent.value + local.value};
    }
};

using authoring_scene =
    scene_polytree::basic_authoring_scene<std::uint32_t, std::uint32_t, translation>;

[[nodiscard]] bool records_equal(const auto &left, const auto &right) {
    const auto nodes = std::views::iota(std::size_t{}, left.size());
    return left.size() == right.size() && std::ranges::all_of(nodes, [&](std::size_t index) {
               const auto &a = left.record(static_cast<wz::core::graph::NodeHandle>(index));
               const auto &b = right.record(static_cast<wz::core::graph::NodeHandle>(index));
               return a.local == b.local && a.world == b.world &&
                      a.local_revision == b.local_revision &&
                      a.world_revision == b.world_revision && a.dirty == b.dirty;
           });
}

[[nodiscard]] int wide_parallel_parity() {
    constexpr std::uint32_t node_total = 5'000;
    authoring_scene authoring;
    const auto root = authoring.insert_root(0u, translation{1}).value();
    std::ranges::for_each(std::views::iota(1u, node_total), [&](std::uint32_t node) {
        (void)authoring.insert_child(root, node, node, translation{node});
    });
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 1;
    }
    auto runtime = std::move(frozen).value();
    auto parallel_state = runtime.state();
    scene_polytree::transform_evaluation_workspace sequential_workspace;
    scene_polytree::transform_evaluation_workspace parallel_workspace;
    const auto sequential_plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), sequential_workspace);
    const auto parallel_plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), parallel_state, parallel_workspace);
    translation_policy policy;
    const auto sequential = scene_polytree::evaluate_transforms(
        runtime.topology(), runtime.state(), sequential_plan.value(), policy);
    scene_polytree::cpu_task_executor executor{3};
    const auto parallel = scene_polytree::evaluate_transforms(
        runtime.topology(), parallel_state, parallel_plan.value(), policy, executor,
        {.minimum_task_grain = 512});
    const auto statistics = executor.last_statistics();
    if (!sequential || !parallel || !records_equal(runtime.state(), parallel_state) ||
        !std::ranges::equal(sequential.changed_nodes, parallel.changed_nodes) ||
        statistics.parallel_dispatch_count == 0 || statistics.task_count < 2) {
        return 2;
    }
    return 0;
}

[[nodiscard]] int chain_stays_sequential() {
    constexpr std::uint32_t node_total = 3'000;
    authoring_scene authoring;
    auto parent = authoring.insert_root(0u, translation{1}).value();
    std::ranges::for_each(std::views::iota(1u, node_total), [&](std::uint32_t node) {
        parent = authoring.insert_child(parent, node, node, translation{1}).value();
    });
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 3;
    }
    auto runtime = std::move(frozen).value();
    scene_polytree::transform_evaluation_workspace workspace;
    const auto plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), workspace);
    translation_policy policy;
    scene_polytree::cpu_task_executor executor{3};
    const auto evaluated = scene_polytree::evaluate_transforms(
        runtime.topology(), runtime.state(), plan.value(), policy, executor,
        {.minimum_task_grain = 128});
    if (!evaluated || executor.last_statistics().parallel_dispatch_count != 0 ||
        runtime.state().world(node_total - 1u) != translation{node_total}) {
        return 4;
    }
    return 0;
}

[[nodiscard]] int partial_parallel_parity() {
    constexpr std::uint32_t branch_width = 2'500;
    authoring_scene authoring;
    const auto root = authoring.insert_root(0u, translation{1}).value();
    const auto left = authoring.insert_child(root, 1u, 1u, translation{2}).value();
    const auto right = authoring.insert_child(root, 2u, 2u, translation{3}).value();
    std::ranges::for_each(std::views::iota(0u, branch_width), [&](std::uint32_t index) {
        (void)authoring.insert_child(left, index + 3u, index + 3u, translation{1});
        (void)authoring.insert_child(right, index + branch_width + 3u,
                                     index + branch_width + 3u, translation{1});
    });
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 5;
    }
    auto runtime = std::move(frozen).value();
    const auto left_node = runtime.identities().runtime_handle(left).value();
    const auto right_node = runtime.identities().runtime_handle(right).value();
    translation_policy policy;
    scene_polytree::transform_evaluation_workspace initialization_workspace;
    const auto initialization = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), initialization_workspace);
    if (!initialization ||
        !scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                             initialization.value(), policy)) {
        return 6;
    }

    auto sequential_state = runtime.state();
    auto parallel_state = runtime.state();
    if (sequential_state.mark_dirty(left_node) != scene_polytree::transform_error::none ||
        sequential_state.mark_dirty(right_node) != scene_polytree::transform_error::none ||
        parallel_state.mark_dirty(left_node) != scene_polytree::transform_error::none ||
        parallel_state.mark_dirty(right_node) != scene_polytree::transform_error::none) {
        return 7;
    }
    const std::array selected{left_node};
    scene_polytree::transform_evaluation_workspace sequential_workspace;
    scene_polytree::transform_evaluation_workspace parallel_workspace;
    const auto sequential_plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), sequential_state, sequential_workspace, selected);
    const auto parallel_plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), parallel_state, parallel_workspace, selected);
    const auto sequential = scene_polytree::evaluate_transforms(
        runtime.topology(), sequential_state, sequential_plan.value(), policy);
    scene_polytree::cpu_task_executor executor{3};
    const auto parallel = scene_polytree::evaluate_transforms(
        runtime.topology(), parallel_state, parallel_plan.value(), policy, executor,
        {.minimum_task_grain = 512});
    if (!sequential || !parallel || !records_equal(sequential_state, parallel_state) ||
        !std::ranges::equal(sequential.changed_nodes, parallel.changed_nodes) ||
        !parallel_state.record(right_node).dirty || parallel_state.record(left_node).dirty ||
        executor.last_statistics().parallel_dispatch_count == 0) {
        return 8;
    }
    return 0;
}
} // namespace

int main() {
    const auto wide = wide_parallel_parity();
    if (wide != 0) {
        return wide;
    }
    const auto partial = partial_parallel_parity();
    return partial == 0 ? chain_stays_sequential() : partial;
}
