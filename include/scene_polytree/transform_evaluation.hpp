#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <algo/next.h>
#include <graph/static_polytree.h>

#include <scene_polytree/transform_policy.hpp>
#include <scene_polytree/transform_state.hpp>
#include <scene_polytree/cpu_task_executor.hpp>

namespace scene_polytree {
namespace detail {
struct transform_workspace_access;
}

class transform_evaluation_workspace {
  public:
    [[nodiscard]] std::size_t scratch_capacity_bytes() const noexcept {
        return m_affected.capacity() * sizeof(std::uint64_t) +
               m_owner.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_selected.capacity() * sizeof(std::uint64_t) +
               m_topological_rank.capacity() * sizeof(std::uint32_t) +
               m_subtree_end.capacity() * sizeof(std::uint32_t) +
               m_subtree_size.capacity() * sizeof(std::uint32_t) +
               m_dependency_level.capacity() * sizeof(std::uint32_t) +
               m_dependency_nodes.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_dependency_offsets.capacity() * sizeof(std::uint32_t) +
               m_dependency_cursor.capacity() * sizeof(std::uint32_t) +
               m_dirty_candidates.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_dirty_roots.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_affected_nodes.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_selected_dirty_roots.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_selected_nodes.capacity() * sizeof(wz::core::graph::NodeHandle) +
               m_requested_roots.capacity() * sizeof(wz::core::graph::NodeHandle);
    }

  private:
    friend struct detail::transform_workspace_access;

    template <class N, class E>
    void prepare(const wz::core::graph::Polytree<N, E> &topology) {
        if (++m_generation == 0) {
            std::ranges::fill(m_affected, std::uint64_t{});
            std::ranges::fill(m_selected, std::uint64_t{});
            ++m_generation;
        }
        const auto generic_plan = wz::core::graph::evaluation_plan(topology);
        const auto node_total = generic_plan.node_count();
        if (m_topology_identity != &topology || m_topological_rank.size() != node_total) {
            m_topology_identity = &topology;
            m_affected.assign(node_total, std::uint64_t{});
            m_owner.assign(node_total, wz::core::graph::INVALID_NODE);
            m_selected.assign(node_total, std::uint64_t{});
            m_topological_rank.resize(node_total);
            m_subtree_end.resize(node_total);
            m_subtree_size.assign(node_total, 1u);
            m_dependency_level.resize(node_total);
            m_maximum_level_width = 0;
            const auto positions = std::views::iota(std::size_t{}, node_total);
            std::ranges::for_each(positions, [&](std::size_t position) {
                const auto node = generic_plan.topological_order[position];
                m_topological_rank[node] = static_cast<std::uint32_t>(position);
                m_subtree_end[node] = static_cast<std::uint32_t>(position + 1u);
            });
            std::ranges::for_each(generic_plan.reverse_topological_order, [&](auto node) {
                const auto parent_node = parent(topology, node);
                if (parent_node != wz::core::graph::INVALID_NODE) {
                    m_subtree_end[parent_node] =
                        std::max(m_subtree_end[parent_node], m_subtree_end[node]);
                    m_subtree_size[parent_node] += m_subtree_size[node];
                }
            });
            std::ranges::for_each(
                std::views::iota(std::size_t{}, generic_plan.level_count()),
                [&](std::size_t level) {
                    const auto level_nodes = generic_plan.dependency_level(level);
                    m_maximum_level_width = std::max(m_maximum_level_width,
                                                     level_nodes.size());
                    std::ranges::for_each(level_nodes, [&](auto node) {
                        m_dependency_level[node] = static_cast<std::uint32_t>(level);
                    });
                });
            m_dependency_nodes.reserve(node_total);
            m_dependency_offsets.reserve(generic_plan.level_count() + 1u);
            m_dependency_cursor.reserve(generic_plan.level_count() + 1u);
            m_subtrees_are_contiguous = std::ranges::all_of(
                generic_plan.topological_order, [&](auto node) {
                    return m_subtree_end[node] - m_topological_rank[node] ==
                           m_subtree_size[node];
                });
        }
        m_dirty_candidates.clear();
        m_dirty_roots.clear();
        m_affected_nodes.clear();
        m_selected_dirty_roots.clear();
        m_selected_nodes.clear();
        m_dirty_candidates.reserve(node_total);
        m_dirty_roots.reserve(node_total);
        m_affected_nodes.reserve(node_total);
        m_selected_dirty_roots.reserve(node_total);
        m_selected_nodes.reserve(node_total);
    }

