#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace scene_polytree {
class cpu_task_executor {
  public:
    using task_function = void (*)(void *context, std::size_t first,
                                   std::size_t last) noexcept;

    struct statistics {
        std::size_t task_count{};
        std::size_t parallel_dispatch_count{};
    };

    explicit cpu_task_executor(std::size_t worker_count = 0);
    ~cpu_task_executor();

    cpu_task_executor(const cpu_task_executor &) = delete;
    cpu_task_executor &operator=(const cpu_task_executor &) = delete;

    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] statistics last_statistics() const noexcept;
    void reset_statistics() noexcept;

    // Executes [0, count) synchronously. A parallel dispatch is used only when
    // at least two chunks can satisfy minimum_grain; otherwise the callback runs
    // once on the calling thread.
    void execute(std::size_t count, std::size_t minimum_grain, void *context,
                 task_function function) noexcept;

  private:
    class implementation;
    std::unique_ptr<implementation> m_implementation;
};
} // namespace scene_polytree
