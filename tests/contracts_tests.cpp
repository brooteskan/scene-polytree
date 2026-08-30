#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_polytree.hpp>

#include <array>
#include <cstdint>

namespace
{
    struct fake_topology
    {
        std::uint32_t node_count{};
    };

    struct fake_scene_state
    {
        std::uint64_t revision{};
    };

    struct fake_transform
    {
        float value{};
    };

    struct fake_vector
    {
        float value{};
    };
}

int main()
{
    scene_polytree::basic_scene scene{fake_topology{3}, fake_scene_state{7}};
    if (scene.topology().node_count != 3 || scene.state().revision != 7)
    {
        return 1;
    }

    scene_polytree::transform_record<fake_transform> transform{};
    if (!transform.dirty || transform.local_revision != 0 || transform.world_revision != 0)
    {
        return 2;
    }

    constexpr std::array<std::uint32_t, 3> order{0, 1, 2};
    constexpr std::array<std::uint32_t, 3> offsets{0, 1, 3};
    const scene_polytree::evaluation_plan_view<std::uint32_t> plan{order, offsets};
    if (plan.empty() || plan.node_count() != 3 || plan.level_count() != 2)
    {
        return 3;
    }

    const scene_polytree::motion::motion_state<fake_vector, fake_vector> motion{};
    if (!motion.enabled)
    {
        return 4;
    }

    return 0;
}
