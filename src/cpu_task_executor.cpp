#include <scene_polytree/cpu_task_executor.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>

namespace scene_polytree {
class cpu_task_executor::implementation {
  public:
    explicit implementation(std::size_t requested_workers) {
        const auto hardware = std::max(1u, std::thread::hardware_concurrency());
        const auto automatic = static_cast<std::size_t>(std::min(15u, hardware - 1u));
        const auto count = requested_workers == 0 ? automatic : requested_workers;
        m_workers.reserve(count);
        std::ranges::for_each(std::views::iota(std::size_t{}, count), [&](std::size_t) {
            m_workers.emplace_back([this] { worker_main(); });
        });
    }

    ~implementation() {
        {
            const std::lock_guard lock{m_mutex};
            m_stopping = true;
            ++m_generation;
        }
        m_job_available.notify_all();
        std::ranges::for_each(m_workers, [](std::thread &worker) {
            if (worker.joinable()) {
                worker.join();
            }
        });
    }

    [[nodiscard]] std::size_t worker_count() const noexcept { return m_workers.size(); }

    [[nodiscard]] cpu_task_executor::statistics statistics() const noexcept {
        return {
            m_task_count.load(std::memory_order_relaxed),
            m_parallel_dispatch_count.load(std::memory_order_relaxed),
        };
    }

    void reset_statistics() noexcept {
        m_task_count.store(0, std::memory_order_relaxed);
        m_parallel_dispatch_count.store(0, std::memory_order_relaxed);
    }

    void execute(std::size_t count, std::size_t minimum_grain, void *context,
                 cpu_task_executor::task_function function) noexcept {
        if (count == 0) {
            return;
        }
        minimum_grain = std::max<std::size_t>(1, minimum_grain);
        if (m_workers.empty() || count / minimum_grain < 2) {
            function(context, 0, count);
            return;
        }

        {
            const std::lock_guard lock{m_mutex};
            m_count = count;
            m_grain = minimum_grain;
            m_context = context;
            m_function = function;
            m_next.store(0, std::memory_order_relaxed);
            m_remaining_workers = m_workers.size();
            ++m_generation;
            m_parallel_dispatch_count.fetch_add(1, std::memory_order_relaxed);
        }
        m_job_available.notify_all();
        drain_job();

        std::unique_lock lock{m_mutex};
        m_job_finished.wait(lock, [&] { return m_remaining_workers == 0; });
    }

  private:
    void drain_job() noexcept {
        const auto chunks = std::views::iota(std::size_t{}) |
                            std::views::take_while([&](std::size_t) {
                                return m_next.load(std::memory_order_relaxed) < m_count;
                            });
        std::ranges::for_each(chunks, [&](std::size_t) {
            const auto first = m_next.fetch_add(m_grain, std::memory_order_relaxed);
            if (first < m_count) {
                m_function(m_context, first, std::min(first + m_grain, m_count));
                m_task_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    void worker_main() noexcept {
        std::uint64_t observed_generation{};
        const auto jobs = std::views::iota(std::size_t{}) |
                          std::views::take_while([&](std::size_t) {
                              std::unique_lock lock{m_mutex};
                              m_job_available.wait(lock, [&] {
                                  return m_stopping || m_generation != observed_generation;
                              });
                              return !m_stopping;
                          });
        std::ranges::for_each(jobs, [&](std::size_t) {
            {
                const std::lock_guard lock{m_mutex};
                observed_generation = m_generation;
            }
            drain_job();
            {
                const std::lock_guard lock{m_mutex};
                --m_remaining_workers;
                if (m_remaining_workers == 0) {
                    m_job_finished.notify_one();
                }
            }
        });
    }

    std::vector<std::thread> m_workers;
    mutable std::mutex m_mutex;
    std::condition_variable m_job_available;
    std::condition_variable m_job_finished;
    std::atomic<std::size_t> m_next{};
    std::atomic<std::size_t> m_task_count{};
    std::atomic<std::size_t> m_parallel_dispatch_count{};
    std::size_t m_count{};
    std::size_t m_grain{1};
    std::size_t m_remaining_workers{};
    void *m_context{};
    cpu_task_executor::task_function m_function{};
    std::uint64_t m_generation{};
    bool m_stopping{};
};

cpu_task_executor::cpu_task_executor(std::size_t worker_count)
    : m_implementation(std::make_unique<implementation>(worker_count)) {}

cpu_task_executor::~cpu_task_executor() = default;

std::size_t cpu_task_executor::worker_count() const noexcept {
    return m_implementation->worker_count();
}

cpu_task_executor::statistics cpu_task_executor::last_statistics() const noexcept {
    return m_implementation->statistics();
}

void cpu_task_executor::reset_statistics() noexcept { m_implementation->reset_statistics(); }

void cpu_task_executor::execute(std::size_t count, std::size_t minimum_grain, void *context,
                                task_function function) noexcept {
    m_implementation->execute(count, minimum_grain, context, function);
}
} // namespace scene_polytree
