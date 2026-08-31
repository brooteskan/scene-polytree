#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include <graph/handles.h>

#include <scene_polytree/transform_record.hpp>

namespace scene_polytree {
using scene_revision = std::uint64_t;

enum class transform_error {
    none,
    invalid_node,
    state_size_mismatch,
    invalid_scope,
    stale_plan,
    topology_mismatch,
    revision_exhausted,
};

namespace detail {
struct transform_state_access;
}

template <class Transform> class transform_state {
  public:
    using transform_type = Transform;
    using record_type = transform_record<Transform>;

    transform_state() = default;

    explicit transform_state(std::vector<record_type> records, scene_revision revision = {})
        : m_records(std::move(records)), m_revision(revision), m_mutation_generation(1) {
        m_dirty_nodes.reserve(m_records.size());
        auto nodes = std::views::iota(std::size_t{}, m_records.size()) |
                     std::views::filter(
                         [&](std::size_t node) { return m_records[node].dirty; });
        std::ranges::for_each(nodes, [&](std::size_t node) {
            m_dirty_nodes.push_back(static_cast<wz::core::graph::NodeHandle>(node));
        });
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_records.size(); }

    [[nodiscard]] bool empty() const noexcept { return m_records.empty(); }

    [[nodiscard]] scene_revision revision() const noexcept { return m_revision; }

    [[nodiscard]] std::uint64_t mutation_generation() const noexcept {
        return m_mutation_generation;
    }

    [[nodiscard]] std::uint64_t evaluation_generation() const noexcept {
        return m_evaluation_generation;
    }

    [[nodiscard]] std::span<const record_type> records() const noexcept { return m_records; }

    [[nodiscard]] bool has_dirty_transforms() const noexcept { return !m_dirty_nodes.empty(); }

    [[nodiscard]] std::span<const wz::core::graph::NodeHandle> dirty_nodes() const noexcept {
        return m_dirty_nodes;
    }

    [[nodiscard]] std::size_t dirty_frontier_capacity_bytes() const noexcept {
        return m_dirty_nodes.capacity() * sizeof(wz::core::graph::NodeHandle);
    }

    [[nodiscard]] const record_type &record(wz::core::graph::NodeHandle node) const noexcept {
        return m_records[node];
    }

    [[nodiscard]] const Transform &local(wz::core::graph::NodeHandle node) const noexcept {
        return record(node).local;
    }

    [[nodiscard]] const Transform &world(wz::core::graph::NodeHandle node) const noexcept {
        return record(node).world;
    }

    [[nodiscard]] transform_error set_local(wz::core::graph::NodeHandle node, Transform local) {
        if (node >= m_records.size()) {
            return transform_error::invalid_node;
        }
        if (!revision_available()) {
            return transform_error::revision_exhausted;
        }

        auto &target = m_records[node];
        if (!target.dirty) {
            m_dirty_nodes.push_back(node);
        }
        target.local = std::move(local);
        ++m_revision;
        ++m_mutation_generation;
        target.local_revision = m_revision;
        target.dirty = true;
        return transform_error::none;
    }

    [[nodiscard]] transform_error mark_dirty(wz::core::graph::NodeHandle node) {
        if (node >= m_records.size()) {
            return transform_error::invalid_node;
        }
        if (!revision_available()) {
            return transform_error::revision_exhausted;
        }

        auto &target = m_records[node];
        if (!target.dirty) {
            m_dirty_nodes.push_back(node);
        }
        ++m_revision;
        ++m_mutation_generation;
        target.dirty = true;
        return transform_error::none;
    }

  private:
    friend struct detail::transform_state_access;

    [[nodiscard]] bool revision_available() const noexcept {
        return m_revision != std::numeric_limits<scene_revision>::max();
    }

    std::vector<record_type> m_records;
    std::vector<wz::core::graph::NodeHandle> m_dirty_nodes;
    scene_revision m_revision{};
    std::uint64_t m_mutation_generation{};
    std::uint64_t m_evaluation_generation{};
};

namespace detail {
struct transform_state_access {
    template <class Transform>
    [[nodiscard]] static bool revision_available(const transform_state<Transform> &state) noexcept {
        return state.revision_available();
    }

    template <class Transform>
    [[nodiscard]] static scene_revision begin_evaluation(transform_state<Transform> &state,
                                                         bool has_work) noexcept {
        ++state.m_evaluation_generation;
        if (has_work) {
            ++state.m_revision;
        }
        return state.m_revision;
    }

    template <class Transform>
    [[nodiscard]] static transform_record<Transform> &
    record(transform_state<Transform> &state, wz::core::graph::NodeHandle node) noexcept {
        return state.m_records[node];
    }

    template <class Transform>
    static void compact_dirty_frontier(transform_state<Transform> &state) {
        std::erase_if(state.m_dirty_nodes, [&](wz::core::graph::NodeHandle node) {
            return !state.m_records[node].dirty;
        });
    }
};
} // namespace detail
} // namespace scene_polytree
