#pragma once

#include <ScenePolytree/ScenePolytreeSpawnerTypes.h>

#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Name/Name.h>
#include <AzCore/RTTI/RTTI.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>

namespace ScenePolytree {
using ScenePolytreeControllerTypeId = AZ::TypeId;

enum class ScenePolytreeControllerTargetAccess : AZ::u8 { ReadOnly, ReadWrite };

enum class ScenePolytreeControllerResultCode : AZ::u8 {
    Success,
    InvalidHandle,
    StaleHandle,
    InputClosed,
    InvalidInput,
    InputQueueFull,
    InvalidTarget,
    ReadOnlyTarget,
    InvalidConfiguration,
    ConstructionFailed,
    FactoryNotFound,
    FactoryAlreadyRegistered,
    FactoryInUse,
};

struct ScenePolytreeControllerTargetToken {
    AZ::u32 m_index{};
    AZ::u32 m_generation{};
    [[nodiscard]] bool IsValid() const noexcept { return m_generation != 0; }
    friend bool operator==(const ScenePolytreeControllerTargetToken &,
                           const ScenePolytreeControllerTargetToken &) = default;
};

struct ScenePolytreeControllerStateHandle {
    AZ::u32 m_index{};
    AZ::u32 m_generation{};
    [[nodiscard]] bool IsValid() const noexcept { return m_generation != 0; }
    friend bool operator==(const ScenePolytreeControllerStateHandle &,
                           const ScenePolytreeControllerStateHandle &) = default;
};

struct ScenePolytreeControllerHandle {
    InstanceHandle m_instance;
    ScenePolytreeControllerTypeId m_typeId{AZ::TypeId::CreateNull()};
    ScenePolytreeControllerStateHandle m_state;
    [[nodiscard]] bool IsValid() const noexcept {
        return m_instance.IsValid() && !m_typeId.IsNull() && m_state.IsValid();
    }
    friend bool operator==(const ScenePolytreeControllerHandle &,
                           const ScenePolytreeControllerHandle &) = default;
};

struct ScenePolytreeControllerTargetReference {
    AZ::Name m_slot;
    AZ::EntityId m_prefabEntity;
    AZ::Name m_bindingId;
    ScenePolytreeControllerTargetAccess m_access{ScenePolytreeControllerTargetAccess::ReadWrite};
};

struct ScenePolytreeResolvedControllerTarget {
    AZ::Name m_slot;
    SceneNodeHandle m_node;
    ScenePolytreeControllerTargetToken m_token;
    ScenePolytreeControllerTargetAccess m_access{ScenePolytreeControllerTargetAccess::ReadWrite};
};

class ScenePolytreeControllerConfiguration {
  public:
    AZ_RTTI(ScenePolytreeControllerConfiguration, "{A608756C-A7B5-4F94-A77B-9719CE84ED08}");
    virtual ~ScenePolytreeControllerConfiguration() = default;
    [[nodiscard]] virtual AZStd::shared_ptr<const ScenePolytreeControllerConfiguration>
    Clone() const = 0;
};

class ScenePolytreeControllerInput {
  public:
    AZ_RTTI(ScenePolytreeControllerInput, "{C47C2DC7-5FEA-47C3-92AC-38612BA9317C}");
    virtual ~ScenePolytreeControllerInput() = default;
};

struct ScenePolytreeControllerDeclaration {
    AZ::Name m_declarationId;
    ScenePolytreeControllerTypeId m_typeId{AZ::TypeId::CreateNull()};
    AZ::s32 m_executionOrder{};
    AZ::Name m_providerBindingId;
    AZStd::shared_ptr<const ScenePolytreeControllerConfiguration> m_configuration;
    AZStd::vector<ScenePolytreeControllerTargetReference> m_targets;
};

struct ScenePolytreeControllerCreateResult {
    ScenePolytreeControllerResultCode m_code{ScenePolytreeControllerResultCode::Success};
    ScenePolytreeControllerStateHandle m_state;
    [[nodiscard]] bool IsSuccess() const noexcept {
        return m_code == ScenePolytreeControllerResultCode::Success && m_state.IsValid();
    }
};

struct ScenePolytreeControllerLookupResult {
    ScenePolytreeControllerResultCode m_code{ScenePolytreeControllerResultCode::Success};
    ScenePolytreeControllerHandle m_handle;
    [[nodiscard]] bool IsSuccess() const noexcept {
        return m_code == ScenePolytreeControllerResultCode::Success && m_handle.IsValid();
    }
};

struct ScenePolytreeControllerFactoryRegistrationToken {
    ScenePolytreeControllerTypeId m_typeId{AZ::TypeId::CreateNull()};
    AZ::u32 m_generation{};
    [[nodiscard]] bool IsValid() const noexcept { return !m_typeId.IsNull() && m_generation != 0; }
    friend bool operator==(const ScenePolytreeControllerFactoryRegistrationToken &,
                           const ScenePolytreeControllerFactoryRegistrationToken &) = default;
};

struct ScenePolytreeControllerFactoryRegistrationResult {
    ScenePolytreeControllerResultCode m_code{ScenePolytreeControllerResultCode::Success};
    ScenePolytreeControllerFactoryRegistrationToken m_token;
    [[nodiscard]] bool IsSuccess() const noexcept {
        return m_code == ScenePolytreeControllerResultCode::Success && m_token.IsValid();
    }
};
} // namespace ScenePolytree

namespace AZ {
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::ScenePolytreeControllerTargetAccess,
                        "{93C94F22-E9B0-4A83-B35D-657531B4CE26}");
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::ScenePolytreeControllerResultCode,
                        "{C53EEDFB-3B6C-4EC2-B8A0-726073C2550C}");
} // namespace AZ
