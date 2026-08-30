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
        std::span<const NodeHandle> reverse_topological_order;
        std::span<const NodeHandle> roots;
        std::span<const NodeHandle> dependency_order;
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

        [[nodiscard]] constexpr std::span<const NodeHandle> dependency_level(
            std::size_t index) const noexcept
        {
            if (index >= level_count())
            {
                return {};
            }

            const auto first = level_offsets[index];
            const auto last = level_offsets[index + 1];
            return dependency_order.subspan(first, last - first);
        }
    };
}
