#pragma once

#include <type_traits>
#include <utility>

namespace scene_polytree
{
    template<class Topology, class SceneState>
    class basic_scene
    {
    public:
        using topology_type = Topology;
        using state_type = SceneState;

        constexpr basic_scene(Topology topology, SceneState state)
            noexcept(std::is_nothrow_move_constructible_v<Topology>
                && std::is_nothrow_move_constructible_v<SceneState>)
            : m_topology(std::move(topology))
            , m_state(std::move(state))
        {
        }

        [[nodiscard]] constexpr Topology& topology() noexcept
        {
            return m_topology;
        }

        [[nodiscard]] constexpr const Topology& topology() const noexcept
        {
            return m_topology;
        }

        [[nodiscard]] constexpr SceneState& state() noexcept
        {
            return m_state;
        }

        [[nodiscard]] constexpr const SceneState& state() const noexcept
        {
            return m_state;
        }

    private:
        Topology m_topology;
        SceneState m_state;
    };

    template<class Topology, class SceneState>
    basic_scene(Topology, SceneState) -> basic_scene<Topology, SceneState>;
}
