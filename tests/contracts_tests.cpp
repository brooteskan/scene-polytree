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
    const auto root_result = insert_root(topology, std::uint32_t{10});
    if (!root_result)
    {
        return 1;
    }
    const auto root = root_result.value();
    const auto intermediate_result = insert_child(
        topology,
        root,
        std::uint32_t{20},
        std::uint32_t{100});
    if (!intermediate_result)
    {
        return 2;
    }
    const auto intermediate = intermediate_result.value();
    const auto leaf_result = insert_child(
        topology,
        intermediate,
        std::uint32_t{30},
        std::uint32_t{200});
    if (!leaf_result)
    {
        return 3;
    }
    const auto leaf = leaf_result.value();

    scene_polytree::basic_scene authoring_scene{
        std::move(topology),
        fake_scene_state{7}};
    if (node_count(authoring_scene.topology()) != 3
        || edge_count(authoring_scene.topology()) != 2
        || parent(authoring_scene.topology(), root) != INVALID_STABLE_NODE
        || parent(authoring_scene.topology(), intermediate) != root
        || parent(authoring_scene.topology(), leaf) != intermediate
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
    const auto runtime_root = first_freeze->identities.runtime_handle(root);
    const auto runtime_intermediate = first_freeze->identities.runtime_handle(intermediate);
    const auto runtime_leaf = first_freeze->identities.runtime_handle(leaf);
    if (runtime_root != 0u
        || runtime_intermediate != 1u
        || runtime_leaf != 2u
        || first_freeze->identities.authoring_id(0u) != root
        || first_freeze->identities.authoring_id(1u) != intermediate
        || first_freeze->identities.authoring_id(2u) != leaf)
    {
        return 6;
    }

    scene_polytree::basic_scene runtime_scene{
        std::move(first_freeze->topology),
        fake_scene_state{7}};
    const auto& tree = runtime_scene.topology().polytree;
    if (node_count(tree) != 3
        || edge_count(tree) != 2
        || parent(tree, *runtime_root) != INVALID_NODE
        || parent(tree, *runtime_intermediate) != *runtime_root
        || parent(tree, *runtime_leaf) != *runtime_intermediate
        || depth(tree, *runtime_leaf) != 2
        || runtime_scene.state().revision != 7)
    {
        return 7;
    }

    const auto plan = evaluation_plan(tree);
    if (plan.node_count() != 3
        || plan.level_count() != 3
        || plan.roots.size() != 1
        || plan.roots[0] != *runtime_root
        || plan.reverse_topological_order[0] != *runtime_leaf
        || plan.dependency_level(1).size() != 1
        || plan.dependency_level(1)[0] != *runtime_intermediate)
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
    if (motion.linear_velocity.value != 0 || motion.angular_velocity.value != 0)
    {
        return 10;
    }

    if (!detach_to_root(authoring_scene.topology(), intermediate, 0u))
    {
        return 11;
    }
    if (frozen_revision == revision(authoring_scene.topology())
        || first_freeze->identities.runtime_handle(intermediate) != 1u)
    {
        return 12;
    }

    auto second_freeze = freeze(authoring_scene.topology(), workspace);
    if (!second_freeze
        || second_freeze->identities.source_revision()
            != revision(authoring_scene.topology())
        || second_freeze->identities.runtime_handle(intermediate) != 0u
        || second_freeze->identities.runtime_handle(leaf) != 1u
        || second_freeze->identities.runtime_handle(root) != 2u
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
