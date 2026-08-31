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

template <class LinearVelocity, class AngularVelocity> class active_motion_update_workspace {
  public:
    using update_type = motion_update<LinearVelocity, AngularVelocity>;
    using record_type = active_motion_record<LinearVelocity, AngularVelocity>;

    [[nodiscard]] std::size_t scratch_capacity_bytes() const noexcept {
        return m_updates.capacity() * sizeof(normalized_update) +
               m_merged.capacity() * sizeof(record_type) +
               m_stationary.capacity() * sizeof(std::uint8_t);
    }

  private:
    template <class, class> friend class active_motion_set;

    struct normalized_update {
        update_type update;
        std::size_t input_order{};
    };

    std::vector<normalized_update> m_updates;
    std::vector<record_type> m_merged;
    std::vector<std::uint8_t> m_stationary;
};

template <class LinearVelocity, class AngularVelocity> class active_motion_set {
  public:
    using state_type = motion_state<LinearVelocity, AngularVelocity>;
    using update_type = motion_update<LinearVelocity, AngularVelocity>;
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

    template <std::ranges::forward_range Updates, class Policy>
        requires MotionPolicy<Policy> &&
                 std::same_as<typename Policy::linear_velocity_type, LinearVelocity> &&
                 std::same_as<typename Policy::angular_velocity_type, AngularVelocity>
    [[nodiscard]] motion_error
    apply_updates(const Updates &updates, Policy &policy,
                  active_motion_update_workspace<LinearVelocity, AngularVelocity> &workspace) {
        const bool valid = std::ranges::all_of(
            updates, [&](const auto &update) { return update.node < m_node_count; });
        if (!valid) {
            return motion_error::invalid_node;
        }

        auto &normalized = workspace.m_updates;
        auto &merged = workspace.m_merged;
        auto &stationary = workspace.m_stationary;
        normalized.clear();
        std::size_t input_order{};
        std::ranges::for_each(updates, [&](const auto &update) {
            normalized.push_back({update, input_order++});
        });

        // Descending input order inside each handle group puts the last input
        // update first. The in-place sort avoids the temporary allocation that
        // stable_sort is permitted to make.
        std::ranges::sort(normalized, [](const auto &left, const auto &right) {
            return left.update.node != right.update.node
                       ? left.update.node < right.update.node
                       : left.input_order > right.input_order;
        });
        const auto duplicates = std::ranges::unique(
            normalized, {}, [](const auto &entry) { return entry.update.node; });
        normalized.erase(duplicates.begin(), duplicates.end());
        stationary.resize(normalized.size());
        std::ranges::transform(normalized, stationary.begin(), [&](const auto &entry) {
            return static_cast<std::uint8_t>(policy.is_stationary(entry.update.state));
        });

        std::size_t final_size{};
        std::size_t count_existing{};
        std::size_t count_updates{};
        const auto count_pending = std::views::iota(std::size_t{}) |
                                   std::views::take_while([&](std::size_t) {
                                       return count_existing < m_records.size() ||
                                              count_updates < normalized.size();
                                   });
        std::ranges::for_each(count_pending, [&](std::size_t) {
            const bool has_existing = count_existing < m_records.size();
            const bool has_update = count_updates < normalized.size();
            if (!has_update ||
                (has_existing && m_records[count_existing].node <
                                     normalized[count_updates].update.node)) {
                ++count_existing;
                ++final_size;
                return;
            }
            const auto &update = normalized[count_updates].update;
            const bool same_node =
                has_existing && m_records[count_existing].node == update.node;
            final_size += stationary[count_updates] != 0 ? 0u : 1u;
            count_existing += same_node ? 1u : 0u;
            ++count_updates;
        });

        merged.clear();
        m_records.reserve(final_size);
        merged.reserve(final_size);
        std::size_t existing_index{};
        std::size_t update_index{};
        bool mutated{};
        const auto pending = std::views::iota(std::size_t{}) |
                             std::views::take_while([&](std::size_t) {
                                 return existing_index < m_records.size() ||
                                        update_index < normalized.size();
                             });
        std::ranges::for_each(pending, [&](std::size_t) {
            const bool has_existing = existing_index < m_records.size();
            const bool has_update = update_index < normalized.size();
            if (!has_update ||
                (has_existing && m_records[existing_index].node <
                                     normalized[update_index].update.node)) {
                merged.push_back(m_records[existing_index++]);
                return;
            }

            const auto &update = normalized[update_index].update;
            const bool same_node =
                has_existing && m_records[existing_index].node == update.node;
            const bool update_is_stationary = stationary[update_index] != 0;
            if (!update_is_stationary) {
                merged.push_back(record_type{update.node, update.state});
            }
            mutated = mutated || same_node || !update_is_stationary;
            existing_index += same_node ? 1u : 0u;
            ++update_index;
        });

        if (mutated) {
            m_records.swap(merged);
            ++m_mutation_generation;
        } else {
            // The merge only copied the existing set when every normalized update
            // was an absent-node deactivation, so preserve record storage and spans.
            merged.clear();
        }
        return motion_error::none;
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
