#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_polytree.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
struct translation {
    double value{};
};

struct scalar_motion_policy {
    using transform_type = translation;
    using linear_velocity_type = double;
    using angular_velocity_type = double;

    [[nodiscard]] translation
    integrate(const translation &local,
              const scene_polytree::motion::motion_state<double, double> &state,
              scene_polytree::motion::fixed_motion_step step) noexcept {
        const auto seconds = std::chrono::duration<double>(step.delta).count();
        return {local.value + (state.linear_velocity + state.angular_velocity) * seconds};
    }

    [[nodiscard]] bool
    is_stationary(const scene_polytree::motion::motion_state<double, double> &state) noexcept {
        return state.linear_velocity == 0.0 && state.angular_velocity == 0.0;
    }
};

using state = scene_polytree::motion::motion_state<double, double>;
using authoring_scene =
    scene_polytree::basic_authoring_scene<std::uint32_t, std::uint32_t, translation>;
using clock_type = std::chrono::steady_clock;

struct benchmark_result {
    std::size_t node_count{};
    double active_ratio{};
    std::size_t active_count{};
    std::size_t repetitions{};
    std::size_t sparse_storage_bytes{};
    std::size_t dense_storage_bytes{};
    double sparse_registration_ns{};
    double dense_registration_ns{};
    double sparse_tick_ns{};
    double dense_tick_ns{};
    double checksum{};
};

[[nodiscard]] std::size_t active_count(std::size_t node_count, double ratio) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(node_count * ratio));
}

template <class Function> [[nodiscard]] double elapsed_nanoseconds(Function &&function) {
    const auto start = clock_type::now();
    std::forward<Function>(function)();
    const auto finish = clock_type::now();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count());
}

[[nodiscard]] auto make_runtime(std::size_t node_count) {
    authoring_scene authoring;
    std::ranges::for_each(std::views::iota(std::size_t{0}, node_count), [&](std::size_t index) {
        const auto inserted =
            authoring.insert_root(static_cast<std::uint32_t>(index), translation{});
        if (!inserted) {
            throw std::runtime_error("failed to create benchmark topology");
        }
    });
    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        throw std::runtime_error("failed to freeze benchmark topology");
    }
    return std::move(frozen).value();
}

[[nodiscard]] benchmark_result run_case(std::size_t node_count, double ratio) {
    auto runtime = make_runtime(node_count);
    scalar_motion_policy policy;
    const auto moving_count = active_count(node_count, ratio);
    const auto moving_indices = std::views::iota(std::size_t{0}, moving_count) |
                                std::views::transform([=](std::size_t index) {
                                    return (index * node_count) / moving_count;
                                });

    scene_polytree::motion::active_motion_set<double, double> sparse{runtime.topology()};
    const auto sparse_registration = elapsed_nanoseconds([&] {
        std::ranges::for_each(moving_indices, [&](std::size_t index) {
            const auto status = sparse.set(static_cast<wz::core::graph::NodeHandle>(index),
                                           state{1.0, 0.25}, policy);
            if (status != scene_polytree::motion::motion_error::none) {
                throw std::runtime_error("failed to register sparse motion state");
            }
        });
    });

    std::vector<state> dense_states;
    std::vector<std::uint8_t> dense_active;
    const auto dense_registration = elapsed_nanoseconds([&] {
        dense_states.resize(node_count);
        dense_active.resize(node_count);
        std::ranges::for_each(moving_indices, [&](std::size_t index) {
            dense_states[index] = {1.0, 0.25};
            dense_active[index] = 1;
        });
    });

    const auto repetitions = std::max<std::size_t>(16, 2'000'000 / node_count);
    double sparse_checksum = 0.0;
    const auto sparse_ticks = elapsed_nanoseconds([&] {
        std::ranges::for_each(std::views::iota(std::size_t{0}, repetitions), [&](std::size_t) {
            sparse_checksum += std::transform_reduce(
                sparse.records().begin(), sparse.records().end(), 0.0, std::plus{},
                [](const auto &record) {
                    return record.state.linear_velocity + record.state.angular_velocity;
                });
        });
    });

    double dense_checksum = 0.0;
    const auto dense_ticks = elapsed_nanoseconds([&] {
        std::ranges::for_each(std::views::iota(std::size_t{0}, repetitions), [&](std::size_t) {
            dense_checksum +=
                std::transform_reduce(std::views::iota(std::size_t{0}, node_count).begin(),
                                      std::views::iota(std::size_t{0}, node_count).end(), 0.0,
                                      std::plus{}, [&](std::size_t index) {
                                          return dense_active[index] == 0
                                                     ? 0.0
                                                     : dense_states[index].linear_velocity +
                                                           dense_states[index].angular_velocity;
                                      });
        });
    });

    return {node_count,
            ratio,
            moving_count,
            repetitions,
            sparse.storage_capacity_bytes(),
            dense_states.capacity() * sizeof(state) +
                dense_active.capacity() * sizeof(std::uint8_t),
            sparse_registration,
            dense_registration,
            sparse_ticks / static_cast<double>(repetitions),
            dense_ticks / static_cast<double>(repetitions),
            sparse_checksum + dense_checksum};
}

void print_result(const benchmark_result &result) {
    std::cout << result.node_count << ',' << std::fixed << std::setprecision(3)
              << result.active_ratio * 100.0 << ',' << result.active_count << ','
              << result.repetitions << ',' << result.sparse_storage_bytes << ','
              << result.dense_storage_bytes << ',' << std::setprecision(1)
              << result.sparse_registration_ns << ',' << result.dense_registration_ns << ','
              << result.sparse_tick_ns << ',' << result.dense_tick_ns << ',' << result.checksum
              << '\n';
}
} // namespace

int main() {
    constexpr std::array node_counts{std::size_t{1'000}, std::size_t{10'000}, std::size_t{100'000}};
    constexpr std::array active_ratios{0.001, 0.01, 0.1, 1.0};

    std::cout << "# hierarchy_shape=flat_forest\n"
                 "# execution_policy=sequential_transform_reduce\n"
                 "# allocation_metric=retained_payload_capacity_bytes\n"
                 "# changed_node_ratio=not_applicable_storage_scan\n"
                 "node_count,active_percent,active_count,repetitions,sparse_bytes,dense_bytes,"
                 "sparse_registration_ns,dense_registration_ns,sparse_tick_ns,dense_tick_ns,"
                 "checksum\n";
    std::ranges::for_each(node_counts, [&](std::size_t node_count) {
        std::ranges::for_each(active_ratios,
                              [&](double ratio) { print_result(run_case(node_count, ratio)); });
    });
}
