#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#include <graph/polytree_freeze.h>
#include <graph/static_polytree.h>

#include <scene_polytree/authoring_scene.hpp>
#include <scene_polytree/transform_state.hpp>

namespace scene_polytree {
template <class NodeData, class EdgeData, class Transform> class basic_runtime_scene {
  public:
    using topology_storage_type = wz::core::graph::PolytreeStorage<NodeData, EdgeData>;
    using topology_type = wz::core::graph::Polytree<NodeData, EdgeData>;
    using state_type = transform_state<Transform>;
    using transform_type = Transform;

    basic_runtime_scene(topology_storage_type topology,
                        wz::core::graph::FrozenIdentityMap identities,
                        wz::core::graph::FreezeMetrics metrics, state_type state,
                        std::vector<scene_revision> authoring_local_revisions)
        : m_topology(std::move(topology)), m_identities(std::move(identities)), m_metrics(metrics),
          m_state(std::move(state)),
          m_authoring_local_revisions(std::move(authoring_local_revisions)) {}

    [[nodiscard]] const topology_type &topology() const noexcept { return m_topology.polytree; }

    [[nodiscard]] const topology_storage_type &topology_storage() const noexcept {
        return m_topology;
    }

    [[nodiscard]] const wz::core::graph::FrozenIdentityMap &identities() const noexcept {
        return m_identities;
    }

    [[nodiscard]] const wz::core::graph::FreezeMetrics &freeze_metrics() const noexcept {
        return m_metrics;
    }

    [[nodiscard]] state_type &state() noexcept { return m_state; }

    [[nodiscard]] const state_type &state() const noexcept { return m_state; }

    [[nodiscard]] scene_revision
    authoring_local_revision(wz::core::graph::NodeHandle node) const noexcept {
        return m_authoring_local_revisions[node];
    }

    [[nodiscard]] transform_error set_local(wz::core::graph::NodeHandle node, Transform local) {
        return m_state.set_local(node, std::move(local));
    }

  private:
    topology_storage_type m_topology;
    wz::core::graph::FrozenIdentityMap m_identities;
    wz::core::graph::FreezeMetrics m_metrics;
    state_type m_state;
    std::vector<scene_revision> m_authoring_local_revisions;
};

template <class NodeData, class EdgeData, class Transform> class scene_freeze_outcome {
  public:
    using value_type = basic_runtime_scene<NodeData, EdgeData, Transform>;

    [[nodiscard]] static scene_freeze_outcome success(value_type value) {
        return scene_freeze_outcome(std::move(value));
    }

    [[nodiscard]] static scene_freeze_outcome failure(wz::core::graph::FreezeError error) {
        return scene_freeze_outcome(error);
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_value.has_value(); }

    [[nodiscard]] value_type &value() & { return m_value.value(); }

    [[nodiscard]] const value_type &value() const & { return m_value.value(); }

    [[nodiscard]] value_type &&value() && { return std::move(m_value).value(); }

    [[nodiscard]] value_type *operator->() noexcept { return &m_value.value(); }

    [[nodiscard]] const value_type *operator->() const noexcept { return &m_value.value(); }

    [[nodiscard]] wz::core::graph::FreezeError error() const noexcept { return m_error; }

  private:
    explicit scene_freeze_outcome(value_type value) : m_value(std::move(value)) {}

    explicit scene_freeze_outcome(wz::core::graph::FreezeError error) : m_error(error) {}

    std::optional<value_type> m_value;
    wz::core::graph::FreezeError m_error{wz::core::graph::FreezeError::none};
};

namespace detail {
template <class N, class E>
[[nodiscard]] std::optional<wz::core::graph::StableNodeId>
stable_parent(const wz::core::graph::Polytree<N, E> &topology,
              const wz::core::graph::FrozenIdentityMap &identities,
              wz::core::graph::NodeHandle node) {
    const auto parent_node = wz::core::graph::parent(topology, node);
    return parent_node == wz::core::graph::INVALID_NODE ? std::nullopt
                                                        : identities.authoring_id(parent_node);
}

template <class N, class E, class Transform>
[[nodiscard]] scene_freeze_outcome<N, E, Transform>
freeze_scene_impl(const basic_authoring_scene<N, E, Transform> &source,
                  wz::core::graph::FreezeWorkspace &workspace,
                  const basic_runtime_scene<N, E, Transform> *previous) {
    auto generic = wz::core::graph::freeze(source.topology(), workspace);
    if (!generic) {
        return scene_freeze_outcome<N, E, Transform>::failure(generic.error());
    }

    auto frozen = std::move(generic).value();
    const auto &topology = frozen.topology.polytree;
    const auto stable_nodes = frozen.identities.runtime_to_authoring();
    std::vector<transform_record<Transform>> records;
    std::vector<scene_revision> authoring_local_revisions;
    records.reserve(stable_nodes.size());
    authoring_local_revisions.reserve(stable_nodes.size());
    const auto dense_nodes = std::views::iota(std::size_t{}, stable_nodes.size());
    std::ranges::for_each(dense_nodes, [&](std::size_t index) {
        const auto runtime = static_cast<wz::core::graph::NodeHandle>(index);
        const auto stable = stable_nodes[index];
        const auto &authoring = source.state().record(stable);
        authoring_local_revisions.push_back(authoring.local_revision);
        if (previous == nullptr) {
            records.push_back(transform_record<Transform>{
                authoring.local,
                authoring.local,
                authoring.local_revision,
                scene_revision{},
                true,
            });
            return;
        }

        const auto old_runtime = previous->identities().runtime_handle(stable);
        if (!old_runtime) {
            records.push_back(transform_record<Transform>{
                authoring.local,
                authoring.local,
                authoring.local_revision,
                scene_revision{},
                true,
            });
            return;
        }

        auto result = previous->state().record(*old_runtime);
        const auto old_authoring_revision = previous->authoring_local_revision(*old_runtime);
        const bool local_changed = old_authoring_revision != authoring.local_revision;
        const bool runtime_local_diverged = result.local_revision != old_authoring_revision;
        const bool parent_changed =
            stable_parent(previous->topology(), previous->identities(), *old_runtime) !=
            stable_parent(topology, frozen.identities, runtime);
        result.local = authoring.local;
        result.local_revision = authoring.local_revision;
        result.dirty = result.dirty || local_changed || runtime_local_diverged || parent_changed;
        records.push_back(std::move(result));
    });

    const auto previous_revision =
        previous == nullptr ? scene_revision{} : previous->state().revision();
    const auto revision = std::max(source.state().revision(), previous_revision);
    transform_state<Transform> state{std::move(records), revision};
    return scene_freeze_outcome<N, E, Transform>::success({
        std::move(frozen.topology),
        std::move(frozen.identities),
        frozen.metrics,
        std::move(state),
        std::move(authoring_local_revisions),
    });
}
} // namespace detail

template <class N, class E, class Transform>
[[nodiscard]] scene_freeze_outcome<N, E, Transform>
freeze_scene(const basic_authoring_scene<N, E, Transform> &source,
             wz::core::graph::FreezeWorkspace &workspace) {
    return detail::freeze_scene_impl(
        source, workspace, static_cast<const basic_runtime_scene<N, E, Transform> *>(nullptr));
}

template <class N, class E, class Transform>
[[nodiscard]] scene_freeze_outcome<N, E, Transform>
freeze_scene(const basic_authoring_scene<N, E, Transform> &source,
             wz::core::graph::FreezeWorkspace &workspace,
             const basic_runtime_scene<N, E, Transform> &previous) {
    return detail::freeze_scene_impl(source, workspace, &previous);
}
} // namespace scene_polytree
