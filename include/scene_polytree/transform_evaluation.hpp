#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <algo/next.h>
#include <graph/static_polytree.h>

#include <scene_polytree/transform_policy.hpp>
#include <scene_polytree/transform_state.hpp>

namespace scene_polytree {
namespace detail {
struct transform_workspace_access;
}

class transform_evaluation_workspace {
  public:
    [[nodiscard]] std::size_t scratch_capacity_bytes() const noexcept {
        return m_affected.capacity() * sizeof(std::uint8_t) +
               m_owner.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_selected.capacity() * sizeof(std::uint8_t) +
               m_dirty_roots.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_affected_nodes.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_selected_dirty_roots.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_selected_nodes.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_requested_roots.capacity() * sizeof(wz::core::graph::NodeHandle);
    }

  private:
    friend struct detail::transform_workspace_access;

    void prepare(std::size_t node_total) {
        ++m_generation;
        m_affected.assign(node_total, std::uint8_t{});
        m_owner.assign(node_total, wz::core::graph::INVALID_NODE);
        m_selected.assign(node_total, std::uint8_t{});
        m_dirty_roots.clear();
        m_affected_nodes.clear();
        m_selected_dirty_roots.clear();
        m_selected_nodes.clear();
        m_dirty_roots.reserve(node_total);
        m_affected_nodes.reserve(node_total);
        m_selected_dirty_roots.reserve(node_total);
        m_selected_nodes.reserve(node_total);
    }

    std::vector<std::uint8_t> m_affected;
    std::vector<wz::core::graph::NodeHandle> m_owner;
    std::vector<std::uint8_t> m_selected;
    std::vector<wz::core::graph::NodeHandle> m_dirty_roots;
    std::vector<wz::core::graph::NodeHandle> m_affected_nodes;
    std::vector<wz::core::graph::NodeHandle> m_selected_dirty_roots;
    std::vector<wz::core::graph::NodeHandle> m_selected_nodes;
    std::vector<wz::core::graph::NodeHandle> m_requested_roots;
    std::uint64_t m_generation{};
};

namespace detail {
struct transform_workspace_access {
    static void prepare(transform_evaluation_workspace &workspace, std::size_t node_total) {
        workspace.prepare(node_total);
    }

    [[nodiscard]] static auto &affected(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_affected;
    }

    [[nodiscard]] static auto &owner(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_owner;
    }

    [[nodiscard]] static auto &selected(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_selected;
    }

    [[nodiscard]] static auto &dirty_roots(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_dirty_roots;
    }

    [[nodiscard]] static auto &affected_nodes(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_affected_nodes;
    }

    [[nodiscard]] static auto &
    selected_dirty_roots(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_selected_dirty_roots;
    }

    [[nodiscard]] static auto &selected_nodes(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_selected_nodes;
    }

    [[nodiscard]] static auto &requested_roots(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_requested_roots;
    }

    [[nodiscard]] static std::uint64_t
    generation(const transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_generation;
    }
};
} // namespace detail

struct transform_evaluation_plan {
    std::span<const wz::core::graph::NodeHandle> dirty_roots;
    std::span<const wz::core::graph::NodeHandle> ordered_nodes;
    scene_revision source_revision{};
    std::uint64_t mutation_generation{};
    std::uint64_t evaluation_generation{};
    const void *topology_identity{};
    const transform_evaluation_workspace *workspace{};
    std::uint64_t workspace_generation{};
};

class transform_plan_outcome {
  public:
    [[nodiscard]] static transform_plan_outcome success(transform_evaluation_plan plan) noexcept {
        return transform_plan_outcome(plan, transform_error::none);
    }

    [[nodiscard]] static transform_plan_outcome failure(transform_error error) noexcept {
        return transform_plan_outcome({}, error);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return m_error == transform_error::none;
    }

    [[nodiscard]] const transform_evaluation_plan &value() const noexcept { return m_plan; }

    [[nodiscard]] transform_error error() const noexcept { return m_error; }

  private:
    transform_plan_outcome(transform_evaluation_plan plan, transform_error error) noexcept
        : m_plan(plan), m_error(error) {}

    transform_evaluation_plan m_plan;
    transform_error m_error{transform_error::none};
};

struct transform_evaluation_result {
    transform_error error{transform_error::none};
    std::span<const wz::core::graph::NodeHandle> changed_nodes;
    scene_revision world_revision{};

