#pragma once

#include <chrono>
#include <cstdint>

namespace scene_polytree::motion {
struct fixed_motion_step {
    std::uint64_t tick{};
    std::chrono::nanoseconds delta{};
};

namespace detail {
struct fixed_step_sequence_access;
}

class fixed_step_sequence {
  public:
    explicit fixed_step_sequence(std::chrono::nanoseconds delta,
                                 std::uint64_t initial_tick = 0) noexcept
        : m_delta(delta), m_next_tick(initial_tick) {}

    [[nodiscard]] std::chrono::nanoseconds delta() const noexcept { return m_delta; }

    [[nodiscard]] std::uint64_t next_tick() const noexcept { return m_next_tick; }

    [[nodiscard]] fixed_motion_step next_step() const noexcept { return {m_next_tick, m_delta}; }

  private:
    friend struct detail::fixed_step_sequence_access;

    std::chrono::nanoseconds m_delta;
    std::uint64_t m_next_tick{};
};

namespace detail {
struct fixed_step_sequence_access {
    static void complete(fixed_step_sequence &sequence) noexcept { ++sequence.m_next_tick; }
};
} // namespace detail
} // namespace scene_polytree::motion
