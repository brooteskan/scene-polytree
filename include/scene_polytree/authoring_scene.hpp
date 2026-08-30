#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>

#include <graph/mutable_polytree.h>

#include <scene_polytree/transform_state.hpp>

namespace scene_polytree {
template <class Transform> struct authoring_transform_record {
    Transform local{};
    scene_revision local_revision{};
};

template <class Transform> class authoring_transform_state {
  public:
    using record_type = authoring_transform_record<Transform>;

    [[nodiscard]] scene_revision revision() const noexcept { return m_revision; }

    [[nodiscard]] bool revision_available() const noexcept {
        return m_revision != std::numeric_limits<scene_revision>::max();
    }

    [[nodiscard]] bool contains(wz::core::graph::StableNodeId node) const noexcept {
        return node.valid() && m_records.contains(node.value);
    }

    [[nodiscard]] const record_type &record(wz::core::graph::StableNodeId node) const {
        return m_records.at(node.value);
    }

    [[nodiscard]] const Transform &local(wz::core::graph::StableNodeId node) const {
        return record(node).local;
    }

    [[nodiscard]] transform_error set_local(wz::core::graph::StableNodeId node, Transform local) {
        const auto position = m_records.find(node.value);
        if (!node.valid() || position == m_records.end()) {
            return transform_error::invalid_node;
        }
        if (!revision_available()) {
            return transform_error::revision_exhausted;
        }

        position->second.local = std::move(local);
        ++m_revision;
        position->second.local_revision = m_revision;
        return transform_error::none;
    }

  private:
    template <class N, class E, class T> friend class basic_authoring_scene;

    [[nodiscard]] transform_error insert(wz::core::graph::StableNodeId node, Transform local) {
        if (!revision_available()) {
            return transform_error::revision_exhausted;
        }

        ++m_revision;
        m_records.emplace(node.value, record_type{std::move(local), m_revision});
        return transform_error::none;
    }

    void erase_absent_nodes(const auto &topology) {
        std::erase_if(m_records, [&](const auto &item) {
            return !wz::core::graph::contains(topology, wz::core::graph::StableNodeId{item.first});
        });
    }

    [[nodiscard]] transform_error note_scene_change() {
        if (!revision_available()) {
            return transform_error::revision_exhausted;
        }
        ++m_revision;
        return transform_error::none;
    }

    std::unordered_map<std::uint64_t, record_type> m_records;
    scene_revision m_revision{};
};

template <class NodeData, class EdgeData, class Transform> class basic_authoring_scene {
  public:
    using topology_type = wz::core::graph::MutablePolytree<NodeData, EdgeData>;
    using state_type = authoring_transform_state<Transform>;
    using transform_type = Transform;

    [[nodiscard]] const topology_type &topology() const noexcept { return m_topology; }

    [[nodiscard]] const state_type &state() const noexcept { return m_state; }

    [[nodiscard]] scene_revision revision() const noexcept { return m_state.revision(); }

    [[nodiscard]] wz::core::graph::MutationResult<wz::core::graph::StableNodeId>
    insert_root(NodeData data, Transform local,
                std::size_t ordinal = wz::core::graph::APPEND_CHILD) {
        if (!m_state.revision_available()) {
            return wz::core::graph::MutationResult<wz::core::graph::StableNodeId>::failure(
                wz::core::graph::MutationError::revision_exhausted);
        }

        auto outcome = wz::core::graph::insert_root(m_topology, std::move(data), ordinal);
        if (outcome) {
            (void)m_state.insert(outcome.value(), std::move(local));
        }
        return outcome;
    }

    [[nodiscard]] wz::core::graph::MutationResult<wz::core::graph::StableNodeId>
    insert_child(wz::core::graph::StableNodeId parent, NodeData data, EdgeData edge_data,
                 Transform local, std::size_t ordinal = wz::core::graph::APPEND_CHILD) {
        if (!m_state.revision_available()) {
            return wz::core::graph::MutationResult<wz::core::graph::StableNodeId>::failure(
                wz::core::graph::MutationError::revision_exhausted);
        }

        auto outcome = wz::core::graph::insert_child(m_topology, parent, std::move(data),
                                                     std::move(edge_data), ordinal);
        if (outcome) {
            (void)m_state.insert(outcome.value(), std::move(local));
        }
        return outcome;
    }

    [[nodiscard]] transform_error set_local(wz::core::graph::StableNodeId node, Transform local) {
        if (!wz::core::graph::contains(m_topology, node)) {
            return transform_error::invalid_node;
        }
        return m_state.set_local(node, std::move(local));
    }

    [[nodiscard]] wz::core::graph::MutationResult<wz::core::graph::StableNodeId>
    reparent(wz::core::graph::StableNodeId node, wz::core::graph::StableNodeId new_parent,
             EdgeData edge_data, std::size_t ordinal = wz::core::graph::APPEND_CHILD) {
        const auto old_parent = wz::core::graph::contains(m_topology, node)
                                    ? wz::core::graph::parent(m_topology, node)
                                    : wz::core::graph::INVALID_STABLE_NODE;
        if (old_parent != new_parent && !m_state.revision_available()) {
            return wz::core::graph::MutationResult<wz::core::graph::StableNodeId>::failure(
                wz::core::graph::MutationError::revision_exhausted);
        }

        auto outcome =
            wz::core::graph::reparent(m_topology, node, new_parent, std::move(edge_data), ordinal);
        if (outcome && old_parent != new_parent) {
            (void)m_state.note_scene_change();
        }
        return outcome;
    }

    [[nodiscard]] wz::core::graph::MutationResult<wz::core::graph::StableNodeId>
    detach_to_root(wz::core::graph::StableNodeId node,
                   std::size_t ordinal = wz::core::graph::APPEND_CHILD) {
        const bool parent_changes =
            wz::core::graph::contains(m_topology, node) &&
            wz::core::graph::parent(m_topology, node) != wz::core::graph::INVALID_STABLE_NODE;
        if (parent_changes && !m_state.revision_available()) {
            return wz::core::graph::MutationResult<wz::core::graph::StableNodeId>::failure(
                wz::core::graph::MutationError::revision_exhausted);
        }

        auto outcome = wz::core::graph::detach_to_root(m_topology, node, ordinal);
        if (outcome && parent_changes) {
            (void)m_state.note_scene_change();
        }
        return outcome;
    }

    [[nodiscard]] wz::core::graph::MutationResult<std::size_t>
    erase_subtree(wz::core::graph::StableNodeId node) {
        if (!m_state.revision_available()) {
            return wz::core::graph::MutationResult<std::size_t>::failure(
                wz::core::graph::MutationError::revision_exhausted);
        }

        auto outcome = wz::core::graph::erase_subtree(m_topology, node);
        if (outcome) {
            m_state.erase_absent_nodes(m_topology);
            (void)m_state.note_scene_change();
        }
        return outcome;
    }

    [[nodiscard]] wz::core::graph::MutationResult<wz::core::graph::StableNodeId>
    replace_node_data(wz::core::graph::StableNodeId node, NodeData data) {
        return wz::core::graph::replace_node_data(m_topology, node, std::move(data));
    }

    [[nodiscard]] wz::core::graph::MutationResult<wz::core::graph::StableNodeId>
    replace_parent_edge_data(wz::core::graph::StableNodeId node, EdgeData edge_data) {
        return wz::core::graph::replace_parent_edge_data(m_topology, node, std::move(edge_data));
    }

  private:
    topology_type m_topology;
    state_type m_state;
};
} // namespace scene_polytree
