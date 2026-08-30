#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace scene_polytree
{
    template<class NodeHandle>
    struct evaluation_plan_view
    {
        std::span<const NodeHandle> topological_order;
        std::span<const std::uint32_t> level_offsets;

        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return topological_order.empty();
        }

        [[nodiscard]] constexpr std::size_t node_count() const noexcept
        {
            return topological_order.size();
        }

        [[nodiscard]] constexpr std::size_t level_count() const noexcept
        {
            return level_offsets.empty() ? 0 : level_offsets.size() - 1;
        }
    };
}
