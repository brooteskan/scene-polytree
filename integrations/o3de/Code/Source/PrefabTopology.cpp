#include "PrefabTopology.h"

#include <ScenePolytree/ScenePolytreePrefabNodeComponent.h>

#include <AzFramework/Components/TransformComponent.h>

#include <algorithm>
#include <functional>
#include <ranges>

namespace ScenePolytree::Internal {
namespace {
[[nodiscard]] bool NameLess(const AZ::Name &left, const AZ::Name &right) {
    return left.GetStringView() < right.GetStringView();
}
} // namespace

PrefabTopologyResult
ValidateAndNormalizeTopology(AZStd::vector<ScenePolytreeNodeDescriptor> nodes) {
    if (nodes.empty()) {
        return {{}, {ScenePolytreeResultCode::MissingTopologyMetadata}};
    }

    const auto invalidNode = std::ranges::find_if(nodes, [](const auto &node) {
        return node.m_bindingId.IsEmpty() || !node.m_initialLocal.IsFinite();
    });
    if (invalidNode != nodes.end()) {
        const auto code = invalidNode->m_bindingId.IsEmpty()
                              ? ScenePolytreeResultCode::InvalidBinding
                              : ScenePolytreeResultCode::InvalidTransform;
        return {{}, {code, {}, {}, invalidNode->m_bindingId}};
    }

    const auto unsupportedNode = std::ranges::find_if(nodes, [](const auto &node) {
        return node.m_nodeType != ScenePolytreeNodeType::Transform;
    });
    if (unsupportedNode != nodes.end()) {
        return {
            {},
            {ScenePolytreeResultCode::UnsupportedNodeType, {}, {}, unsupportedNode->m_bindingId}};
    }

    std::ranges::sort(nodes, [](const auto &left, const auto &right) {
        return NameLess(left.m_bindingId, right.m_bindingId);
    });
    const auto duplicate =
        std::ranges::adjacent_find(nodes, [](const auto &left, const auto &right) {
            return left.m_bindingId == right.m_bindingId;
        });
    if (duplicate != nodes.end()) {
        return {{}, {ScenePolytreeResultCode::DuplicateBindingId, {}, {}, duplicate->m_bindingId}};
    }

    const auto invalidJoint = std::ranges::find_if(nodes, [&](const auto &node) {
        const bool root = node.m_parentBindingId.IsEmpty();
        const bool supported = node.m_jointType == ScenePolytreeJointType::None ||
                               node.m_jointType == ScenePolytreeJointType::Fixed ||
                               node.m_jointType == ScenePolytreeJointType::Yaw ||
                               node.m_jointType == ScenePolytreeJointType::Pitch;
        return !supported || (root && node.m_jointType != ScenePolytreeJointType::None) ||
               (!root && node.m_jointType == ScenePolytreeJointType::None);
    });
    if (invalidJoint != nodes.end()) {
        return {{},
                {ScenePolytreeResultCode::UnsupportedJointType, {}, {}, invalidJoint->m_bindingId}};
    }

    const auto findNode = [&](const AZ::Name &bindingId) {
        return std::ranges::lower_bound(nodes, bindingId, NameLess,
                                        &ScenePolytreeNodeDescriptor::m_bindingId);
    };
    const auto dangling = std::ranges::find_if(nodes, [&](const auto &node) {
        return !node.m_parentBindingId.IsEmpty() &&
               (findNode(node.m_parentBindingId) == nodes.end() ||
                findNode(node.m_parentBindingId)->m_bindingId != node.m_parentBindingId);
    });
    if (dangling != nodes.end()) {
        return {{}, {ScenePolytreeResultCode::DanglingParent, {}, {}, dangling->m_bindingId}};
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
        return {{}, {ScenePolytreeResultCode::Cycle, {}, {}, nodes[*cyclic].m_bindingId}};
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
    return {AZStd::move(nodes), {}};
}

PrefabTopologyResult ExtractPrefabTopology(AzFramework::Spawnable &spawnable) {
    AZStd::vector<ScenePolytreeNodeDescriptor> nodes;
    AZ::Name missingTransformNode;
    std::ranges::for_each(
        spawnable.GetEntities(), [&](const AZStd::unique_ptr<AZ::Entity> &entity) {
            auto *metadata = entity->FindComponent<ScenePolytreePrefabNodeComponent>();
            auto *transform = entity->FindComponent<AzFramework::TransformComponent>();
            if (metadata != nullptr && transform == nullptr) {
                missingTransformNode = metadata->GetBindingId();
            } else if (metadata != nullptr) {
                nodes.push_back({metadata->GetBindingId(), metadata->GetParentBindingId(),
                                 metadata->GetNodeType(), metadata->GetJointType(),
                                 transform->GetLocalTM()});
            }
        });

    if (!missingTransformNode.IsEmpty()) {
        return {{}, {ScenePolytreeResultCode::InvalidTransform, {}, {}, missingTransformNode}};
    }

    return ValidateAndNormalizeTopology(AZStd::move(nodes));
}
} // namespace ScenePolytree::Internal
