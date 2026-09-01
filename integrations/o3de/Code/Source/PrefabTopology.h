#pragma once

#include <ScenePolytree/ScenePolytreeTypes.h>

#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>

namespace ScenePolytree::Internal {
struct PrefabTopologyResult {
    AZStd::vector<ScenePolytreeNodeDescriptor> m_nodes;
    AZStd::vector<AZ::EntityId> m_entities;
    ScenePolytreeFailure m_failure;

    [[nodiscard]] bool IsSuccess() const noexcept { return m_failure.IsSuccess(); }
};

[[nodiscard]] PrefabTopologyResult
ValidateAndNormalizeTopology(AZStd::vector<ScenePolytreeNodeDescriptor> nodes);

[[nodiscard]] PrefabTopologyResult ExtractPrefabTopology(AzFramework::Spawnable &spawnable);
[[nodiscard]] PrefabTopologyResult
ExtractPrefabTopology(AzFramework::SpawnableEntityContainerView entities);
[[nodiscard]] PrefabTopologyResult
ExtractPrefabTopology(AzFramework::SpawnableConstEntityContainerView entities);
} // namespace ScenePolytree::Internal
