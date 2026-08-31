#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Name/Name.h>
#include <AzCore/RTTI/TypeInfo.h>
#include <AzCore/std/containers/vector.h>
#include <AzFramework/Spawnable/Spawnable.h>

namespace ScenePolytree {
enum class ScenePolytreeLifecycle : AZ::u8 {
    Collecting,
    Building,
    Ready,
    Failed,
    Destroying,
};

enum class ScenePolytreeNodeType : AZ::u8 { Transform };

enum class ScenePolytreeJointType : AZ::u8 {
    None,
    Fixed,
    Yaw,
    Pitch,
};

enum class ScenePolytreeResultCode : AZ::u8 {
    Success,
    SceneNotFound,
    SceneNotReady,
    RegistrationClosed,
    InvalidPrefab,
    AssetLoadFailed,
    MissingTopologyMetadata,
    DuplicateBindingId,
    DanglingParent,
    Cycle,
    UnsupportedNodeType,
    UnsupportedJointType,
    InvalidTransform,
    ZeroCapacity,
    EmptyScene,
    MissingDefaultScene,
    DuplicateDefaultScene,
    StaleHandle,
    PartitionMismatch,
    SlotUnavailable,
    InvalidBinding,
    ConstructionFailed,
};

struct ScenePolytreeFailure {
    ScenePolytreeResultCode m_code{ScenePolytreeResultCode::Success};
    AZ::Name m_registrationKey;
    AZ::Data::AssetId m_assetId;
    AZ::Name m_nodeId;

    [[nodiscard]] bool IsSuccess() const noexcept {
        return m_code == ScenePolytreeResultCode::Success;
    }
};

struct RegistrationToken {
    AZ::u64 m_value{};
    [[nodiscard]] bool IsValid() const noexcept { return m_value != 0; }
    friend bool operator==(const RegistrationToken &, const RegistrationToken &) = default;
};

struct SceneHandle {
    AZ::u64 m_value{};
    [[nodiscard]] bool IsValid() const noexcept { return m_value != 0; }
    friend bool operator==(const SceneHandle &, const SceneHandle &) = default;
};

struct SpawnerHandle {
    SceneHandle m_scene;
    AZ::u64 m_partition{};
    AZ::u32 m_generation{1};
    [[nodiscard]] bool IsValid() const noexcept {
        return m_scene.IsValid() && m_partition != 0 && m_generation != 0;
    }
    friend bool operator==(const SpawnerHandle &, const SpawnerHandle &) = default;
};

struct SlotHandle {
    SpawnerHandle m_spawner;
    AZ::u32 m_slot{};
    AZ::u32 m_generation{};
    [[nodiscard]] bool IsValid() const noexcept { return m_spawner.IsValid() && m_generation != 0; }
    friend bool operator==(const SlotHandle &, const SlotHandle &) = default;
};

struct SceneNodeHandle {
    SlotHandle m_slot;
    AZ::Name m_bindingId;
    [[nodiscard]] bool IsValid() const noexcept {
        return m_slot.IsValid() && !m_bindingId.IsEmpty();
    }
    friend bool operator==(const SceneNodeHandle &, const SceneNodeHandle &) = default;
};

struct ScenePolytreeNodeDescriptor {
    AZ_TYPE_INFO(ScenePolytreeNodeDescriptor, "{BA10BBCA-AC1B-4EC7-8C3D-C5EF9386A90A}");
    AZ::Name m_bindingId;
    AZ::Name m_parentBindingId;
    ScenePolytreeNodeType m_nodeType{ScenePolytreeNodeType::Transform};
    ScenePolytreeJointType m_jointType{ScenePolytreeJointType::None};
    AZ::Transform m_initialLocal{AZ::Transform::CreateIdentity()};
};

struct ScenePolytreePrefabRegistrationDescriptor {
    AZ_TYPE_INFO(ScenePolytreePrefabRegistrationDescriptor,
                 "{D05E19FB-B303-418A-9F27-D4FE2F9627BA}");
    AZ::Data::Asset<AzFramework::Spawnable> m_prefab;
    AZ::u32 m_capacity{};
    AZ::Name m_registrationKey;
};

struct ScenePolytreePartitionDescriptor {
    AZ::u64 m_partition{};
    AZ::u32 m_capacity{};
    AZStd::vector<ScenePolytreeNodeDescriptor> m_nodes;
};

struct ScenePolytreeSceneDescriptor {
    AZStd::vector<ScenePolytreeNodeDescriptor> m_permanentNodes;
    AZStd::vector<ScenePolytreePartitionDescriptor> m_partitions;
    AZ::s64 m_fixedStepNanoseconds{16'666'667};
    AZ::u32 m_maxCatchUpSteps{4};
};

struct ScenePolytreeEntityBinding {
    AZ::Name m_bindingId;
    AZ::EntityId m_entity;
    AZ::Transform m_nodeToEntity{AZ::Transform::CreateIdentity()};
};

struct RegistrationResult {
    ScenePolytreeResultCode m_code{ScenePolytreeResultCode::Success};
    RegistrationToken m_token;
    [[nodiscard]] bool IsSuccess() const noexcept {
        return m_code == ScenePolytreeResultCode::Success && m_token.IsValid();
    }
};

struct SlotResult {
    ScenePolytreeResultCode m_code{ScenePolytreeResultCode::Success};
    SlotHandle m_handle;
    [[nodiscard]] bool IsSuccess() const noexcept {
        return m_code == ScenePolytreeResultCode::Success && m_handle.IsValid();
    }
};

struct NodeResult {
    ScenePolytreeResultCode m_code{ScenePolytreeResultCode::Success};
    SceneNodeHandle m_handle;
    [[nodiscard]] bool IsSuccess() const noexcept {
        return m_code == ScenePolytreeResultCode::Success && m_handle.IsValid();
    }
};

struct ResolvedScenePolytreeRegistration {
    RegistrationToken m_token;
    AZ::EntityId m_ownerEntity;
    AZ::u64 m_partition{};
    ScenePolytreePrefabRegistrationDescriptor m_descriptor;
};
} // namespace ScenePolytree

namespace AZ {
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::ScenePolytreeLifecycle,
                        "{15026EB7-CA72-476B-91D5-88467DA95029}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::ScenePolytreeNodeType,
                        "{CB4464DB-4EB4-4770-873F-4CB42AA192F3}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::ScenePolytreeJointType,
                        "{5122D2AD-6214-4FBA-A652-25314D534E10}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::ScenePolytreeResultCode,
                        "{051E15A2-A957-4709-907E-CC5CC4DDA5A0}");
} // namespace AZ
