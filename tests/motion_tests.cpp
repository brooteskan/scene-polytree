#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_polytree.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

namespace {
struct translation {
    int value{};

    friend constexpr bool operator==(const translation &, const translation &) = default;
};

struct scalar_policy {
    using transform_type = translation;
    using linear_velocity_type = int;
    using angular_velocity_type = int;

    int *integration_calls{};
    int *composition_calls{};

    [[nodiscard]] translation compose(const translation &parent_world,
                                      const translation &local) noexcept {
        if (composition_calls != nullptr) {
            ++*composition_calls;
        }
        return {parent_world.value + local.value};
    }

    [[nodiscard]] translation integrate(const translation &local,
                                        const scene_polytree::motion::motion_state<int, int> &state,
                                        scene_polytree::motion::fixed_motion_step step) noexcept {
        if (integration_calls != nullptr) {
            ++*integration_calls;
        }
        const auto seconds =
            static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(step.delta).count());
        return {local.value + (state.linear_velocity + state.angular_velocity) * seconds};
    }

    [[nodiscard]] bool
    is_stationary(const scene_polytree::motion::motion_state<int, int> &state) noexcept {
        return state.linear_velocity == 0 && state.angular_velocity == 0;
    }
};

static_assert(scene_polytree::TransformPolicy<scalar_policy>);
static_assert(scene_polytree::motion::MotionPolicy<scalar_policy>);

using authoring_scene =
    scene_polytree::basic_authoring_scene<std::uint32_t, std::uint32_t, translation>;
using active_set = scene_polytree::motion::active_motion_set<int, int>;

int registration_and_ordering() {
    authoring_scene authoring;
    const auto first = authoring.insert_root(1u, translation{}).value();
    const auto second = authoring.insert_root(2u, translation{}).value();
    const auto third = authoring.insert_root(3u, translation{}).value();
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 1;
    }
    auto runtime = std::move(frozen).value();
    const auto first_runtime = runtime.identities().runtime_handle(first).value();
    const auto second_runtime = runtime.identities().runtime_handle(second).value();
    const auto third_runtime = runtime.identities().runtime_handle(third).value();
    active_set active{runtime.topology()};
    scalar_policy policy;

    if (active.set(third_runtime, {3, 0}, policy) != scene_polytree::motion::motion_error::none ||
        active.set(first_runtime, {1, 0}, policy) != scene_polytree::motion::motion_error::none ||
        active.set(second_runtime, {2, 0}, policy) != scene_polytree::motion::motion_error::none) {
        return 2;
    }
    const std::array expected{first_runtime, second_runtime, third_runtime};
    const auto handles =
        active.records() | std::views::transform([](const auto &record) { return record.node; });
    if (!std::ranges::equal(handles, expected)) {
        return 3;
    }

    if (active.set(second_runtime, {7, 0}, policy) != scene_polytree::motion::motion_error::none ||
        active.size() != 3 || active.records()[1].state.linear_velocity != 7 ||
        active.set(second_runtime, {}, policy) != scene_polytree::motion::motion_error::none ||
        active.size() != 2) {
        return 4;
    }

    using update = scene_polytree::motion::motion_update<int, int>;
    const std::array updates{update{first_runtime, {}}, update{second_runtime, {4, 1}}};
    if (active.apply_updates(updates, policy) != scene_polytree::motion::motion_error::none ||
        active.size() != 2 || active.records().front().node != second_runtime ||
        active.set(wz::core::graph::INVALID_NODE, {1, 0}, policy) !=
            scene_polytree::motion::motion_error::invalid_node) {
        return 5;
    }

    const auto generation = active.mutation_generation();
    const std::array invalid_updates{update{third_runtime, {9, 0}},
                                     update{wz::core::graph::INVALID_NODE, {1, 0}}};
    if (active.apply_updates(invalid_updates, policy) !=
            scene_polytree::motion::motion_error::invalid_node ||
        active.mutation_generation() != generation || active.size() != 2) {
        return 6;
    }
    return 0;
}