    std::vector<std::uint64_t> m_affected;
    std::vector<wz::core::graph::NodeHandle> m_owner;
    std::vector<std::uint64_t> m_selected;
    std::vector<std::uint32_t> m_topological_rank;
    std::vector<std::uint32_t> m_subtree_end;
    std::vector<std::uint32_t> m_subtree_size;
    std::vector<std::uint32_t> m_dependency_level;
    std::vector<wz::core::graph::NodeHandle> m_dependency_nodes;
    std::vector<std::uint32_t> m_dependency_offsets;
    std::vector<std::uint32_t> m_dependency_cursor;
    std::vector<wz::core::graph::NodeHandle> m_dirty_candidates;
    std::vector<wz::core::graph::NodeHandle> m_dirty_roots;
    std::vector<wz::core::graph::NodeHandle> m_affected_nodes;
    std::vector<wz::core::graph::NodeHandle> m_selected_dirty_roots;
    std::vector<wz::core::graph::NodeHandle> m_selected_nodes;
    std::vector<wz::core::graph::NodeHandle> m_requested_roots;
    const void *m_topology_identity{};
    bool m_subtrees_are_contiguous{};
    std::size_t m_maximum_level_width{};
    std::uint64_t m_generation{};
};

namespace detail {
struct transform_workspace_access {
    template <class N, class E>
    static void prepare(transform_evaluation_workspace &workspace,
                        const wz::core::graph::Polytree<N, E> &topology) {
        workspace.prepare(topology);
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

    [[nodiscard]] static auto &
    topological_rank(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_topological_rank;
    }

    [[nodiscard]] static auto &subtree_end(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_subtree_end;
    }

    [[nodiscard]] static auto &subtree_size(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_subtree_size;
    }

    [[nodiscard]] static auto &
    dirty_candidates(transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_dirty_candidates;
    }

    [[nodiscard]] static bool
    subtrees_are_contiguous(const transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_subtrees_are_contiguous;
    }

    template <class N, class E>
    [[nodiscard]] static std::size_t prepare_dependency_batches(
        transform_evaluation_workspace &workspace,
        const wz::core::graph::Polytree<N, E> &topology,
        std::span<const wz::core::graph::NodeHandle> ordered_nodes) {
        const auto level_count = wz::core::graph::evaluation_plan(topology).level_count();
        workspace.m_dependency_offsets.assign(level_count + 1u, 0u);
        std::ranges::for_each(ordered_nodes, [&](auto node) {
            workspace.m_affected[node] = workspace.m_generation;
            ++workspace.m_dependency_offsets[workspace.m_dependency_level[node] + 1u];
        });
        const auto maximum_batch = workspace.m_dependency_offsets.empty()
                                       ? std::size_t{}
                                       : static_cast<std::size_t>(
                                             *std::ranges::max_element(
                                                 workspace.m_dependency_offsets));
        std::partial_sum(workspace.m_dependency_offsets.begin(),
                         workspace.m_dependency_offsets.end(),
                         workspace.m_dependency_offsets.begin());
        workspace.m_dependency_cursor = workspace.m_dependency_offsets;
        workspace.m_dependency_nodes.resize(ordered_nodes.size());
        std::ranges::for_each(ordered_nodes, [&](auto node) {
            const auto level = workspace.m_dependency_level[node];
            workspace.m_dependency_nodes[workspace.m_dependency_cursor[level]++] = node;
        });
        return maximum_batch;
    }

    [[nodiscard]] static std::size_t
    maximum_level_width(const transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_maximum_level_width;
    }

    [[nodiscard]] static std::span<const wz::core::graph::NodeHandle>
    dependency_nodes(const transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_dependency_nodes;
    }

    [[nodiscard]] static std::span<const std::uint32_t>
    dependency_offsets(const transform_evaluation_workspace &workspace) noexcept {
        return workspace.m_dependency_offsets;
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
    transform_evaluation_workspace *workspace{};
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

struct transform_execution_options {
    std::size_t minimum_task_grain{2'048};
};

namespace detail {
template <class T> struct vector_sink {
    std::vector<T> &values;

    bool push(T value) {
        values.push_back(std::move(value));
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
    transform_workspace_access::prepare(workspace, topology);
    selected_roots = requested_roots;
    auto &dirty_roots = transform_workspace_access::dirty_roots(workspace);
    auto &affected_nodes = transform_workspace_access::affected_nodes(workspace);
    auto &affected = transform_workspace_access::affected(workspace);
    auto &owners = transform_workspace_access::owner(workspace);
    auto &rank = transform_workspace_access::topological_rank(workspace);
    auto &subtree_end = transform_workspace_access::subtree_end(workspace);
    auto &dirty_candidates = transform_workspace_access::dirty_candidates(workspace);
    const auto epoch = transform_workspace_access::generation(workspace);

    if (!state.has_dirty_transforms()) {
        // The exact direct-dirty frontier makes the common clean plan independent
        // of scene size.
    } else if (!partial && state.dirty_nodes().size() == generic_plan.node_count()) {
        // Every node is directly dirty, so the cached generic views already are
        // the canonical roots and affected order. This avoids sorting or stamping
        // a frontier whose ownership cannot change the full-plan result.
        dirty_roots.assign(generic_plan.roots.begin(), generic_plan.roots.end());
        affected_nodes.assign(generic_plan.topological_order.begin(),
                              generic_plan.topological_order.end());
    } else if (transform_workspace_access::subtrees_are_contiguous(workspace)) {
        dirty_candidates.assign(state.dirty_nodes().begin(), state.dirty_nodes().end());
        const auto rank_of = [&](wz::core::graph::NodeHandle node) { return rank[node]; };
        if (!std::ranges::is_sorted(dirty_candidates, {}, rank_of)) {
            std::ranges::sort(dirty_candidates, {}, rank_of);
        }
        std::ranges::for_each(dirty_candidates, [&](wz::core::graph::NodeHandle candidate) {
            if (affected[candidate] == epoch) {
                return;
            }
            dirty_roots.push_back(candidate);
            const auto first = rank[candidate];
            const auto nodes = generic_plan.topological_order.subspan(
                first, subtree_end[candidate] - first);
            std::ranges::for_each(nodes, [&](wz::core::graph::NodeHandle node) {
                affected[node] = epoch;
                owners[node] = candidate;
                affected_nodes.push_back(node);
            });
        });
    } else {
        // A foreign polytree implementation may provide a valid topological order
        // without contiguous subtree ranges. Preserve correctness for that case.
        std::ranges::for_each(generic_plan.topological_order, [&](auto node) {
            const auto parent_node = parent(topology, node);
            const bool parent_affected = parent_node != wz::core::graph::INVALID_NODE &&
                                         affected[parent_node] == epoch;
            if (!state.record(node).dirty && !parent_affected) {
                return;
            }
            const auto owner = parent_affected ? owners[parent_node] : node;
            affected[node] = epoch;
            owners[node] = owner;
            affected_nodes.push_back(node);
            if (!parent_affected) {
                dirty_roots.push_back(node);
            }
        });
    }

    auto &selected = transform_workspace_access::selected(workspace);
    auto &selected_dirty_roots = transform_workspace_access::selected_dirty_roots(workspace);
    auto &selected_nodes = transform_workspace_access::selected_nodes(workspace);
    auto plan_roots = std::span<const wz::core::graph::NodeHandle>{dirty_roots};
    auto plan_nodes = std::span<const wz::core::graph::NodeHandle>{affected_nodes};

    if (partial) {
        const bool valid_scope =
            std::ranges::all_of(selected_roots, [&](wz::core::graph::NodeHandle root) {
                if (root >= selected.size() || selected[root] == epoch ||
                    affected[root] != epoch ||
                    owners[root] != root) {
                    return false;
                }
                selected[root] = epoch;
                return true;
            });
        if (!valid_scope) {
            return transform_plan_outcome::failure(transform_error::invalid_scope);
        }

        vector_sink<wz::core::graph::NodeHandle> root_sink{selected_dirty_roots};
        const auto root_status = wz::core::algo::next::filter(
            dirty_roots, root_sink,
            [&](wz::core::graph::NodeHandle root) { return selected[root] == epoch; });
        assert(root_status == wz::core::algo::next::execution_status::completed);
        (void)root_status;

        vector_sink<wz::core::graph::NodeHandle> node_sink{selected_nodes};
        const auto node_status = wz::core::algo::next::filter(
            affected_nodes, node_sink,
            [&](wz::core::graph::NodeHandle node) {
                return selected[owners[node]] == epoch;
            });
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

template <class N, class E, class Transform, class Policy>
    requires TransformPolicy<Policy> && std::same_as<typename Policy::transform_type, Transform>
struct parallel_evaluation_context {
    const wz::core::graph::Polytree<N, E> &topology;
    transform_state<Transform> &state;
    Policy &policy;
    std::span<const wz::core::graph::NodeHandle> nodes;

    static void execute(void *opaque, std::size_t first, std::size_t last) noexcept {
        auto &context = *static_cast<parallel_evaluation_context *>(opaque);
        std::ranges::for_each(context.nodes.subspan(first, last - first), [&](auto node) {
            auto &target = transform_state_access::record(context.state, node);
            const auto parent_node = parent(context.topology, node);
            target.world = parent_node == wz::core::graph::INVALID_NODE
                               ? target.local
                               : context.policy.compose(context.state.world(parent_node),
                                                        target.local);
        });
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
    detail::transform_state_access::compact_dirty_frontier(state);
    return {transform_error::none, plan.ordered_nodes, revision};
}

template <class N, class E, class Transform, class Policy>
    requires TransformPolicy<Policy> && std::same_as<typename Policy::transform_type, Transform>
[[nodiscard]] transform_evaluation_result evaluate_transforms(
    const wz::core::graph::Polytree<N, E> &topology, transform_state<Transform> &state,
    const transform_evaluation_plan &plan, Policy &policy, cpu_task_executor &executor,
    transform_execution_options options = {}) {
    executor.reset_statistics();
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
        return {transform_error::revision_exhausted, {}, state.revision()};
    }
    const auto grain = std::max<std::size_t>(1, options.minimum_task_grain);
    if (plan.ordered_nodes.size() / grain < 2 || executor.worker_count() == 0 ||
        detail::transform_workspace_access::maximum_level_width(*plan.workspace) / grain < 2) {
        return evaluate_transforms(topology, state, plan, policy);
    }

    const auto maximum_batch = detail::transform_workspace_access::prepare_dependency_batches(
        *plan.workspace, topology, plan.ordered_nodes);
    if (maximum_batch / grain < 2) {
        return evaluate_transforms(topology, state, plan, policy);
    }
    const auto dependency_nodes =
        detail::transform_workspace_access::dependency_nodes(*plan.workspace);
    const auto offsets = detail::transform_workspace_access::dependency_offsets(*plan.workspace);
    const auto revision =
        detail::transform_state_access::begin_evaluation(state, !plan.ordered_nodes.empty());
    detail::parallel_evaluation_context<N, E, Transform, Policy> context{
        topology, state, policy, {}};
    std::ranges::for_each(std::views::iota(std::size_t{}, offsets.size() - 1u),
                          [&](std::size_t level) {
                              const auto first = offsets[level];
                              const auto count = offsets[level + 1u] - first;
                              context.nodes = dependency_nodes.subspan(first, count);
                              executor.execute(count, grain, &context,
                                               &decltype(context)::execute);
                          });

    std::ranges::for_each(plan.ordered_nodes, [&](auto node) {
        auto &target = detail::transform_state_access::record(state, node);
        target.world_revision = revision;
        target.dirty = false;
    });
    detail::transform_state_access::compact_dirty_frontier(state);
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
