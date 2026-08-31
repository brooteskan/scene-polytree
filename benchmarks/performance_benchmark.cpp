#include "tank_example.hpp"

#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_polytree.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef SCENE_POLYTREE_BENCHMARK_GIT_REVISION
#define SCENE_POLYTREE_BENCHMARK_GIT_REVISION "unknown"
#endif
#ifndef SCENE_POLYTREE_BENCHMARK_GIT_DIRTY
#define SCENE_POLYTREE_BENCHMARK_GIT_DIRTY "unknown"
#endif
#ifndef SCENE_POLYTREE_BENCHMARK_PROJECT_VERSION
#define SCENE_POLYTREE_BENCHMARK_PROJECT_VERSION "unknown"
#endif
#ifndef SCENE_POLYTREE_BENCHMARK_COMPILER_ID
#define SCENE_POLYTREE_BENCHMARK_COMPILER_ID "unknown"
#endif
#ifndef SCENE_POLYTREE_BENCHMARK_COMPILER_VERSION
#define SCENE_POLYTREE_BENCHMARK_COMPILER_VERSION "unknown"
#endif
#ifndef SCENE_POLYTREE_BENCHMARK_SYSTEM_NAME
#define SCENE_POLYTREE_BENCHMARK_SYSTEM_NAME "unknown"
#endif
#ifndef SCENE_POLYTREE_BENCHMARK_SYSTEM_PROCESSOR
#define SCENE_POLYTREE_BENCHMARK_SYSTEM_PROCESSOR "unknown"
#endif
#ifndef SCENE_POLYTREE_BENCHMARK_BUILD_CONFIG
#define SCENE_POLYTREE_BENCHMARK_BUILD_CONFIG "unknown"
#endif

