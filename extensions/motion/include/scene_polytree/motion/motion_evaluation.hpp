#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <algo/next.h>
#include <graph/static_polytree.h>

#include <scene_polytree/motion/active_motion_set.hpp>
#include <scene_polytree/motion/fixed_step.hpp>
#include <scene_polytree/motion/motion_error.hpp>
#include <scene_polytree/motion/motion_policy.hpp>
#include <scene_polytree/transform_evaluation.hpp>

namespace scene_polytree::motion {
namespace detail {
template <class Transform> struct local_motion_update {
    wz::core::graph::NodeHandle node{wz::core::graph::INVALID_NODE};
    Transform local;
};

template <class T> struct motion_vector_sink {
    std::vector<T> &values;

    bool push(T value) {
        values.push_back(std::move(value));
        return true;
    }
};

struct motion_workspace_access;
} // namespace detail

template <class Transform> class motion_evaluation_workspace {
  public:
    [[nodiscard]] std::size_t scratch_capacity_bytes() const noexcept {
        return m_updates.capacity() * sizeof(detail::local_motion_update<Transform>) +
               m_integrated_nodes.capacity() * sizeof(wz::core::graph::NodeHandle);
    }

  private:
    friend struct detail::motion_workspace_access;

    std::vector<detail::local_motion_update<Transform>> m_updates;
    std::vector<wz::core::graph::NodeHandle> m_integrated_nodes;
};

struct motion_evaluation_result {
    motion_error error{motion_error::none};
    transform_error transform_status{transform_error::none};
    std::span<const wz::core::graph::NodeHandle> integrated_nodes;
    std::span<const wz::core::graph::NodeHandle> changed_nodes;
    fixed_motion_step step;
    scene_revision world_revision{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == motion_error::none && transform_status == transform_error::none;
    }
};

namespace detail {
struct motion_workspace_access {
    template <class Transform>
    [[nodiscard]] static auto &updates(motion_evaluation_workspace<Transform> &workspace) noexcept {
        return workspace.m_updates;
    }

    template <class Transform>
    [[nodiscard]] static auto &
    integrated_nodes(motion_evaluation_workspace<Transform> &workspace) noexcept {
        return workspace.m_integrated_nodes;
    }
};

template <class Transform> struct local_update_sink {
    transform_state<Transform> &state;
    std::vector<wz::core::graph::NodeHandle> &integrated_nodes;
    transform_error error{transform_error::none};

    bool push(const local_motion_update<Transform> &update) {
        error = state.set_local(update.node, update.local);
        if (error != transform_error::none) {
            return false;
        }
        integrated_nodes.push_back(update.node);
        return true;
    }
};

[[nodiscard]] inline motion_error map_transform_error(transform_error error) noexcept {
    switch (error) {
    case transform_error::none:
        return motion_error::none;
    case transform_error::state_size_mismatch:
        return motion_error::state_size_mismatch;
    case transform_error::topology_mismatch:
        return motion_error::topology_mismatch;
    case transform_error::revision_exhausted:
        return motion_error::revision_exhausted;
    default:
        return motion_error::transform_failure;
    }
}
} // namespace detail

template <class N, class E, class Transform, class LinearVelocity, class AngularVelocity,
          class MotionPolicyType, class TransformPolicyType>
    requires MotionPolicy<MotionPolicyType> && TransformPolicy<TransformPolicyType> &&
             std::same_as<typename MotionPolicyType::transform_type, Transform> &&
             std::same_as<typename MotionPolicyType::linear_velocity_type, LinearVelocity> &&
             std::same_as<typename MotionPolicyType::angular_velocity_type, AngularVelocity> &&
             std::same_as<typename TransformPolicyType::transform_type, Transform>
[[nodiscard]] motion_evaluation_result advance_motion_scene(
    const wz::core::graph::Polytree<N, E> &topology, transform_state<Transform> &state,
    const active_motion_set<LinearVelocity, AngularVelocity> &active, fixed_step_sequence &sequence,
    motion_evaluation_workspace<Transform> &motion_workspace,
    transform_evaluation_workspace &transform_workspace, MotionPolicyType &motion_policy,
    TransformPolicyType &transform_policy) {
    const auto step = sequence.next_step();
    if (step.delta.count() <= 0) {
        return {motion_error::invalid_step, transform_error::none, {}, {}, step, state.revision()};
    }
    if (step.tick == std::numeric_limits<std::uint64_t>::max()) {
        return {
            motion_error::tick_exhausted, transform_error::none, {}, {}, step, state.revision()};
    }
    if (active.topology_identity() != &topology) {
        return {
            motion_error::topology_mismatch, transform_error::none, {}, {}, step, state.revision()};
    }
    const auto node_total = wz::core::graph::node_count(topology);
    if (state.size() != node_total || active.node_capacity() != node_total) {
        return {motion_error::state_size_mismatch,
                transform_error::none,
                {},
                {},
                step,
                state.revision()};
    }
    const bool valid_nodes = std::ranges::all_of(
        active.records(), [&](const auto &record) { return record.node < node_total; });
    if (!valid_nodes) {
        return {motion_error::invalid_node, transform_error::none, {}, {}, step, state.revision()};
    }

    const bool existing_dirty =
        std::ranges::any_of(state.records(), [](const auto &record) { return record.dirty; });
    const bool needs_world_revision = existing_dirty || !active.empty();
    const auto remaining = std::numeric_limits<scene_revision>::max() - state.revision();
    if (active.size() > remaining ||
        (needs_world_revision && remaining - static_cast<scene_revision>(active.size()) == 0)) {
        return {motion_error::revision_exhausted,
                transform_error::none,
                {},
                {},
                step,
                state.revision()};
    }

    auto &updates = detail::motion_workspace_access::updates(motion_workspace);
    auto &integrated_nodes = detail::motion_workspace_access::integrated_nodes(motion_workspace);
    updates.clear();
    integrated_nodes.clear();
    updates.reserve(active.size());
    integrated_nodes.reserve(active.size());
    detail::motion_vector_sink<detail::local_motion_update<Transform>> update_sink{updates};
    const auto integration_status =
        wz::core::algo::next::transform(active.records(), update_sink, [&](const auto &record) {
            return detail::local_motion_update<Transform>{
                record.node, motion_policy.integrate(state.local(record.node), record.state, step)};
        });
    assert(integration_status == wz::core::algo::next::execution_status::completed);
    (void)integration_status;

    detail::local_update_sink<Transform> local_sink{state, integrated_nodes};
    const auto local_status = wz::core::algo::next::transform(
        updates, local_sink, [](const auto &update) -> const auto & { return update; });
    if (local_status != wz::core::algo::next::execution_status::completed) {
        return {detail::map_transform_error(local_sink.error),
                local_sink.error,
                integrated_nodes,
                {},
                step,
                state.revision()};
    }

    const auto plan = make_transform_evaluation_plan(topology, state, transform_workspace);
    if (!plan) {
        return {detail::map_transform_error(plan.error()),
                plan.error(),
                integrated_nodes,
                {},
                step,
                state.revision()};
    }
    const auto evaluated = evaluate_transforms(topology, state, plan.value(), transform_policy);
    if (!evaluated) {
        return {detail::map_transform_error(evaluated.error),
                evaluated.error,
                integrated_nodes,
                {},
                step,
                state.revision()};
    }

    detail::fixed_step_sequence_access::complete(sequence);
    return {motion_error::none,
            transform_error::none,
            integrated_nodes,
            evaluated.changed_nodes,
            step,
            evaluated.world_revision};
}
} // namespace scene_polytree::motion
