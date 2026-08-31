#pragma once

#include <ScenePolytree/ScenePolytreeTypes.h>

#include <AzFramework/Spawnable/Spawnable.h>

namespace ScenePolytree::Internal {
struct PrefabTopologyResult {
    AZStd::vector<ScenePolytreeNodeDescriptor> m_nodes;
    ScenePolytreeFailure m_failure;

    [[nodiscard]] bool IsSuccess() const noexcept { return m_failure.IsSuccess(); }
};

[[nodiscard]] PrefabTopologyResult
ValidateAndNormalizeTopology(AZStd::vector<ScenePolytreeNodeDescriptor> nodes);

[[nodiscard]] PrefabTopologyResult ExtractPrefabTopology(AzFramework::Spawnable &spawnable);
} // namespace ScenePolytree::Internal