int centralized_integration() {
    authoring_scene authoring;
    const auto root = authoring.insert_root(1u, translation{10}).value();
    const auto moving = authoring.insert_child(root, 2u, 1u, translation{2}).value();
    const auto descendant = authoring.insert_child(moving, 3u, 2u, translation{3}).value();
    const auto sibling = authoring.insert_child(root, 4u, 3u, translation{20}).value();
    const auto other_root = authoring.insert_root(5u, translation{100}).value();
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 10;
    }
    auto runtime = std::move(frozen).value();
    const auto runtime_moving = runtime.identities().runtime_handle(moving).value();
    const auto runtime_descendant = runtime.identities().runtime_handle(descendant).value();
    const auto runtime_sibling = runtime.identities().runtime_handle(sibling).value();
    const auto runtime_other_root = runtime.identities().runtime_handle(other_root).value();

    active_set active{runtime.topology()};
    scene_polytree::motion::fixed_step_sequence sequence{std::chrono::seconds{1}};
    scene_polytree::motion::motion_evaluation_workspace<translation> motion_workspace;
    scene_polytree::transform_evaluation_workspace transform_workspace;
    int integration_calls = 0;
    int composition_calls = 0;
    scalar_policy policy{&integration_calls, &composition_calls};

    const auto initial = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    if (!initial || !initial.integrated_nodes.empty() || initial.changed_nodes.size() != 5 ||
        sequence.next_tick() != 1) {
        return 11;
    }
    const auto sibling_revision = runtime.state().record(runtime_sibling).world_revision;
    const auto other_revision = runtime.state().record(runtime_other_root).world_revision;

    if (active.set(runtime_moving, {5, 1}, policy) != scene_polytree::motion::motion_error::none) {
        return 12;
    }
    integration_calls = 0;
    composition_calls = 0;
    const auto moved = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    const std::array expected_changed{runtime_moving, runtime_descendant};
    if (!moved || integration_calls != 1 || composition_calls != 2 ||
        !std::ranges::equal(moved.integrated_nodes, std::array{runtime_moving}) ||
        !std::ranges::equal(moved.changed_nodes, expected_changed) ||
        runtime.state().local(runtime_moving) != translation{8} ||
        runtime.state().world(runtime_moving) != translation{18} ||
        runtime.state().world(runtime_descendant) != translation{21} ||
        runtime.state().record(runtime_sibling).world_revision != sibling_revision ||
        runtime.state().record(runtime_other_root).world_revision != other_revision) {
        return 13;
    }

    if (active.set(runtime_moving, {}, policy) != scene_polytree::motion::motion_error::none ||
        !active.empty()) {
        return 14;
    }
    integration_calls = 0;
    composition_calls = 0;
    const auto stationary = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    if (!stationary || !stationary.integrated_nodes.empty() || !stationary.changed_nodes.empty() ||
        integration_calls != 0 || composition_calls != 0 || sequence.next_tick() != 3) {
        return 15;
    }
    return 0;
}

int validation_is_non_mutating() {
    authoring_scene authoring;
    const auto root = authoring.insert_root(1u, translation{4}).value();
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto first_frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    auto second_frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!first_frozen || !second_frozen) {
        return 20;
    }
    auto first = std::move(first_frozen).value();
    auto second = std::move(second_frozen).value();
    const auto runtime_root = first.identities().runtime_handle(root).value();
    active_set active{first.topology()};
    scalar_policy policy;
    if (active.set(runtime_root, {1, 0}, policy) != scene_polytree::motion::motion_error::none) {
        return 21;
    }
    scene_polytree::motion::motion_evaluation_workspace<translation> motion_workspace;
    scene_polytree::transform_evaluation_workspace transform_workspace;

    scene_polytree::motion::fixed_step_sequence invalid_sequence{std::chrono::nanoseconds{0}};
    const auto invalid_step = scene_polytree::motion::advance_motion_scene(
        first.topology(), first.state(), active, invalid_sequence, motion_workspace,
        transform_workspace, policy, policy);
    if (invalid_step || invalid_step.error != scene_polytree::motion::motion_error::invalid_step ||
        first.state().local(runtime_root) != translation{4} || invalid_sequence.next_tick() != 0) {
        return 22;
    }

    scene_polytree::motion::fixed_step_sequence sequence{std::chrono::seconds{1}};
    const auto mismatch = scene_polytree::motion::advance_motion_scene(
        second.topology(), second.state(), active, sequence, motion_workspace, transform_workspace,
        policy, policy);
    if (mismatch || mismatch.error != scene_polytree::motion::motion_error::topology_mismatch ||
        sequence.next_tick() != 0) {
        return 23;
    }

    std::vector<scene_polytree::transform_record<translation>> exhausted_records{
        first.state().records().begin(), first.state().records().end()};
    scene_polytree::transform_state<translation> exhausted{
        std::move(exhausted_records),
        std::numeric_limits<scene_polytree::scene_revision>::max() - 1};
    const auto exhausted_result = scene_polytree::motion::advance_motion_scene(
        first.topology(), exhausted, active, sequence, motion_workspace, transform_workspace,
        policy, policy);
    if (exhausted_result ||
        exhausted_result.error != scene_polytree::motion::motion_error::revision_exhausted ||
        exhausted.local(runtime_root) != translation{4} || sequence.next_tick() != 0) {
        return 24;
    }

    scene_polytree::motion::fixed_step_sequence exhausted_sequence{
        std::chrono::seconds{1}, std::numeric_limits<std::uint64_t>::max()};
    const auto exhausted_tick = scene_polytree::motion::advance_motion_scene(
        first.topology(), first.state(), active, exhausted_sequence, motion_workspace,
        transform_workspace, policy, policy);
    if (exhausted_tick ||
        exhausted_tick.error != scene_polytree::motion::motion_error::tick_exhausted ||
        first.state().local(runtime_root) != translation{4} ||
        exhausted_sequence.next_tick() != std::numeric_limits<std::uint64_t>::max()) {
        return 25;
    }
    return 0;
}
} // namespace

int main() {
    const auto registration = registration_and_ordering();
    if (registration != 0) {
        return registration;
    }
    const auto integration = centralized_integration();
    if (integration != 0) {
        return integration;
    }
    return validation_is_non_mutating();
}
