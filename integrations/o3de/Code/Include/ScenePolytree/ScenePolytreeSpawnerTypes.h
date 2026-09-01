#pragma once

#include <ScenePolytree/ScenePolytreeTypes.h>

#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/RTTI/TypeInfo.h>

namespace ScenePolytree {
enum class SpawnTriggerMode : AZ::u8 {
    OnReady,
    ExternalRequestsOnly,
    OnReadyAndExternalRequests,
};

enum class DefaultPlacement : AZ::u8 { SpawnerWorldTransform };

enum class ScenePolytreeSpawnerLifecycle : AZ::u8 {
    Inactive,
    Registering,
    Ready,
    Failed,
    Deactivating,
};

enum class SpawnError : AZ::u8 {
    None,
    NotReady,
    ExternalRequestsDisabled,
    ShuttingDown,
    InvalidConfiguration,
    InvalidPrefab,
    InvalidPlacement,
    NoCapacity,
    SceneCommandFailed,
    SpawnServiceUnavailable,
    O3deSpawnFailed,
    HierarchyMismatch,
    InvalidBinding,
    DuplicateBinding,
    InactiveEntity,
    InvalidTransform,
    CompetingTransformParent,
    Cancelled,
    CleanupFailed,
};

enum class DespawnError : AZ::u8 {
    None,
    NotReady,
    ShuttingDown,
    StaleInstance,
    InstanceNotActive,
    SceneCommandFailed,
    DespawnServiceUnavailable,
    CleanupFailed,
};

struct SpawnRequestId {
    AZ::u32 m_spawnerGeneration{};
    AZ::u64 m_value{};
    [[nodiscard]] bool IsValid() const noexcept { return m_spawnerGeneration != 0 && m_value != 0; }
    friend bool operator==(const SpawnRequestId &, const SpawnRequestId &) = default;
};

struct DespawnRequestId {
    AZ::u32 m_spawnerGeneration{};
    AZ::u64 m_value{};
    [[nodiscard]] bool IsValid() const noexcept { return m_spawnerGeneration != 0 && m_value != 0; }
    friend bool operator==(const DespawnRequestId &, const DespawnRequestId &) = default;
};

struct InstanceHandle {
    SlotHandle m_slot;
    AZ::u32 m_spawnerGeneration{};
    [[nodiscard]] bool IsValid() const noexcept {
        return m_slot.IsValid() && m_spawnerGeneration != 0;
    }
    friend bool operator==(const InstanceHandle &, const InstanceHandle &) = default;
};

struct ScenePolytreeRequestContext {
    AZ::EntityId m_requestingEntity{};
    AZ::u64 m_correlation{};
};

struct ScenePolytreeSpawnRequest {
    ScenePolytreeRequestContext m_context;
    bool m_hasExplicitWorldTransform{};
    AZ::Transform m_worldTransform{AZ::Transform::CreateIdentity()};
};

struct ScenePolytreeSpawnFailure {
    SpawnError m_error{SpawnError::None};
    ScenePolytreeResultCode m_sceneResult{ScenePolytreeResultCode::Success};
    ScenePolytreeResultCode m_cleanupResult{ScenePolytreeResultCode::Success};
};

struct ScenePolytreeDespawnFailure {
    DespawnError m_error{DespawnError::None};
    ScenePolytreeResultCode m_sceneResult{ScenePolytreeResultCode::Success};
    ScenePolytreeResultCode m_cleanupResult{ScenePolytreeResultCode::Success};
};

struct ScenePolytreeSpawnerFailure {
    SpawnError m_error{SpawnError::None};
    ScenePolytreeFailure m_registrationFailure;
};
} // namespace ScenePolytree

namespace AZ {
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::SpawnTriggerMode, "{D1E8F49D-41BC-4C9A-A56E-D2DF4BC5AB14}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::DefaultPlacement, "{2F3C9235-673F-4722-A25C-376127615A41}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::ScenePolytreeSpawnerLifecycle,
                        "{96BD9016-C606-4304-9B62-AFFAD78014E6}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::SpawnError, "{AC2587BE-2B93-4E09-B64A-437015387F3B}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::DespawnError, "{B46473B3-A67C-4588-A246-6922820A9934}");
} // namespace AZ
