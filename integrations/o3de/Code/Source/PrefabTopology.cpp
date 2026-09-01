#include "PrefabTopology.h"

#include <AzCore/Component/Entity.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzFramework/Components/TransformComponent.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <ranges>

namespace ScenePolytree::Internal {
namespace {
constexpr std::size_t NoParent = (std::numeric_limits<std::size_t>::max)();

struct InferredNode {
    const AZ::Entity *m_entity{};
    AzFramework::TransformComponent *m_transform{};
    std::size_t m_parent{NoParent};
    std::size_t m_depth{};
    AZStd::string m_path;
};

[[nodiscard]] bool NameLess(const AZ::Name &left, const AZ::Name &right) {
    return left.GetStringView() < right.GetStringView();
}

[[nodiscard]] AZStd::string EscapePathSegment(AZStd::string_view name) {
    AZStd::string escaped;
    escaped.reserve(name.size());
    std::ranges::for_each(name, [&](char value) {
        switch (value) {
        case '%':
            escaped += "%25";
            break;
        case '/':
            escaped += "%2F";
            break;
        default:
            escaped.push_back(value);
            break;
        }
    });
    return escaped;
}

[[nodiscard]] PrefabTopologyResult
ExtractEntityHierarchy(const AZStd::vector<const AZ::Entity *> &entities) {
    AZStd::vector<InferredNode> inferred;
    inferred.reserve(entities.size());
    AZStd::unordered_map<AZ::EntityId, std::size_t> byEntity;
    ScenePolytreeResultCode entityFailure = ScenePolytreeResultCode::Success;

    std::ranges::for_each(entities, [&](const AZ::Entity *entity) {
        if (entity == nullptr) {
            return;
        }
        auto *transform = const_cast<AzFramework::TransformComponent *>(
            entity->FindComponent<AzFramework::TransformComponent>());
        if (transform == nullptr) {
            return;
        }
        const AZ::EntityId entityId = entity->GetId();
        if (!entityId.IsValid() || byEntity.contains(entityId)) {
            entityFailure = ScenePolytreeResultCode::InvalidBinding;
            return;
        }
        if (!transform->GetLocalTM().IsFinite()) {
            entityFailure = ScenePolytreeResultCode::InvalidTransform;
            return;
        }
        byEntity.emplace(entityId, inferred.size());
        inferred.push_back({entity, transform, NoParent, 0, {}});
    });

    if (entityFailure != ScenePolytreeResultCode::Success) {
        return {{}, {}, {entityFailure}};
    }
    if (inferred.empty()) {
        return {{}, {}, {ScenePolytreeResultCode::EmptyPrefabHierarchy}};
    }

    const auto indices = std::views::iota(std::size_t{}, inferred.size());
    const auto dangling = std::ranges::find_if(indices, [&](std::size_t index) {
        const AZ::EntityId parentId = inferred[index].m_transform->GetParentId();
        return parentId.IsValid() && !byEntity.contains(parentId);
    });
    if (dangling != indices.end()) {
        return {{},
                {},
                {ScenePolytreeResultCode::DanglingParent,
                 {},
                 {},
                 AZ::Name(inferred[*dangling].m_entity->GetName())}};
    }
    std::ranges::for_each(indices, [&](std::size_t index) {
        const AZ::EntityId parentId = inferred[index].m_transform->GetParentId();
        if (parentId.IsValid()) {
            inferred[index].m_parent = byEntity.find(parentId)->second;
        }
    });

    AZStd::vector<AZ::u8> visitState(inferred.size());
    ScenePolytreeResultCode pathFailure = ScenePolytreeResultCode::Success;
    std::function<bool(std::size_t)> buildPath = [&](std::size_t index) {
        if (visitState[index] == 1) {
            pathFailure = ScenePolytreeResultCode::Cycle;
            return false;
        }
        if (visitState[index] == 2) {
            return true;
        }
        visitState[index] = 1;
        const AZStd::string segment = EscapePathSegment(inferred[index].m_entity->GetName());
        if (segment.empty()) {
            pathFailure = ScenePolytreeResultCode::InvalidBinding;
            return false;
        }
        if (inferred[index].m_parent == NoParent) {
            inferred[index].m_path = segment;
        } else {
            if (!buildPath(inferred[index].m_parent)) {
                return false;
            }
            inferred[index].m_depth = inferred[inferred[index].m_parent].m_depth + 1;
            inferred[index].m_path = inferred[inferred[index].m_parent].m_path + "/" + segment;
        }
        visitState[index] = 2;
        return true;
    };

    const auto invalidPath =
        std::ranges::find_if(indices, [&](std::size_t index) { return !buildPath(index); });
    if (invalidPath != indices.end()) {
        return {
            {}, {}, {pathFailure, {}, {}, AZ::Name(inferred[*invalidPath].m_entity->GetName())}};
    }

    std::ranges::sort(inferred, [](const InferredNode &left, const InferredNode &right) {
        return left.m_depth != right.m_depth ? left.m_depth < right.m_depth
                                             : left.m_path < right.m_path;
    });
    const auto duplicate = std::ranges::adjacent_find(
        inferred, [](const InferredNode &left, const InferredNode &right) {
            return left.m_path == right.m_path;
        });
    if (duplicate != inferred.end()) {
        return {{},
                {},
                {ScenePolytreeResultCode::DuplicateBindingId, {}, {}, AZ::Name(duplicate->m_path)}};
    }

    PrefabTopologyResult result;
    result.m_nodes.reserve(inferred.size());
    result.m_entities.reserve(inferred.size());
    std::ranges::for_each(inferred, [&](const InferredNode &node) {
        const AZ::Name parent =
            node.m_parent == NoParent
                ? AZ::Name()
                : AZ::Name(std::ranges::find_if(inferred, [&](const InferredNode &candidate) {
                               return candidate.m_entity->GetId() ==
                                      node.m_transform->GetParentId();
                           })->m_path);
        result.m_nodes.push_back({AZ::Name(node.m_path), parent, node.m_transform->GetLocalTM()});
        result.m_entities.push_back(node.m_entity->GetId());
    });
    return result;
}
} // namespace

PrefabTopologyResult
ValidateAndNormalizeTopology(AZStd::vector<ScenePolytreeNodeDescriptor> nodes) {
    if (nodes.empty()) {
        return {{}, {}, {ScenePolytreeResultCode::EmptyPrefabHierarchy}};
    }

    const auto invalidNode = std::ranges::find_if(nodes, [](const auto &node) {
        return node.m_bindingId.IsEmpty() || !node.m_initialLocal.IsFinite();
    });
    if (invalidNode != nodes.end()) {
        const auto code = invalidNode->m_bindingId.IsEmpty()
                              ? ScenePolytreeResultCode::InvalidBinding
                              : ScenePolytreeResultCode::InvalidTransform;
        return {{}, {}, {code, {}, {}, invalidNode->m_bindingId}};
    }

    std::ranges::sort(nodes, [](const auto &left, const auto &right) {
        return NameLess(left.m_bindingId, right.m_bindingId);
    });
    const auto duplicate =
        std::ranges::adjacent_find(nodes, [](const auto &left, const auto &right) {
            return left.m_bindingId == right.m_bindingId;
        });
    if (duplicate != nodes.end()) {
        return {
            {}, {}, {ScenePolytreeResultCode::DuplicateBindingId, {}, {}, duplicate->m_bindingId}};
    }

    const auto findNode = [&](const AZ::Name &bindingId) {
        return std::ranges::lower_bound(nodes, bindingId, NameLess,
                                        &ScenePolytreeNodeDescriptor::m_bindingId);
    };
    const auto dangling = std::ranges::find_if(nodes, [&](const auto &node) {
        if (node.m_parentBindingId.IsEmpty()) {
            return false;
        }
        const auto parent = findNode(node.m_parentBindingId);
        return parent == nodes.end() || parent->m_bindingId != node.m_parentBindingId;
    });
    if (dangling != nodes.end()) {
        return {{}, {}, {ScenePolytreeResultCode::DanglingParent, {}, {}, dangling->m_bindingId}};
    }

    AZStd::vector<AZ::u8> visitState(nodes.size());
    std::function<bool(std::size_t)> visit = [&](std::size_t index) {
        if (visitState[index] == 1) {
            return false;
        }
        if (visitState[index] == 2) {
            return true;
        }
        visitState[index] = 1;
        const auto &parentId = nodes[index].m_parentBindingId;
        const bool parentValid =
            parentId.IsEmpty() ||
            visit(static_cast<std::size_t>(findNode(parentId) - nodes.begin()));
        visitState[index] = 2;
        return parentValid;
    };
    const auto indices = std::views::iota(std::size_t{}, nodes.size());
    const auto cyclic =
        std::ranges::find_if(indices, [&](std::size_t index) { return !visit(index); });
    if (cyclic != indices.end()) {
        return {{}, {}, {ScenePolytreeResultCode::Cycle, {}, {}, nodes[*cyclic].m_bindingId}};
    }

    const auto lexicographicNodes = nodes;
    const auto findLexicographicNode = [&](const AZ::Name &bindingId) {
        return std::ranges::lower_bound(lexicographicNodes, bindingId, NameLess,
                                        &ScenePolytreeNodeDescriptor::m_bindingId);
    };
    AZStd::vector<std::size_t> depths(nodes.size());
    std::function<std::size_t(std::size_t)> depth = [&](std::size_t index) {
        if (depths[index] != 0 || lexicographicNodes[index].m_parentBindingId.IsEmpty()) {
            return depths[index];
        }
        const auto parent = findLexicographicNode(lexicographicNodes[index].m_parentBindingId);
        depths[index] = depth(static_cast<std::size_t>(parent - lexicographicNodes.begin())) + 1;
        return depths[index];
    };
    std::ranges::for_each(indices, [&](std::size_t index) { (void)depth(index); });
    std::ranges::sort(nodes, [&](const auto &left, const auto &right) {
        const auto leftIndex = static_cast<std::size_t>(findLexicographicNode(left.m_bindingId) -
                                                        lexicographicNodes.begin());
        const auto rightIndex = static_cast<std::size_t>(findLexicographicNode(right.m_bindingId) -
                                                         lexicographicNodes.begin());
        return depths[leftIndex] != depths[rightIndex]
                   ? depths[leftIndex] < depths[rightIndex]
                   : NameLess(left.m_bindingId, right.m_bindingId);
    });
    return {AZStd::move(nodes), {}, {}};
}

PrefabTopologyResult ExtractPrefabTopology(AzFramework::Spawnable &spawnable) {
    AZStd::vector<const AZ::Entity *> entities;
    entities.reserve(spawnable.GetEntities().size());
    std::ranges::transform(spawnable.GetEntities(), std::back_inserter(entities),
                           [](const auto &entity) { return entity.get(); });
    return ExtractEntityHierarchy(entities);
}

PrefabTopologyResult ExtractPrefabTopology(AzFramework::SpawnableEntityContainerView entityView) {
    AZStd::vector<const AZ::Entity *> entities;
    entities.reserve(entityView.size());
    std::ranges::copy(entityView, std::back_inserter(entities));
    return ExtractEntityHierarchy(entities);
}

PrefabTopologyResult
ExtractPrefabTopology(AzFramework::SpawnableConstEntityContainerView entityView) {
    AZStd::vector<const AZ::Entity *> entities;
    entities.reserve(entityView.size());
    std::ranges::copy(entityView, std::back_inserter(entities));
    return ExtractEntityHierarchy(entities);
}
} // namespace ScenePolytree::Internal
