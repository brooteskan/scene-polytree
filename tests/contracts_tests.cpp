#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_polytree.hpp>

// The tagged algo baseline's pipeline header relies on these transitive includes.
#include <functional>
#include <tuple>
#include <type_traits>

#include <graph/mutable_polytree.h>
#include <graph/polytree_concepts.h>
#include <graph/polytree_freeze.h>
#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>

#include <array>
#include <cstdint>
#include <ranges>
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
    using namespace wz::core::graph;
    using MutableTopology = MutablePolytree<std::uint32_t, std::uint32_t>;
    using RuntimeTopology = Polytree<std::uint32_t, std::uint32_t>;
    static_assert(PolytreeTopology<MutableTopology>);
    static_assert(PolytreeTopology<RuntimeTopology>);

    MutableTopology topology;
    const auto hull_result = insert_root(topology, std::uint32_t{10});
    if (!hull_result)
    {
        return 1;
    }
    const auto hull = hull_result.value();
    const auto turret_result = insert_child(
        topology,
        hull,
        std::uint32_t{20},
        std::uint32_t{100});
    if (!turret_result)
    {
        return 2;
    }
    const auto turret = turret_result.value();
    const auto gun_result = insert_child(
        topology,
        turret,
        std::uint32_t{30},
        std::uint32_t{200});
    if (!gun_result)
    {
        return 3;
    }
    const auto gun = gun_result.value();

    scene_polytree::basic_scene authoring_scene{
        std::move(topology),
        fake_scene_state{7}};
    if (node_count(authoring_scene.topology()) != 3
        || edge_count(authoring_scene.topology()) != 2
        || parent(authoring_scene.topology(), hull) != INVALID_STABLE_NODE
        || parent(authoring_scene.topology(), turret) != hull
        || parent(authoring_scene.topology(), gun) != turret
        || authoring_scene.state().revision != 7)
    {
        return 4;
    }

    FreezeWorkspace workspace;
    auto first_freeze = freeze(authoring_scene.topology(), workspace);
    if (!first_freeze)
    {
        return 5;
    }
    const auto frozen_revision = first_freeze->identities.source_revision();
    const auto runtime_hull = first_freeze->identities.runtime_handle(hull);
    const auto runtime_turret = first_freeze->identities.runtime_handle(turret);
    const auto runtime_gun = first_freeze->identities.runtime_handle(gun);
    if (runtime_hull != 0u
        || runtime_turret != 1u
        || runtime_gun != 2u
        || first_freeze->identities.authoring_id(0u) != hull
        || first_freeze->identities.authoring_id(1u) != turret
        || first_freeze->identities.authoring_id(2u) != gun)
    {
        return 6;
    }

    scene_polytree::basic_scene runtime_scene{
        std::move(first_freeze->topology),
        fake_scene_state{7}};
    const auto& tree = runtime_scene.topology().polytree;
    if (node_count(tree) != 3
        || edge_count(tree) != 2
        || parent(tree, *runtime_hull) != INVALID_NODE
        || parent(tree, *runtime_turret) != *runtime_hull
        || parent(tree, *runtime_gun) != *runtime_turret
        || depth(tree, *runtime_gun) != 2
        || runtime_scene.state().revision != 7)
    {
        return 7;
    }

    const auto plan = evaluation_plan(tree);
    if (plan.node_count() != 3
        || plan.level_count() != 3
        || plan.roots.size() != 1
        || plan.roots[0] != *runtime_hull
        || plan.reverse_topological_order[0] != *runtime_gun
        || plan.dependency_level(1).size() != 1
        || plan.dependency_level(1)[0] != *runtime_turret)
    {
        return 8;
    }

    scene_polytree::transform_record<fake_transform> transform{};
    if (!transform.dirty
        || transform.local_revision != 0
        || transform.world_revision != 0)
    {
        return 9;
    }

    const scene_polytree::motion::motion_state<fake_vector, fake_vector> motion{};
    if (!motion.enabled)
    {
        return 10;
    }

    if (!detach_to_root(authoring_scene.topology(), turret, 0u))
    {
        return 11;
    }
    if (frozen_revision == revision(authoring_scene.topology())
        || first_freeze->identities.runtime_handle(turret) != 1u)
    {
        return 12;
    }

    auto second_freeze = freeze(authoring_scene.topology(), workspace);
    if (!second_freeze
        || second_freeze->identities.source_revision()
            != revision(authoring_scene.topology())
        || second_freeze->identities.runtime_handle(turret) != 0u
        || second_freeze->identities.runtime_handle(gun) != 1u
        || second_freeze->identities.runtime_handle(hull) != 2u
        || !std::ranges::equal(
            roots(second_freeze->topology.polytree),
            std::array{0u, 2u}))
    {
        return 13;
    }

    auto repeated_freeze = freeze(authoring_scene.topology(), workspace);
    if (!repeated_freeze
        || !std::ranges::equal(
            second_freeze->identities.runtime_to_authoring(),
            repeated_freeze->identities.runtime_to_authoring())
        || !std::ranges::equal(
            evaluation_plan(second_freeze->topology.polytree).topological_order,
            evaluation_plan(repeated_freeze->topology.polytree).topological_order))
    {
        return 14;
    }

    return 0;
}