    [[nodiscard]] explicit operator bool() const noexcept { return error == transform_error::none; }
};

namespace detail {
template <class T> struct vector_sink {
    std::vector<T> &values;

    bool push(T value) {
        values.push_back(std::move(value));
        return true;
    }
};

template <class N, class E, class Transform> struct planning_sink {
    const wz::core::graph::Polytree<N, E> &topology;
    const transform_state<Transform> &state;
    transform_evaluation_workspace &workspace;

    bool push(wz::core::graph::NodeHandle node) {
        auto &affected_nodes = transform_workspace_access::affected_nodes(workspace);
        auto &affected_flags = transform_workspace_access::affected(workspace);
        auto &owners = transform_workspace_access::owner(workspace);
        auto &dirty_roots = transform_workspace_access::dirty_roots(workspace);
        const auto parent_node = parent(topology, node);
        const bool parent_affected =
            parent_node != wz::core::graph::INVALID_NODE && affected_flags[parent_node] != 0;
        const bool affected = state.record(node).dirty || parent_affected;
        affected_flags[node] = affected ? 1u : 0u;
        if (!affected) {
            return true;
        }

        const auto owner = parent_affected ? owners[parent_node] : node;
        owners[node] = owner;
        affected_nodes.push_back(node);
        if (!parent_affected) {
            dirty_roots.push_back(node);
        }
        return true;
    }
};

template <class N, class E, class Transform>
transform_plan_outcome
make_plan(const wz::core::graph::Polytree<N, E> &topology, const transform_state<Transform> &state,
          transform_evaluation_workspace &workspace,
          std::span<const wz::core::graph::NodeHandle> selected_roots, bool partial) {
    if (state.size() != wz::core::graph::node_count(topology)) {
        return transform_plan_outcome::failure(transform_error::state_size_mismatch);
    }

    auto &requested_roots = transform_workspace_access::requested_roots(workspace);
    if (partial) {
        requested_roots.assign(selected_roots.begin(), selected_roots.end());
    } else {
        requested_roots.clear();
    }

    const auto generic_plan = wz::core::graph::evaluation_plan(topology);
    transform_workspace_access::prepare(workspace, generic_plan.node_count());
    selected_roots = requested_roots;
    planning_sink<N, E, Transform> sink{topology, state, workspace};
    const auto status =
        wz::core::algo::next::transform(generic_plan.topological_order, sink,
                                        [](wz::core::graph::NodeHandle node) { return node; });
    assert(status == wz::core::algo::next::execution_status::completed);
    (void)status;

    auto &dirty_roots = transform_workspace_access::dirty_roots(workspace);
    auto &affected_nodes = transform_workspace_access::affected_nodes(workspace);
    auto &selected = transform_workspace_access::selected(workspace);
    auto &owners = transform_workspace_access::owner(workspace);
    auto &selected_dirty_roots = transform_workspace_access::selected_dirty_roots(workspace);
    auto &selected_nodes = transform_workspace_access::selected_nodes(workspace);
    auto plan_roots = std::span<const wz::core::graph::NodeHandle>{dirty_roots};
    auto plan_nodes = std::span<const wz::core::graph::NodeHandle>{affected_nodes};

    if (partial) {
        const bool valid_scope =
            std::ranges::all_of(selected_roots, [&](wz::core::graph::NodeHandle root) {
                if (root >= selected.size() || selected[root] != 0 ||
                    transform_workspace_access::affected(workspace)[root] == 0 ||
                    owners[root] != root) {
                    return false;
                }
                selected[root] = 1u;
                return true;
            });
        if (!valid_scope) {
            return transform_plan_outcome::failure(transform_error::invalid_scope);
        }

        vector_sink<wz::core::graph::NodeHandle> root_sink{selected_dirty_roots};
        const auto root_status = wz::core::algo::next::filter(
            dirty_roots, root_sink,
            [&](wz::core::graph::NodeHandle root) { return selected[root] != 0; });
        assert(root_status == wz::core::algo::next::execution_status::completed);
        (void)root_status;

        vector_sink<wz::core::graph::NodeHandle> node_sink{selected_nodes};
        const auto node_status = wz::core::algo::next::filter(
            affected_nodes, node_sink,
            [&](wz::core::graph::NodeHandle node) { return selected[owners[node]] != 0; });
        assert(node_status == wz::core::algo::next::execution_status::completed);
        (void)node_status;

        plan_roots = selected_dirty_roots;
        plan_nodes = selected_nodes;
    }

    return transform_plan_outcome::success({
        plan_roots,
        plan_nodes,
        state.revision(),
        state.mutation_generation(),
        state.evaluation_generation(),
        &topology,
        &workspace,
        transform_workspace_access::generation(workspace),
    });
}

template <class N, class E, class Transform, class Policy>
    requires TransformPolicy<Policy> && std::same_as<typename Policy::transform_type, Transform>
struct evaluation_sink {
    const wz::core::graph::Polytree<N, E> &topology;
    transform_state<Transform> &state;
    Policy &policy;
    scene_revision revision;

