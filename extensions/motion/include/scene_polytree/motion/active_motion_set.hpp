#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <algo/next.h>
#include <graph/static_polytree.h>

#include <scene_polytree/motion/motion_error.hpp>
#include <scene_polytree/motion/motion_policy.hpp>
#include <scene_polytree/motion/motion_state.hpp>

namespace scene_polytree::motion {
namespace detail {
template <class Set, class Policy> struct motion_update_sink {
    Set &set;
    Policy &policy;
    motion_error error{motion_error::none};

    template <class Update> bool push(const Update &update) {
        error = set.set(update.node, update.state, policy);
        return error == motion_error::none;
    }
};
} // namespace detail

template <class LinearVelocity, class AngularVelocity> class active_motion_set {
  public:
    using state_type = motion_state<LinearVelocity, AngularVelocity>;
    using record_type = active_motion_record<LinearVelocity, AngularVelocity>;

    template <class N, class E>
    explicit active_motion_set(const wz::core::graph::Polytree<N, E> &topology) noexcept
        : m_topology_identity(&topology), m_node_count(wz::core::graph::node_count(topology)) {}

    [[nodiscard]] std::size_t size() const noexcept { return m_records.size(); }

    [[nodiscard]] bool empty() const noexcept { return m_records.empty(); }

    [[nodiscard]] std::size_t node_capacity() const noexcept { return m_node_count; }

    [[nodiscard]] std::size_t storage_capacity_bytes() const noexcept {
        return m_records.capacity() * sizeof(record_type);
    }

    [[nodiscard]] const void *topology_identity() const noexcept { return m_topology_identity; }

    [[nodiscard]] std::uint64_t mutation_generation() const noexcept {
        return m_mutation_generation;
    }

    [[nodiscard]] std::span<const record_type> records() const noexcept { return m_records; }

    template <class Policy>
        requires MotionPolicy<Policy> &&
                 std::same_as<typename Policy::linear_velocity_type, LinearVelocity> &&
                 std::same_as<typename Policy::angular_velocity_type, AngularVelocity>
    [[nodiscard]] motion_error set(wz::core::graph::NodeHandle node, state_type state,
                                   Policy &policy) {
        if (node >= m_node_count) {
            return motion_error::invalid_node;
        }

        const auto position = lower_bound(node);
        const bool exists = position != m_records.end() && position->node == node;
        if (policy.is_stationary(state)) {
            if (exists) {
                m_records.erase(position);
                ++m_mutation_generation;
            }
            return motion_error::none;
        }

        if (exists) {
            position->state = std::move(state);
        } else {
            m_records.insert(position, record_type{node, std::move(state)});
        }
        ++m_mutation_generation;
        return motion_error::none;
    }

    [[nodiscard]] motion_error deactivate(wz::core::graph::NodeHandle node) {
        if (node >= m_node_count) {
            return motion_error::invalid_node;
        }
        const auto position = lower_bound(node);
        if (position != m_records.end() && position->node == node) {
            m_records.erase(position);
            ++m_mutation_generation;
        }
        return motion_error::none;
    }

    template <std::ranges::forward_range Updates, class Policy>
        requires MotionPolicy<Policy> &&
                 std::same_as<typename Policy::linear_velocity_type, LinearVelocity> &&
                 std::same_as<typename Policy::angular_velocity_type, AngularVelocity>
    [[nodiscard]] motion_error apply_updates(const Updates &updates, Policy &policy) {
        const bool valid = std::ranges::all_of(
            updates, [&](const auto &update) { return update.node < m_node_count; });
        if (!valid) {
            return motion_error::invalid_node;
        }

        detail::motion_update_sink<active_motion_set, Policy> sink{*this, policy};
        const auto status = wz::core::algo::next::transform(
            updates, sink, [](const auto &update) -> const auto & { return update; });
        return status == wz::core::algo::next::execution_status::completed ? motion_error::none
                                                                           : sink.error;
    }

    void clear() noexcept {
        if (!m_records.empty()) {
            m_records.clear();
            ++m_mutation_generation;
        }
    }

  private:
    [[nodiscard]] auto lower_bound(wz::core::graph::NodeHandle node) {
        return std::ranges::lower_bound(m_records, node, {}, &record_type::node);
    }

    const void *m_topology_identity{};
    std::size_t m_node_count{};
    std::vector<record_type> m_records;
    std::uint64_t m_mutation_generation{};
};
} // namespace scene_polytree::motion
