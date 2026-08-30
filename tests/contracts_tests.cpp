#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_polytree.hpp>

// The tagged algo baseline's pipeline header relies on these transitive includes.
#include <functional>
#include <tuple>
#include <type_traits>

#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>

#include <cstdint>
#include <utility>

namespace
{
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
    wz::core::graph::PolytreeBuilder<std::uint32_t, std::uint32_t> builder;
    const auto hull = wz::core::graph::add_node(builder, std::uint32_t{10});
    const auto turret = wz::core::graph::add_node(builder, std::uint32_t{20});
    const auto gun = wz::core::graph::add_node(builder, std::uint32_t{30});
    if (!wz::core::graph::add_edge(builder, hull, turret, std::uint32_t{100})
        || !wz::core::graph::add_edge(builder, turret, gun, std::uint32_t{200}))
    {
        return 1;
    }

    auto topology = wz::core::graph::build(std::move(builder));
    if (!topology)
    {
        return 2;
    }

    scene_polytree::basic_scene scene{std::move(*topology), fake_scene_state{7}};
    const auto& tree = scene.topology().polytree;
    if (wz::core::graph::node_count(tree) != 3
        || wz::core::graph::edge_count(tree) != 2
        || wz::core::graph::parent(tree, hull) != wz::core::graph::INVALID_NODE
        || wz::core::graph::parent(tree, turret) != hull
        || wz::core::graph::parent(tree, gun) != turret
        || wz::core::graph::depth(tree, gun) != 2
        || scene.state().revision != 7)
    {
        return 3;
    }

    const auto topology_order = wz::core::graph::topo_order(tree);
    if (topology_order.size() != 3
        || topology_order[0] != hull
        || topology_order[1] != turret
        || topology_order[2] != gun)
    {
        return 4;
    }

    scene_polytree::transform_record<fake_transform> transform{};
    if (!transform.dirty || transform.local_revision != 0 || transform.world_revision != 0)
    {
        return 5;
    }

    const auto topology_plan = wz::core::graph::evaluation_plan(tree);
    if (topology_plan.node_count() != 3
        || topology_plan.level_count() != 3
        || topology_plan.roots.size() != 1
        || topology_plan.roots[0] != hull
        || topology_plan.reverse_topological_order[0] != gun)
    {
        return 6;
    }

    const scene_polytree::evaluation_plan_view<std::uint32_t> plan{
        topology_plan.topological_order,
        topology_plan.reverse_topological_order,
        topology_plan.roots,
        topology_plan.dependency_order,
        topology_plan.dependency_level_offsets,
    };
    if (plan.empty()
        || plan.node_count() != 3
        || plan.level_count() != 3
        || plan.dependency_level(1).size() != 1
        || plan.dependency_level(1)[0] != turret)
    {
        return 7;
    }

    const scene_polytree::motion::motion_state<fake_vector, fake_vector> motion{};
    if (!motion.enabled)
    {
        return 8;
    }

    return 0;
}