namespace benchmark_alloc {
struct allocation_header {
    void *base{};
    std::size_t size{};
};

struct probe_counters {
    std::size_t allocation_count{};
    std::size_t allocated_bytes{};
    std::size_t peak_absolute_live_bytes{};
};

constinit std::atomic<std::size_t> live_bytes{};
thread_local probe_counters *active_probe{};

void note_peak(std::size_t value) noexcept {
    if (active_probe != nullptr) {
        active_probe->peak_absolute_live_bytes =
            std::max(active_probe->peak_absolute_live_bytes, value);
    }
}

[[nodiscard]] void *allocate(std::size_t requested, std::size_t alignment) {
    const auto size = std::max<std::size_t>(requested, 1);
    alignment = std::max(alignment, alignof(std::max_align_t));
    const auto total = size + alignment - 1 + sizeof(allocation_header);
    void *base = std::malloc(total);
    if (base == nullptr) {
        throw std::bad_alloc{};
    }
    const auto start = reinterpret_cast<std::uintptr_t>(base) + sizeof(allocation_header);
    const auto aligned = (start + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
    auto *header = reinterpret_cast<allocation_header *>(aligned) - 1;
    header->base = base;
    header->size = size;
    const auto current = live_bytes.fetch_add(size, std::memory_order_relaxed) + size;
    if (active_probe != nullptr) {
        ++active_probe->allocation_count;
        active_probe->allocated_bytes += size;
        note_peak(current);
    }
    return reinterpret_cast<void *>(aligned);
}

void deallocate(void *pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    const auto *header = reinterpret_cast<const allocation_header *>(pointer) - 1;
    live_bytes.fetch_sub(header->size, std::memory_order_relaxed);
    std::free(header->base);
}
} // namespace benchmark_alloc

void *operator new(std::size_t size) { return benchmark_alloc::allocate(size, alignof(std::max_align_t)); }
void *operator new[](std::size_t size) {
    return benchmark_alloc::allocate(size, alignof(std::max_align_t));
}
void *operator new(std::size_t size, std::align_val_t alignment) {
    return benchmark_alloc::allocate(size, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
    return benchmark_alloc::allocate(size, static_cast<std::size_t>(alignment));
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t &) noexcept {
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t &) noexcept {
    try {
        return ::operator new[](size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void *pointer) noexcept { benchmark_alloc::deallocate(pointer); }
void operator delete[](void *pointer) noexcept { benchmark_alloc::deallocate(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { benchmark_alloc::deallocate(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete(void *pointer, std::align_val_t) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete[](void *pointer, std::align_val_t) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete(void *pointer, const std::nothrow_t &) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete[](void *pointer, const std::nothrow_t &) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete(void *pointer, std::align_val_t, const std::nothrow_t &) noexcept {
    benchmark_alloc::deallocate(pointer);
}
void operator delete[](void *pointer, std::align_val_t, const std::nothrow_t &) noexcept {
    benchmark_alloc::deallocate(pointer);
}

namespace {
namespace tank = scene_polytree::examples::tank;
using clock_type = std::chrono::steady_clock;
using synthetic_authoring = scene_polytree::basic_authoring_scene<
    std::uint32_t, std::uint32_t, tank::rigid_pose>;
using synthetic_runtime = scene_polytree::basic_runtime_scene<
    std::uint32_t, std::uint32_t, tank::rigid_pose>;

struct options {
    bool smoke{};
    std::string suite{"all"};
    std::size_t samples{};
    std::uint64_t seed{0x5ce9'0009ULL};
};

struct timing_sample {
    double wall_ns{};
    std::size_t allocation_count{};
    std::size_t allocated_bytes{};
    std::size_t peak_live_bytes{};
    std::int64_t live_delta_bytes{};
};

struct topology_metadata {
    std::size_t node_count{};
    std::size_t edge_count{};
    std::size_t root_count{};
    std::size_t depth{};
    std::size_t dependency_levels{};
    std::size_t maximum_level_width{};
    std::size_t topology_payload_bytes{};
};

struct measurement {
    std::string_view suite;
    std::string_view phase;
    std::string_view shape;
    std::string_view propagation_order{"not_applicable"};
    topology_metadata topology;
    std::size_t actor_count{};
    std::size_t active_count{};
    std::size_t dirty_count{};
    std::size_t changed_count{};
    std::size_t integrated_count{};
    double requested_ratio{};
    double actual_changed_ratio{};
    std::size_t sample_index{};
    std::size_t iterations{1};
    timing_sample timing;
    std::size_t retained_bytes{};
    std::size_t scratch_bytes{};
    double checksum{};
    bool correct{true};
};

template <class Function> [[nodiscard]] timing_sample measure(Function &&function) {
    benchmark_alloc::probe_counters counters;
    const auto initial_live = benchmark_alloc::live_bytes.load(std::memory_order_relaxed);
    counters.peak_absolute_live_bytes = initial_live;
    benchmark_alloc::active_probe = &counters;
    const auto start = clock_type::now();
    try {
        std::forward<Function>(function)();
    } catch (...) {
        benchmark_alloc::active_probe = nullptr;
        throw;
    }
    const auto finish = clock_type::now();
    benchmark_alloc::active_probe = nullptr;
    const auto final_live = benchmark_alloc::live_bytes.load(std::memory_order_relaxed);
    return {
        std::chrono::duration<double, std::nano>(finish - start).count(),
        counters.allocation_count,
        counters.allocated_bytes,
        counters.peak_absolute_live_bytes - initial_live,
        static_cast<std::int64_t>(final_live) - static_cast<std::int64_t>(initial_live),
    };
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    std::ranges::for_each(value, [&](char character) {
        switch (character) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result.push_back(character);
            break;
        }
    });
    result.push_back('"');
    return result;
}

void emit_metadata(const options &configuration) {
    const char *processor = std::getenv("PROCESSOR_IDENTIFIER");
    std::cout << "{\"schema_version\":1,\"record_type\":\"metadata\""
              << ",\"project_version\":" << json_escape(SCENE_POLYTREE_BENCHMARK_PROJECT_VERSION)
              << ",\"git_revision\":" << json_escape(SCENE_POLYTREE_BENCHMARK_GIT_REVISION)
              << ",\"git_dirty\":" << SCENE_POLYTREE_BENCHMARK_GIT_DIRTY
              << ",\"system\":" << json_escape(SCENE_POLYTREE_BENCHMARK_SYSTEM_NAME)
              << ",\"architecture\":"
              << json_escape(SCENE_POLYTREE_BENCHMARK_SYSTEM_PROCESSOR)
              << ",\"processor\":" << json_escape(processor == nullptr ? "unknown" : processor)
              << ",\"hardware_threads\":" << std::thread::hardware_concurrency()
              << ",\"compiler\":" << json_escape(SCENE_POLYTREE_BENCHMARK_COMPILER_ID)
              << ",\"compiler_version\":"
              << json_escape(SCENE_POLYTREE_BENCHMARK_COMPILER_VERSION)
              << ",\"build_config\":" << json_escape(SCENE_POLYTREE_BENCHMARK_BUILD_CONFIG)
              << ",\"timer\":\"std::chrono::steady_clock\""
              << ",\"preset\":" << json_escape(configuration.smoke ? "smoke" : "full")
              << ",\"suite\":" << json_escape(configuration.suite)
              << ",\"samples\":" << configuration.samples
              << ",\"seed\":" << configuration.seed << "}\n";
}

void emit_measurement(const measurement &record) {
    std::cout << std::setprecision(17)
              << "{\"schema_version\":1,\"record_type\":\"measurement\""
              << ",\"suite\":" << json_escape(record.suite)
              << ",\"phase\":" << json_escape(record.phase)
              << ",\"shape\":" << json_escape(record.shape)
              << ",\"propagation_order\":" << json_escape(record.propagation_order)
              << ",\"node_count\":" << record.topology.node_count
              << ",\"edge_count\":" << record.topology.edge_count
              << ",\"root_count\":" << record.topology.root_count
              << ",\"depth\":" << record.topology.depth
              << ",\"dependency_levels\":" << record.topology.dependency_levels
              << ",\"maximum_level_width\":" << record.topology.maximum_level_width
              << ",\"actor_count\":" << record.actor_count
              << ",\"active_count\":" << record.active_count
              << ",\"dirty_count\":" << record.dirty_count
              << ",\"changed_count\":" << record.changed_count
              << ",\"integrated_count\":" << record.integrated_count
              << ",\"requested_ratio\":" << record.requested_ratio
              << ",\"actual_changed_ratio\":" << record.actual_changed_ratio
              << ",\"sample_index\":" << record.sample_index
              << ",\"iterations\":" << record.iterations
              << ",\"wall_ns\":" << record.timing.wall_ns
              << ",\"allocation_count\":" << record.timing.allocation_count
              << ",\"allocated_bytes\":" << record.timing.allocated_bytes
              << ",\"peak_live_bytes\":" << record.timing.peak_live_bytes
              << ",\"live_delta_bytes\":" << record.timing.live_delta_bytes
              << ",\"retained_bytes\":" << record.retained_bytes
              << ",\"scratch_bytes\":" << record.scratch_bytes
              << ",\"topology_payload_bytes\":" << record.topology.topology_payload_bytes
              << ",\"checksum\":" << record.checksum
              << ",\"correct\":" << (record.correct ? "true" : "false") << "}\n";
}

[[nodiscard]] bool selected(std::string_view requested, std::string_view suite) {
    return requested == "all" || requested == suite;
}

[[nodiscard]] std::size_t ratio_count(std::size_t total, double ratio) {
    if (ratio <= 0.0 || total == 0) {
        return 0;
    }
    if (ratio >= 1.0) {
        return total;
    }
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(total * ratio)));
}

[[nodiscard]] tank::rigid_pose local_pose(std::size_t index) {
    const auto value = static_cast<double>(index % 97) * 0.001;
    return {{value, value * 0.5, value * 0.25}, {1.0, 0.0, 0.0, 0.0}};
}

struct synthetic_fixture {
    synthetic_authoring authoring;
    std::vector<wz::core::graph::StableNodeId> nodes;
};

[[nodiscard]] synthetic_fixture make_synthetic(std::string_view shape, std::size_t node_total) {
    synthetic_fixture fixture;
    fixture.nodes.reserve(node_total);
    if (node_total == 0) {
        return fixture;
    }

    if (shape == "chain") {
        auto current = fixture.authoring.insert_root(0u, local_pose(0)).value();
        fixture.nodes.push_back(current);
        std::ranges::for_each(std::views::iota(std::size_t{1}, node_total),
                              [&](std::size_t index) {
                                  current = fixture.authoring
                                                .insert_child(current,
                                                              static_cast<std::uint32_t>(index),
                                                              static_cast<std::uint32_t>(index),
                                                              local_pose(index))
                                                .value();
                                  fixture.nodes.push_back(current);
                              });
        return fixture;
    }

    if (shape == "wide") {
        const auto root = fixture.authoring.insert_root(0u, local_pose(0)).value();
        fixture.nodes.push_back(root);
        std::ranges::for_each(std::views::iota(std::size_t{1}, node_total),
                              [&](std::size_t index) {
                                  fixture.nodes.push_back(
                                      fixture.authoring
                                          .insert_child(root, static_cast<std::uint32_t>(index),
                                                        static_cast<std::uint32_t>(index),
                                                        local_pose(index))
                                          .value());
                              });
        return fixture;
    }

    const auto root_total = shape == "forest"
                                ? std::min(node_total, std::max<std::size_t>(2, node_total / 256))
                                : std::size_t{1};
    std::ranges::for_each(std::views::iota(std::size_t{0}, root_total),
                          [&](std::size_t index) {
                              fixture.nodes.push_back(
                                  fixture.authoring
                                      .insert_root(static_cast<std::uint32_t>(index),
                                                   local_pose(index))
                                      .value());
                          });
    constexpr std::size_t branching = 4;
    std::ranges::for_each(std::views::iota(root_total, node_total), [&](std::size_t index) {
        const auto parent_index = (index - root_total) / branching;
        fixture.nodes.push_back(
            fixture.authoring
                .insert_child(fixture.nodes[parent_index], static_cast<std::uint32_t>(index),
                              static_cast<std::uint32_t>(index), local_pose(index))
                .value());
    });
    return fixture;
}

template <class N, class E>
[[nodiscard]] topology_metadata
describe_topology(const wz::core::graph::Polytree<N, E> &topology) {
    const auto plan = wz::core::graph::evaluation_plan(topology);
    const auto level_indices = std::views::iota(std::size_t{}, plan.level_count());
    const auto widths = level_indices | std::views::transform(
                                             [&](std::size_t level) {
                                                 return plan.dependency_level(level).size();
                                             });
    const auto maximum = plan.level_count() == 0 ? std::size_t{}
                                                  : *std::ranges::max_element(widths);
    const auto payload = topology.node_data.size_bytes() + topology.out_offsets.size_bytes() +
                         topology.out_neighbors.size_bytes() +
                         topology.out_edge_data.size_bytes() + topology.parent.size_bytes() +
                         topology.parent_edge_data.size_bytes() + topology.topo_order.size_bytes() +
                         topology.reverse_topo_order.size_bytes() +
                         topology.root_order.size_bytes() +
                         topology.dependency_order.size_bytes() +
                         topology.dependency_level_offsets.size_bytes();
    return {
        wz::core::graph::node_count(topology),
        wz::core::graph::edge_count(topology),
        plan.roots.size(),
        plan.level_count() == 0 ? 0 : plan.level_count() - 1,
        plan.level_count(),
        maximum,
        payload,
    };
}

template <class Runtime> [[nodiscard]] std::size_t retained_bytes(const Runtime &runtime) {
    return describe_topology(runtime.topology()).topology_payload_bytes +
           runtime.state().records().size_bytes() + runtime.identities().storage_bytes();
}

[[nodiscard]] double pose_checksum(const tank::rigid_pose &pose) noexcept {
    return pose.translation.x + pose.translation.y * 3.0 + pose.translation.z * 7.0 +
           pose.rotation.w * 11.0 + pose.rotation.x * 13.0 + pose.rotation.y * 17.0 +
           pose.rotation.z * 19.0;
}

template <class Runtime> [[nodiscard]] double state_checksum(const Runtime &runtime) {
    return std::transform_reduce(
        runtime.state().records().begin(), runtime.state().records().end(), 0.0, std::plus{},
        [](const auto &record) { return pose_checksum(record.world); });
}

template <class Runtime> void initialize_world(Runtime &runtime, tank::rigid_policy &policy) {
    scene_polytree::transform_evaluation_workspace workspace;
    const auto plan = scene_polytree::make_transform_evaluation_plan(
        runtime.topology(), runtime.state(), workspace);
    if (!plan || !scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(),
                                                       plan.value(), policy)) {
        throw std::runtime_error("failed to initialize benchmark world transforms");
    }
}

template <class Runtime>
[[nodiscard]] std::size_t mark_dirty(Runtime &runtime, double ratio, bool root_pattern = false) {
    const auto count = ratio_count(runtime.state().size(), ratio);
    const auto first = root_pattern ? std::size_t{} : runtime.state().size() - count;
    const auto last = root_pattern ? count : runtime.state().size();
    std::ranges::for_each(std::views::iota(first, last), [&](std::size_t index) {
        if (runtime.state().mark_dirty(static_cast<wz::core::graph::NodeHandle>(index)) !=
            scene_polytree::transform_error::none) {
            throw std::runtime_error("failed to mark benchmark transform dirty");
        }
    });
    return count;
}

[[nodiscard]] std::vector<std::size_t> synthetic_sizes(const options &configuration) {
    return configuration.smoke ? std::vector<std::size_t>{64}
                               : std::vector<std::size_t>{1'000, 10'000, 100'000};
}

[[nodiscard]] std::vector<double> workload_ratios(const options &configuration) {
    return configuration.smoke ? std::vector<double>{0.0, 0.1, 1.0}
                               : std::vector<double>{0.0, 0.001, 0.1, 1.0};
}

[[nodiscard]] std::vector<std::size_t> actor_sizes(const options &configuration) {
    return configuration.smoke ? std::vector<std::size_t>{4}
                               : std::vector<std::size_t>{1, 32, 256, 1'024, 10'000};
}

[[nodiscard]] std::vector<double> actor_ratios(const options &configuration) {
    return configuration.smoke ? std::vector<double>{0.25, 1.0}
                               : std::vector<double>{0.0, 0.001, 0.1, 1.0};
}

void run_topology_case(std::string_view shape, std::size_t node_total,
                       const options &configuration) {
    std::ranges::for_each(std::views::iota(std::size_t{}, configuration.samples),
                          [&](std::size_t sample) {
                              auto fixture = make_synthetic(shape, node_total);
                              wz::core::graph::FreezeWorkspace workspace;
                              std::optional<synthetic_runtime> initial_runtime;
                              const auto initial_timing = measure([&] {
                                  auto frozen = scene_polytree::freeze_scene(fixture.authoring,
                                                                             workspace);
                                  if (!frozen) {
                                      throw std::runtime_error("initial freeze failed");
                                  }
                                  initial_runtime.emplace(std::move(frozen).value());
                              });
                              const auto topology = describe_topology(initial_runtime->topology());
                              const auto initial_scratch =
                                  initial_runtime->freeze_metrics().reusable_scratch_capacity_bytes;
                              emit_measurement({
                                  "topology",
                                  "initial_freeze",
                                  shape,
                                  "not_applicable",
                                  topology,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0.0,
                                  0.0,
                                  sample,
                                  1,
                                  initial_timing,
                                  retained_bytes(*initial_runtime),
                                  initial_scratch,
                                  static_cast<double>(initial_runtime->identities().source_revision()),
                                  true,
                              });

                              std::optional<synthetic_runtime> incremental_runtime;
                              const auto incremental_timing = measure([&] {
                                  auto frozen = scene_polytree::freeze_scene(
                                      fixture.authoring, workspace, *initial_runtime);
                                  if (!frozen) {
                                      throw std::runtime_error("incremental freeze failed");
                                  }
                                  incremental_runtime.emplace(std::move(frozen).value());
                              });
                              emit_measurement({
                                  "topology",
                                  "incremental_freeze_clean",
                                  shape,
                                  "not_applicable",
                                  topology,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0.0,
                                  0.0,
                                  sample,
                                  1,
                                  incremental_timing,
                                  retained_bytes(*incremental_runtime),
                                  incremental_runtime->freeze_metrics()
                                      .reusable_scratch_capacity_bytes,
                                  static_cast<double>(incremental_runtime->state().revision()),
                                  true,
                              });

                              if (fixture.nodes.size() > 2) {
                                  const auto moved = fixture.nodes.back();
                                  const auto old_parent =
                                      wz::core::graph::parent(fixture.authoring.topology(), moved);
                                  const auto new_parent = old_parent == fixture.nodes.front()
                                                              ? fixture.nodes[1]
                                                              : fixture.nodes.front();
                                  const auto mutation = fixture.authoring.reparent(
                                      moved, new_parent, static_cast<std::uint32_t>(node_total));
                                  if (!mutation) {
                                      throw std::runtime_error("benchmark reparent failed");
                                  }
                                  std::optional<synthetic_runtime> changed_runtime;
                                  const auto changed_timing = measure([&] {
                                      auto frozen = scene_polytree::freeze_scene(
                                          fixture.authoring, workspace, *incremental_runtime);
                                      if (!frozen) {
                                          throw std::runtime_error("changed freeze failed");
                                      }
                                      changed_runtime.emplace(std::move(frozen).value());
                                  });
                                  const auto changed_topology =
                                      describe_topology(changed_runtime->topology());
                                  emit_measurement({
                                      "topology",
                                      "incremental_freeze_reparent",
                                      shape,
                                      "not_applicable",
                                      changed_topology,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      1.0 / static_cast<double>(node_total),
                                      0.0,
                                      sample,
                                      1,
                                      changed_timing,
                                      retained_bytes(*changed_runtime),
                                      changed_runtime->freeze_metrics()
                                          .reusable_scratch_capacity_bytes,
                                      static_cast<double>(changed_runtime->state().revision()),
                                      true,
                                  });
                              }
                          });
}

void run_topology_suite(const options &configuration) {
    const std::array shapes{"chain", "wide", "balanced", "forest"};
    std::ranges::for_each(shapes, [&](std::string_view shape) {
        std::ranges::for_each(synthetic_sizes(configuration), [&](std::size_t node_total) {
            run_topology_case(shape, node_total, configuration);
        });
    });
}

template <class N, class E>
void evaluate_kernel_topological(const wz::core::graph::Polytree<N, E> &topology,
                                 const std::vector<tank::rigid_pose> &locals,
                                 std::vector<tank::rigid_pose> &worlds,
                                 const std::vector<std::uint8_t> &affected,
                                 tank::rigid_policy &policy) {
    std::ranges::for_each(wz::core::graph::evaluation_plan(topology).topological_order,
                          [&](wz::core::graph::NodeHandle node) {
                              if (affected[node] == 0) {
                                  return;
                              }
                              const auto parent = wz::core::graph::parent(topology, node);
                              worlds[node] = parent == wz::core::graph::INVALID_NODE
                                                 ? locals[node]
                                                 : policy.compose(worlds[parent], locals[node]);
                          });
}

template <class N, class E>
void evaluate_kernel_dependency_levels(const wz::core::graph::Polytree<N, E> &topology,
                                       const std::vector<tank::rigid_pose> &locals,
                                       std::vector<tank::rigid_pose> &worlds,
                                       const std::vector<std::uint8_t> &affected,
                                       tank::rigid_policy &policy) {
    const auto plan = wz::core::graph::evaluation_plan(topology);
    std::ranges::for_each(std::views::iota(std::size_t{}, plan.level_count()),
                          [&](std::size_t level) {
                              std::ranges::for_each(
                                  plan.dependency_level(level),
                                  [&](wz::core::graph::NodeHandle node) {
                                      if (affected[node] == 0) {
                                          return;
                                      }
                                      const auto parent = wz::core::graph::parent(topology, node);
                                      worlds[node] =
                                          parent == wz::core::graph::INVALID_NODE
                                              ? locals[node]
                                              : policy.compose(worlds[parent], locals[node]);
                                  });
                          });
}

void run_transform_case(std::string_view shape, std::size_t node_total, double dirty_ratio,
                        bool root_pattern, const options &configuration) {
    std::ranges::for_each(std::views::iota(std::size_t{}, configuration.samples),
                          [&](std::size_t sample) {
                              auto fixture = make_synthetic(shape, node_total);
                              wz::core::graph::FreezeWorkspace freeze_workspace;
                              auto frozen = scene_polytree::freeze_scene(fixture.authoring,
                                                                         freeze_workspace);
                              if (!frozen) {
                                  throw std::runtime_error("transform fixture freeze failed");
                              }
                              auto runtime = std::move(frozen).value();
                              tank::rigid_policy policy;
                              initialize_world(runtime, policy);
                              const auto directly_dirty =
                                  mark_dirty(runtime, dirty_ratio, root_pattern);
                              scene_polytree::transform_evaluation_workspace workspace;
                              scene_polytree::transform_evaluation_plan plan;
                              const auto plan_timing = measure([&] {
                                  const auto outcome =
                                      scene_polytree::make_transform_evaluation_plan(
                                          runtime.topology(), runtime.state(), workspace);
                                  if (!outcome) {
                                      throw std::runtime_error("transform planning failed");
                                  }
                                  plan = outcome.value();
                              });
                              const auto topology = describe_topology(runtime.topology());
                              const auto changed_count = plan.ordered_nodes.size();
                              const auto actual_ratio =
                                  static_cast<double>(changed_count) / topology.node_count;
                              emit_measurement({
                                  "transform",
                                  root_pattern ? "dirty_plan_root_cold" : "dirty_plan_cold",
                                  shape,
                                  "topological",
                                  topology,
                                  0,
                                  0,
                                  directly_dirty,
                                  changed_count,
                                  0,
                                  dirty_ratio,
                                  actual_ratio,
                                  sample,
                                  1,
                                  plan_timing,
                                  retained_bytes(runtime),
                                  workspace.scratch_capacity_bytes(),
                                  static_cast<double>(changed_count),
                                  true,
                              });

                              const auto warm_plan_timing = measure([&] {
                                  const auto outcome =
                                      scene_polytree::make_transform_evaluation_plan(
                                          runtime.topology(), runtime.state(), workspace);
                                  if (!outcome) {
                                      throw std::runtime_error("warm transform planning failed");
                                  }
                                  plan = outcome.value();
                              });
                              emit_measurement({
                                  "transform",
                                  root_pattern ? "dirty_plan_root_warm" : "dirty_plan_warm",
                                  shape,
                                  "topological",
                                  topology,
                                  0,
                                  0,
                                  directly_dirty,
                                  changed_count,
                                  0,
                                  dirty_ratio,
                                  actual_ratio,
                                  sample,
                                  1,
                                  warm_plan_timing,
                                  retained_bytes(runtime),
                                  workspace.scratch_capacity_bytes(),
                                  static_cast<double>(changed_count),
                                  plan.ordered_nodes.size() == changed_count,
                              });

                              std::vector<tank::rigid_pose> locals;
                              std::vector<tank::rigid_pose> topological_worlds;
                              locals.reserve(runtime.state().size());
                              topological_worlds.reserve(runtime.state().size());
                              std::ranges::for_each(runtime.state().records(), [&](const auto &record) {
                                  locals.push_back(record.local);
                                  topological_worlds.push_back(record.world);
                              });
                              auto dependency_worlds = topological_worlds;
                              std::vector<std::uint8_t> affected(runtime.state().size(), 0);
                              std::ranges::for_each(plan.ordered_nodes, [&](auto node) {
                                  affected[node] = 1;
                              });

                              const auto topological_timing = measure([&] {
                                  evaluate_kernel_topological(runtime.topology(), locals,
                                                              topological_worlds, affected, policy);
                              });
                              const auto dependency_timing = measure([&] {
                                  evaluate_kernel_dependency_levels(
                                      runtime.topology(), locals, dependency_worlds, affected, policy);
                              });
                              const bool kernel_match =
                                  std::ranges::equal(topological_worlds, dependency_worlds);
                              const auto kernel_checksum = std::transform_reduce(
                                  topological_worlds.begin(), topological_worlds.end(), 0.0,
                                  std::plus{}, pose_checksum);
                              emit_measurement({
                                  "transform",
                                  root_pattern ? "propagation_kernel_root" : "propagation_kernel",
                                  shape,
                                  "topological",
                                  topology,
                                  0,
                                  0,
                                  directly_dirty,
                                  changed_count,
                                  0,
                                  dirty_ratio,
                                  actual_ratio,
                                  sample,
                                  1,
                                  topological_timing,
                                  retained_bytes(runtime) +
                                      topological_worlds.capacity() * sizeof(tank::rigid_pose),
                                  affected.capacity() * sizeof(std::uint8_t),
                                  kernel_checksum,
                                  kernel_match,
                              });
                              emit_measurement({
                                  "transform",
                                  root_pattern ? "propagation_kernel_root" : "propagation_kernel",
                                  shape,
                                  "dependency_levels_sequential",
                                  topology,
                                  0,
                                  0,
                                  directly_dirty,
                                  changed_count,
                                  0,
                                  dirty_ratio,
                                  actual_ratio,
                                  sample,
                                  1,
                                  dependency_timing,
                                  retained_bytes(runtime) +
                                      dependency_worlds.capacity() * sizeof(tank::rigid_pose),
                                  affected.capacity() * sizeof(std::uint8_t),
                                  kernel_checksum,
                                  kernel_match,
                              });

                              scene_polytree::transform_evaluation_result evaluated;
                              const auto evaluation_timing = measure([&] {
                                  evaluated = scene_polytree::evaluate_transforms(
                                      runtime.topology(), runtime.state(), plan, policy);
                                  if (!evaluated) {
                                      throw std::runtime_error("transform evaluation failed");
                                  }
                              });
                              const auto indices =
                                  std::views::iota(std::size_t{}, runtime.state().size());
                              const bool production_match = std::ranges::all_of(
                                  indices, [&](std::size_t index) {
                                      return runtime.state().world(
                                                 static_cast<wz::core::graph::NodeHandle>(index)) ==
                                             topological_worlds[index];
                                  });
                              emit_measurement({
                                  "transform",
                                  root_pattern ? "evaluate_transforms_root" : "evaluate_transforms",
                                  shape,
                                  "topological",
                                  topology,
                                  0,
                                  0,
                                  directly_dirty,
                                  evaluated.changed_nodes.size(),
                                  0,
                                  dirty_ratio,
                                  actual_ratio,
                                  sample,
                                  1,
                                  evaluation_timing,
                                  retained_bytes(runtime),
                                  workspace.scratch_capacity_bytes(),
                                  state_checksum(runtime),
                                  production_match && evaluated.changed_nodes.size() == changed_count,
                              });
                          });
}

void run_transform_suite(const options &configuration) {
    const std::array shapes{"chain", "wide", "balanced", "forest"};
    std::ranges::for_each(shapes, [&](std::string_view shape) {
        std::ranges::for_each(synthetic_sizes(configuration), [&](std::size_t node_total) {
            std::ranges::for_each(workload_ratios(configuration), [&](double ratio) {
                run_transform_case(shape, node_total, ratio, false, configuration);
            });
            run_transform_case(shape, node_total, 1.0 / static_cast<double>(node_total), true,
                               configuration);
        });
    });
}

struct tank_fixture {
    tank::authoring_scene authoring;
    std::vector<tank::tank_instance> stable;
};

[[nodiscard]] tank_fixture make_tanks(std::size_t actor_total) {
    tank_fixture fixture;
    fixture.stable.reserve(actor_total);
    const tank::tank_asset asset;
    std::ranges::for_each(std::views::iota(std::size_t{}, actor_total),
                          [&](std::size_t index) {
                              fixture.stable.push_back(tank::instantiate_tank(
                                  fixture.authoring, asset,
                                  {{static_cast<double>(index) * 4.0, 0.0, 0.0}, {}}));
                          });
    return fixture;
}

struct runtime_tanks {
    tank::runtime_scene runtime;
    std::vector<tank::runtime_tank_instance> instances;
};

[[nodiscard]] runtime_tanks freeze_tanks(tank_fixture &fixture) {
    wz::core::graph::FreezeWorkspace workspace;
    auto frozen = scene_polytree::freeze_scene(fixture.authoring, workspace);
    if (!frozen) {
        throw std::runtime_error("tank fixture freeze failed");
    }
    auto runtime = std::move(frozen).value();
    std::vector<tank::runtime_tank_instance> instances;
    instances.reserve(fixture.stable.size());
    std::ranges::transform(fixture.stable, std::back_inserter(instances),
                           [&](tank::tank_instance instance) {
                               return tank::resolve_tank(runtime, instance);
                           });
    return {std::move(runtime), std::move(instances)};
}

void apply_actor_intent(tank::active_set &active,
                        std::span<const tank::runtime_tank_instance> instances,
                        std::size_t active_actors, const tank::tank_intent &intent,
                        tank::rigid_policy &policy) {
    std::ranges::for_each(instances.first(active_actors), [&](const auto &instance) {
        if (tank::apply_intent(active, instance, intent, policy) !=
            scene_polytree::motion::motion_error::none) {
            throw std::runtime_error("tank intent update failed");
        }
    });
}

void apply_actor_intent_order(tank::active_set &active,
                              std::span<const tank::runtime_tank_instance> instances,
                              std::span<const std::size_t> order,
                              const tank::tank_intent &intent, tank::rigid_policy &policy) {
    std::ranges::for_each(order, [&](std::size_t index) {
        if (tank::apply_intent(active, instances[index], intent, policy) !=
            scene_polytree::motion::motion_error::none) {
            throw std::runtime_error("ordered tank intent update failed");
        }
    });
}

void run_motion_case(std::size_t actor_total, double active_ratio,
                     const options &configuration) {
    std::ranges::for_each(std::views::iota(std::size_t{}, configuration.samples),
                          [&](std::size_t sample) {
                              auto fixture = make_tanks(actor_total);
                              auto frozen = freeze_tanks(fixture);
                              tank::rigid_policy policy;
                              initialize_world(frozen.runtime, policy);
                              const auto active_actors = ratio_count(actor_total, active_ratio);
                              tank::active_set active{frozen.runtime.topology()};
                              tank::active_set reverse_active{frozen.runtime.topology()};
                              tank::active_set shuffled_active{frozen.runtime.topology()};
                              const tank::tank_intent moving{2.0, 0.25, -0.5, 0.125};
                              const auto topology = describe_topology(frozen.runtime.topology());
                              std::vector<std::size_t> ascending_order(active_actors);
                              std::iota(ascending_order.begin(), ascending_order.end(), 0);
                              auto descending_order = ascending_order;
                              std::ranges::reverse(descending_order);
                              auto shuffled_order = ascending_order;
                              std::mt19937_64 random{
                                  configuration.seed ^ static_cast<std::uint64_t>(actor_total) ^
                                  static_cast<std::uint64_t>(sample)};
                              std::ranges::shuffle(shuffled_order, random);

                              const auto activation_timing = measure([&] {
                                  apply_actor_intent(active, frozen.instances, active_actors,
                                                     moving, policy);
                              });
                              emit_measurement({
                                  "motion",
                                  "active_set_activate",
                                  "articulated_tank_forest",
                                  "input_actor_order_ascending",
                                  topology,
                                  actor_total,
                                  active.size(),
                                  0,
                                  0,
                                  0,
                                  active_ratio,
                                  0.0,
                                  sample,
                                  1,
                                  activation_timing,
                                  retained_bytes(frozen.runtime) + active.storage_capacity_bytes(),
                                  0,
                                  static_cast<double>(active.size()),
                                  active.size() == active_actors * 3,
                              });

                              const auto reverse_activation_timing = measure([&] {
                                  apply_actor_intent_order(reverse_active, frozen.instances,
                                                           descending_order, moving, policy);
                              });
                              emit_measurement({
                                  "motion",
                                  "active_set_activate_reverse",
                                  "articulated_tank_forest",
                                  "input_actor_order_descending",
                                  topology,
                                  actor_total,
                                  reverse_active.size(),
                                  0,
                                  0,
                                  0,
                                  active_ratio,
                                  0.0,
                                  sample,
                                  1,
                                  reverse_activation_timing,
                                  retained_bytes(frozen.runtime) +
                                      reverse_active.storage_capacity_bytes(),
                                  0,
                                  static_cast<double>(reverse_active.size()),
                                  reverse_active.size() == active_actors * 3,
                              });

                              const auto shuffled_activation_timing = measure([&] {
                                  apply_actor_intent_order(shuffled_active, frozen.instances,
                                                           shuffled_order, moving, policy);
                              });
                              emit_measurement({
                                  "motion",
                                  "active_set_activate_shuffled",
                                  "articulated_tank_forest",
                                  "input_actor_order_seeded_shuffle",
                                  topology,
                                  actor_total,
                                  shuffled_active.size(),
                                  0,
                                  0,
                                  0,
                                  active_ratio,
                                  0.0,
                                  sample,
                                  1,
                                  shuffled_activation_timing,
                                  retained_bytes(frozen.runtime) +
                                      shuffled_active.storage_capacity_bytes(),
                                  0,
                                  static_cast<double>(shuffled_active.size()),
                                  shuffled_active.size() == active_actors * 3,
                              });

                              const auto churn_actor_count =
                                  active_actors == 0 ? std::size_t{}
                                                     : std::max<std::size_t>(1, active_actors / 10);
                              const auto churn_order = std::span<const std::size_t>{shuffled_order}
                                                           .first(churn_actor_count);
                              const auto churn_timing = measure([&] {
                                  apply_actor_intent_order(shuffled_active, frozen.instances,
                                                           churn_order, {}, policy);
                                  apply_actor_intent_order(shuffled_active, frozen.instances,
                                                           churn_order, moving, policy);
                              });
                              emit_measurement({
                                  "motion",
                                  "active_set_churn_10_percent",
                                  "articulated_tank_forest",
                                  "input_actor_order_seeded_shuffle",
                                  topology,
                                  actor_total,
                                  shuffled_active.size(),
                                  0,
                                  0,
                                  0,
                                  active_ratio,
                                  0.0,
                                  sample,
                                  1,
                                  churn_timing,
                                  retained_bytes(frozen.runtime) +
                                      shuffled_active.storage_capacity_bytes(),
                                  0,
                                  static_cast<double>(shuffled_active.size()),
                                  shuffled_active.size() == active_actors * 3,
                              });

                              const tank::tank_intent changed{3.0, -0.5, 0.75, -0.25};
                              const auto update_timing = measure([&] {
                                  apply_actor_intent(active, frozen.instances, active_actors,
                                                     changed, policy);
                              });
                              emit_measurement({
                                  "motion",
                                  "active_set_update",
                                  "articulated_tank_forest",
                                  "node_handle_order",
                                  topology,
                                  actor_total,
                                  active.size(),
                                  0,
                                  0,
                                  0,
                                  active_ratio,
                                  0.0,
                                  sample,
                                  1,
                                  update_timing,
                                  retained_bytes(frozen.runtime) + active.storage_capacity_bytes(),
                                  0,
                                  static_cast<double>(active.size()),
                                  active.size() == active_actors * 3,
                              });

                              scene_polytree::motion::fixed_step_sequence sequence{
                                  std::chrono::nanoseconds{16'666'667}};
                              scene_polytree::motion::motion_evaluation_workspace<tank::rigid_pose>
                                  motion_workspace;
                              scene_polytree::transform_evaluation_workspace transform_workspace;
                              scene_polytree::motion::motion_evaluation_result result;
                              const auto advance_timing = measure([&] {
                                  result = scene_polytree::motion::advance_motion_scene(
                                      frozen.runtime.topology(), frozen.runtime.state(), active,
                                      sequence, motion_workspace, transform_workspace, policy,
                                      policy);
                                  if (!result) {
                                      throw std::runtime_error("motion advancement failed");
                                  }
                              });
                              const auto changed_ratio = static_cast<double>(
                                                             result.changed_nodes.size()) /
                                                         topology.node_count;
                              emit_measurement({
                                  "motion",
                                  "advance_motion_scene_cold",
                                  "articulated_tank_forest",
                                  "node_handle_order_then_topological",
                                  topology,
                                  actor_total,
                                  active.size(),
                                  0,
                                  result.changed_nodes.size(),
                                  result.integrated_nodes.size(),
                                  active_ratio,
                                  changed_ratio,
                                  sample,
                                  1,
                                  advance_timing,
                                  retained_bytes(frozen.runtime) + active.storage_capacity_bytes(),
                                  motion_workspace.scratch_capacity_bytes() +
                                      transform_workspace.scratch_capacity_bytes(),
                                  state_checksum(frozen.runtime),
                                  result.integrated_nodes.size() == active.size(),
                              });

                              const auto warm_advance_timing = measure([&] {
                                  result = scene_polytree::motion::advance_motion_scene(
                                      frozen.runtime.topology(), frozen.runtime.state(), active,
                                      sequence, motion_workspace, transform_workspace, policy,
                                      policy);
                                  if (!result) {
                                      throw std::runtime_error("warm motion advancement failed");
                                  }
                              });
                              const auto warm_changed_ratio = static_cast<double>(
                                                                  result.changed_nodes.size()) /
                                                              topology.node_count;
                              emit_measurement({
                                  "motion",
                                  "advance_motion_scene_warm",
                                  "articulated_tank_forest",
                                  "node_handle_order_then_topological",
                                  topology,
                                  actor_total,
                                  active.size(),
                                  0,
                                  result.changed_nodes.size(),
                                  result.integrated_nodes.size(),
                                  active_ratio,
                                  warm_changed_ratio,
                                  sample,
                                  1,
                                  warm_advance_timing,
                                  retained_bytes(frozen.runtime) + active.storage_capacity_bytes(),
                                  motion_workspace.scratch_capacity_bytes() +
                                      transform_workspace.scratch_capacity_bytes(),
                                  state_checksum(frozen.runtime),
                                  result.integrated_nodes.size() == active.size() &&
                                      warm_advance_timing.allocation_count == 0,
                              });

                              const auto deactivation_timing = measure([&] {
                                  apply_actor_intent(active, frozen.instances, active_actors, {},
                                                     policy);
                              });
                              emit_measurement({
                                  "motion",
                                  "active_set_deactivate",
                                  "articulated_tank_forest",
                                  "node_handle_order",
                                  topology,
                                  actor_total,
                                  active.size(),
                                  0,
                                  0,
                                  0,
                                  active_ratio,
                                  0.0,
                                  sample,
                                  1,
                                  deactivation_timing,
                                  retained_bytes(frozen.runtime) + active.storage_capacity_bytes(),
                                  0,
                                  static_cast<double>(active.size()),
                                  active.empty(),
                              });

                              const auto reverse_deactivation_timing = measure([&] {
                                  apply_actor_intent_order(reverse_active, frozen.instances,
                                                           descending_order, {}, policy);
                              });
                              emit_measurement({
                                  "motion",
                                  "active_set_deactivate_reverse",
                                  "articulated_tank_forest",
                                  "input_actor_order_descending",
                                  topology,
                                  actor_total,
                                  reverse_active.size(),
                                  0,
                                  0,
                                  0,
                                  active_ratio,
                                  0.0,
                                  sample,
                                  1,
                                  reverse_deactivation_timing,
                                  retained_bytes(frozen.runtime) +
                                      reverse_active.storage_capacity_bytes(),
                                  0,
                                  static_cast<double>(reverse_active.size()),
                                  reverse_active.empty(),
                              });
                          });
}

void run_motion_suite(const options &configuration) {
    std::ranges::for_each(actor_sizes(configuration), [&](std::size_t actors) {
        std::ranges::for_each(actor_ratios(configuration), [&](double ratio) {
            run_motion_case(actors, ratio, configuration);
        });
    });
}

void write_changed(const tank::runtime_scene &runtime,
                   std::span<const wz::core::graph::NodeHandle> changed,
                   std::vector<tank::rigid_pose> &targets, tank::rigid_policy &policy) {
    const tank::rigid_pose offset{{0.1, 0.2, 0.3}, {}};
    std::ranges::for_each(changed, [&](wz::core::graph::NodeHandle node) {
        targets[node] = policy.compose(runtime.state().world(node), offset);
    });
}

void run_synchronization_case(std::size_t actor_total, double changed_request,
                              const options &configuration) {
    std::ranges::for_each(std::views::iota(std::size_t{}, configuration.samples),
                          [&](std::size_t sample) {
                              auto fixture = make_tanks(actor_total);
                              auto frozen = freeze_tanks(fixture);
                              tank::rigid_policy policy;
                              initialize_world(frozen.runtime, policy);
                              const auto token = frozen.runtime.state().revision();
                              const auto directly_dirty = mark_dirty(frozen.runtime, changed_request);
                              scene_polytree::transform_evaluation_workspace transform_workspace;
                              const auto plan = scene_polytree::make_transform_evaluation_plan(
                                  frozen.runtime.topology(), frozen.runtime.state(),
                                  transform_workspace);
                              if (!plan) {
                                  throw std::runtime_error("synchronization plan failed");
                              }
                              const auto evaluated = scene_polytree::evaluate_transforms(
                                  frozen.runtime.topology(), frozen.runtime.state(), plan.value(),
                                  policy);
                              if (!evaluated) {
                                  throw std::runtime_error("synchronization evaluation failed");
                              }
                              const auto topology = describe_topology(frozen.runtime.topology());
                              const auto actual_ratio =
                                  static_cast<double>(evaluated.changed_nodes.size()) /
                                  topology.node_count;
                              std::vector<wz::core::graph::NodeHandle> changed_scratch;
                              std::span<const wz::core::graph::NodeHandle> changed;
                              const auto selection_timing = measure([&] {
                                  changed = scene_polytree::changed_transform_nodes_since(
                                      frozen.runtime.topology(), frozen.runtime.state(), token,
                                      changed_scratch);
                              });
                              const bool selected_correctly =
                                  std::ranges::equal(changed, evaluated.changed_nodes);
                              emit_measurement({
                                  "synchronization",
                                  "changed_node_selection",
                                  "articulated_tank_forest",
                                  "topological_filter",
                                  topology,
                                  actor_total,
                                  0,
                                  directly_dirty,
                                  changed.size(),
                                  0,
                                  changed_request,
                                  actual_ratio,
                                  sample,
                                  1,
                                  selection_timing,
                                  retained_bytes(frozen.runtime),
                                  changed_scratch.capacity() *
                                      sizeof(wz::core::graph::NodeHandle),
                                  static_cast<double>(changed.size()),
                                  selected_correctly,
                              });

                              std::vector<tank::rigid_pose> targets(topology.node_count);
                              const auto write_timing = measure([&] {
                                  write_changed(frozen.runtime, changed, targets, policy);
                              });
                              const auto target_checksum = std::transform_reduce(
                                  targets.begin(), targets.end(), 0.0, std::plus{}, pose_checksum);
                              emit_measurement({
                                  "synchronization",
                                  "target_write",
                                  "articulated_tank_forest",
                                  "changed_node_order",
                                  topology,
                                  actor_total,
                                  0,
                                  directly_dirty,
                                  changed.size(),
                                  0,
                                  changed_request,
                                  actual_ratio,
                                  sample,
                                  1,
                                  write_timing,
                                  retained_bytes(frozen.runtime) +
                                      targets.capacity() * sizeof(tank::rigid_pose),
                                  changed_scratch.capacity() *
                                      sizeof(wz::core::graph::NodeHandle),
                                  target_checksum,
                                  selected_correctly,
                              });

                              changed_scratch.clear();
                              const auto combined_timing = measure([&] {
                                  changed = scene_polytree::changed_transform_nodes_since(
                                      frozen.runtime.topology(), frozen.runtime.state(), token,
                                      changed_scratch);
                                  write_changed(frozen.runtime, changed, targets, policy);
                              });
                              emit_measurement({
                                  "synchronization",
                                  "select_and_write",
                                  "articulated_tank_forest",
                                  "topological_filter_then_write",
                                  topology,
                                  actor_total,
                                  0,
                                  directly_dirty,
                                  changed.size(),
                                  0,
                                  changed_request,
                                  actual_ratio,
                                  sample,
                                  1,
                                  combined_timing,
                                  retained_bytes(frozen.runtime) +
                                      targets.capacity() * sizeof(tank::rigid_pose),
                                  changed_scratch.capacity() *
                                      sizeof(wz::core::graph::NodeHandle),
                                  target_checksum,
                                  selected_correctly &&
                                      changed.size() == evaluated.changed_nodes.size(),
                              });
                          });
}

void run_synchronization_suite(const options &configuration) {
    std::ranges::for_each(actor_sizes(configuration), [&](std::size_t actors) {
        std::ranges::for_each(workload_ratios(configuration), [&](double ratio) {
            run_synchronization_case(actors, ratio, configuration);
        });
    });
}

[[nodiscard]] bool parse_unsigned(std::string_view value, std::uint64_t &output) {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

[[nodiscard]] options parse_options(int argc, char **argv) {
    options result;
    const auto arguments = std::views::iota(1, argc) |
                           std::views::transform([&](int index) {
                               return std::string_view(argv[index]);
                           });
    std::ranges::for_each(arguments, [&](std::string_view argument) {
        constexpr std::string_view preset_prefix{"--preset="};
        constexpr std::string_view suite_prefix{"--suite="};
        constexpr std::string_view samples_prefix{"--samples="};
        constexpr std::string_view seed_prefix{"--seed="};
        if (argument.starts_with(preset_prefix)) {
            const auto value = argument.substr(preset_prefix.size());
            if (value != "smoke" && value != "full") {
                throw std::runtime_error("preset must be smoke or full");
            }
            result.smoke = value == "smoke";
        } else if (argument.starts_with(suite_prefix)) {
            result.suite = argument.substr(suite_prefix.size());
            if (result.suite != "all" && result.suite != "topology" &&
                result.suite != "transform" && result.suite != "motion" &&
                result.suite != "synchronization") {
                throw std::runtime_error("unknown benchmark suite");
            }
        } else if (argument.starts_with(samples_prefix)) {
            std::uint64_t value{};
            if (!parse_unsigned(argument.substr(samples_prefix.size()), value) || value == 0) {
                throw std::runtime_error("samples must be a positive integer");
            }
            result.samples = static_cast<std::size_t>(value);
        } else if (argument.starts_with(seed_prefix)) {
            if (!parse_unsigned(argument.substr(seed_prefix.size()), result.seed)) {
                throw std::runtime_error("seed must be an unsigned integer");
            }
        } else {
            throw std::runtime_error("unknown benchmark argument");
        }
    });
    if (result.samples == 0) {
        result.samples = result.smoke ? 1 : 5;
    }
    return result;
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto configuration = parse_options(argc, argv);
        emit_metadata(configuration);
        if (selected(configuration.suite, "topology")) {
            run_topology_suite(configuration);
        }
        if (selected(configuration.suite, "transform")) {
            run_transform_suite(configuration);
        }
        if (selected(configuration.suite, "motion")) {
            run_motion_suite(configuration);
        }
        if (selected(configuration.suite, "synchronization")) {
            run_synchronization_suite(configuration);
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "scene-polytree benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