    bool push(wz::core::graph::NodeHandle node) noexcept {
        auto &target = transform_state_access::record(state, node);
        const auto parent_node = parent(topology, node);
        if (parent_node == wz::core::graph::INVALID_NODE) {
            target.world = target.local;
        } else {
            target.world = policy.compose(state.world(parent_node), target.local);
        }
        target.world_revision = revision;
        target.dirty = false;
        return true;
    }
};
} // namespace detail

template <class N, class E, class Transform>
[[nodiscard]] transform_plan_outcome
make_transform_evaluation_plan(const wz::core::graph::Polytree<N, E> &topology,
                               const transform_state<Transform> &state,
                               transform_evaluation_workspace &workspace) {
    return detail::make_plan(topology, state, workspace,
                             std::span<const wz::core::graph::NodeHandle>{}, false);
}

template <class N, class E, class Transform>
[[nodiscard]] transform_plan_outcome
make_transform_evaluation_plan(const wz::core::graph::Polytree<N, E> &topology,
                               const transform_state<Transform> &state,
                               transform_evaluation_workspace &workspace,
                               std::span<const wz::core::graph::NodeHandle> selected_dirty_roots) {
    return detail::make_plan(topology, state, workspace, selected_dirty_roots, true);
}

template <class N, class E, class Transform, class Policy>
    requires TransformPolicy<Policy> && std::same_as<typename Policy::transform_type, Transform>
[[nodiscard]] transform_evaluation_result
evaluate_transforms(const wz::core::graph::Polytree<N, E> &topology,
                    transform_state<Transform> &state, const transform_evaluation_plan &plan,
                    Policy &policy) {
    if (plan.topology_identity != &topology) {
        return {transform_error::topology_mismatch, {}, state.revision()};
    }
    if (plan.workspace == nullptr ||
        detail::transform_workspace_access::generation(*plan.workspace) !=
            plan.workspace_generation ||
        plan.source_revision != state.revision() ||
        plan.mutation_generation != state.mutation_generation() ||
        plan.evaluation_generation != state.evaluation_generation()) {
        return {transform_error::stale_plan, {}, state.revision()};
    }
    if (!plan.ordered_nodes.empty() && !detail::transform_state_access::revision_available(state)) {
        return {
            transform_error::revision_exhausted,
            {},
            state.revision(),
        };
    }

    const auto revision =
        detail::transform_state_access::begin_evaluation(state, !plan.ordered_nodes.empty());
    detail::evaluation_sink<N, E, Transform, Policy> sink{topology, state, policy, revision};
    const auto status = wz::core::algo::next::transform(
        plan.ordered_nodes, sink, [](wz::core::graph::NodeHandle node) { return node; });
    assert(status == wz::core::algo::next::execution_status::completed);
    (void)status;
    return {transform_error::none, plan.ordered_nodes, revision};
}

template <class N, class E, class Transform>
[[nodiscard]] std::span<const wz::core::graph::NodeHandle>
changed_transform_nodes_since(const wz::core::graph::Polytree<N, E> &topology,
                              const transform_state<Transform> &state, scene_revision since,
                              std::vector<wz::core::graph::NodeHandle> &scratch) {
    scratch.clear();
    scratch.reserve(wz::core::graph::node_count(topology));
    detail::vector_sink<wz::core::graph::NodeHandle> sink{scratch};
    const auto status =
        wz::core::algo::next::filter(wz::core::graph::evaluation_plan(topology).topological_order,
                                     sink, [&](wz::core::graph::NodeHandle node) {
                                         return state.record(node).world_revision > since;
                                     });
    assert(status == wz::core::algo::next::execution_status::completed);
    (void)status;
    return scratch;
}
} // namespace scene_polytree
