#include <ScenePolytree/ScenePolytreeSpawnerComponent.h>

#include "PrefabTopology.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/parallel/mutex.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>

#include <algorithm>
#include <ranges>

namespace ScenePolytree {
namespace {
enum class SpawnBoundary : AZ::u8 { Cancelable, Cancelled, Committed };

struct SpawnCallbackState {
    SpawnCallbackState(AZStd::vector<ScenePolytreeNodeDescriptor> topology,
                       const AZ::Transform &placement)
        : m_topology(AZStd::move(topology)), m_placement(placement) {}

    AZStd::atomic_uint8_t m_boundary{static_cast<AZ::u8>(SpawnBoundary::Cancelable)};
    AZStd::atomic_uint8_t m_error{static_cast<AZ::u8>(SpawnError::None)};
    AZStd::vector<ScenePolytreeNodeDescriptor> m_topology;
    AZ::Transform m_placement{AZ::Transform::CreateIdentity()};
    AZStd::mutex m_hierarchyMutex;
    AZStd::vector<AZ::EntityId> m_hierarchyEntities;
};

struct CollectedBindings {
    SpawnError m_error{SpawnError::None};
    AZStd::vector<ScenePolytreeEntityBinding> m_bindings;
    AZStd::vector<ScenePolytreeControllerDeclaration> m_declarations;
};

void StorePreparationError(const AZStd::shared_ptr<SpawnCallbackState> &state, SpawnError error) {
    AZ::u8 expected = static_cast<AZ::u8>(SpawnError::None);
    (void)state->m_error.compare_exchange_strong(expected, static_cast<AZ::u8>(error));
}

[[nodiscard]] SpawnError HierarchyFailure(ScenePolytreeResultCode result) {
    switch (result) {
    case ScenePolytreeResultCode::DuplicateBindingId:
        return SpawnError::DuplicateBinding;
    case ScenePolytreeResultCode::InvalidTransform:
        return SpawnError::InvalidTransform;
    case ScenePolytreeResultCode::InvalidBinding:
        return SpawnError::InvalidBinding;
    default:
        return SpawnError::HierarchyMismatch;
    }
}

void PreparePrefabInstance(const AZStd::shared_ptr<SpawnCallbackState> &state,
                           AzFramework::SpawnableEntityContainerView entities) {
    AZ::u8 expectedBoundary = static_cast<AZ::u8>(SpawnBoundary::Cancelable);
    if (!state->m_boundary.compare_exchange_strong(expectedBoundary,
                                                   static_cast<AZ::u8>(SpawnBoundary::Committed))) {
        return;
    }

    auto hierarchy = Internal::ExtractPrefabTopology(entities);
    if (!hierarchy.IsSuccess()) {
        StorePreparationError(state, HierarchyFailure(hierarchy.m_failure.m_code));
        return;
    }
    if (hierarchy.m_nodes.size() != state->m_topology.size()) {
        StorePreparationError(state, SpawnError::HierarchyMismatch);
        return;
    }

    AZStd::vector<AZ::Transform> logicalWorlds(state->m_topology.size(),
                                               AZ::Transform::CreateIdentity());
    bool valid = true;
    std::ranges::for_each(
        std::views::iota(std::size_t{}, state->m_topology.size()), [&](std::size_t index) {
            if (!valid) {
                return;
            }
            const auto &registered = state->m_topology[index];
            const auto &node = hierarchy.m_nodes[index];
            if (node.m_bindingId != registered.m_bindingId ||
                node.m_parentBindingId != registered.m_parentBindingId) {
                StorePreparationError(state, SpawnError::HierarchyMismatch);
                valid = false;
                return;
            }
            const AZ::EntityId entityId = hierarchy.m_entities[index];
            const auto found = std::ranges::find_if(entities, [&](const AZ::Entity *candidate) {
                return candidate != nullptr && candidate->GetId() == entityId;
            });
            AZ::Entity *entity = found != entities.end() ? *found : nullptr;
            auto *transform = entity != nullptr
                                  ? entity->FindComponent<AzFramework::TransformComponent>()
                                  : nullptr;
            if (transform == nullptr) {
                StorePreparationError(state, SpawnError::InvalidTransform);
                valid = false;
                return;
            }
            AZ::Transform parentWorld = state->m_placement;
            if (!node.m_parentBindingId.IsEmpty()) {
                const auto parent = std::ranges::find(state->m_topology, node.m_parentBindingId,
                                                      &ScenePolytreeNodeDescriptor::m_bindingId);
                if (parent == state->m_topology.end()) {
                    StorePreparationError(state, SpawnError::InvalidBinding);
                    valid = false;
                    return;
                }
                parentWorld =
                    logicalWorlds[static_cast<std::size_t>(parent - state->m_topology.begin())];
            }
            logicalWorlds[index] = parentWorld * node.m_initialLocal;
            transform->SetParent(AZ::EntityId{});
            transform->SetWorldTM(logicalWorlds[index]);
            if (transform->GetParentId().IsValid()) {
                StorePreparationError(state, SpawnError::CompetingTransformParent);
                valid = false;
            }
        });
    if (valid) {
        AZStd::scoped_lock lock(state->m_hierarchyMutex);
        state->m_hierarchyEntities = AZStd::move(hierarchy.m_entities);
    }
}

[[nodiscard]] CollectedBindings
CollectPrefabBindings(const AZStd::shared_ptr<SpawnCallbackState> &state,
                      AzFramework::SpawnableConstEntityContainerView entities) {
    const auto boundary = static_cast<SpawnBoundary>(state->m_boundary.load());
    if (boundary == SpawnBoundary::Cancelled) {
        return {SpawnError::Cancelled, {}};
    }
    const auto preparationError = static_cast<SpawnError>(state->m_error.load());
    if (preparationError != SpawnError::None) {
        return {preparationError, {}};
    }
    if (entities.empty()) {
        return {SpawnError::O3deSpawnFailed, {}};
    }

    AZStd::vector<AZ::EntityId> hierarchyEntities;
    {
        AZStd::scoped_lock lock(state->m_hierarchyMutex);
        hierarchyEntities = state->m_hierarchyEntities;
    }
    if (hierarchyEntities.size() != state->m_topology.size()) {
        return {SpawnError::HierarchyMismatch, {}};
    }

    CollectedBindings result;
    result.m_bindings.reserve(state->m_topology.size());
    std::ranges::for_each(
        std::views::iota(std::size_t{}, state->m_topology.size()), [&](std::size_t index) {
            if (result.m_error != SpawnError::None) {
                return;
            }
            const auto &node = state->m_topology[index];
            const AZ::EntityId entityId = hierarchyEntities[index];
            const auto matches = std::ranges::count_if(entities, [&](const AZ::Entity *candidate) {
                return candidate != nullptr && candidate->GetId() == entityId;
            });
            if (matches != 1) {
                result.m_error =
                    matches == 0 ? SpawnError::HierarchyMismatch : SpawnError::DuplicateBinding;
                return;
            }
            const auto found = std::ranges::find_if(entities, [&](const AZ::Entity *candidate) {
                return candidate != nullptr && candidate->GetId() == entityId;
            });
            const AZ::Entity *entity = found != entities.end() ? *found : nullptr;
            const auto *transform = entity != nullptr
                                        ? entity->FindComponent<AzFramework::TransformComponent>()
                                        : nullptr;
            if (entity == nullptr || !entity->GetId().IsValid()) {
                result.m_error = SpawnError::InvalidBinding;
            } else if (entity->GetState() != AZ::Entity::State::Active) {
                result.m_error = SpawnError::InactiveEntity;
            } else if (transform == nullptr) {
                result.m_error = SpawnError::InvalidTransform;
            } else if (const_cast<AzFramework::TransformComponent *>(transform)
                           ->GetParentId()
                           .IsValid()) {
                result.m_error = SpawnError::CompetingTransformParent;
            } else if (std::ranges::any_of(result.m_bindings, [&](const auto &binding) {
                           return binding.m_entity == entity->GetId();
                       })) {
                result.m_error = SpawnError::DuplicateBinding;
            } else {
                result.m_bindings.push_back(
                    {node.m_bindingId, entity->GetId(), AZ::Transform::CreateIdentity()});
            }
        });
    if (result.m_error == SpawnError::None) {
        std::ranges::for_each(entities, [&](const AZ::Entity *entity) {
            if (result.m_error != SpawnError::None || entity == nullptr) {
                return;
            }
            const auto hierarchyEntity = std::ranges::find(hierarchyEntities, entity->GetId());
            const AZ::Name providerBinding =
                hierarchyEntity != hierarchyEntities.end()
                    ? state
                          ->m_topology[static_cast<std::size_t>(hierarchyEntity -
                                                                hierarchyEntities.begin())]
                          .m_bindingId
                    : AZ::Name{};
            std::ranges::for_each(entity->GetComponents(), [&](const AZ::Component *component) {
                if (result.m_error != SpawnError::None || component == nullptr) {
                    return;
                }
                const auto *provider =
                    azrtti_cast<const ScenePolytreeBehaviorProvider *>(component);
                if (provider == nullptr) {
                    return;
                }
                auto declaration = provider->CopyScenePolytreeControllerDeclaration();
                auto configuration = declaration.m_configuration != nullptr
                                         ? declaration.m_configuration->Clone()
                                         : nullptr;
                const bool invalidTarget =
                    declaration.m_targets.empty() ||
                    std::ranges::any_of(declaration.m_targets, [&](const auto &target) {
                        return !target.m_prefabEntity.IsValid() || !target.m_bindingId.IsEmpty() ||
                               std::ranges::count_if(entities, [&](const AZ::Entity *candidate) {
                                   return candidate != nullptr &&
                                          candidate->GetId() == target.m_prefabEntity;
                               }) != 1;
                    });
                if (providerBinding.IsEmpty() || declaration.m_declarationId.IsEmpty() ||
                    declaration.m_typeId.IsNull() || configuration == nullptr || invalidTarget) {
                    result.m_error = SpawnError::InvalidControllerConfiguration;
                    return;
                }
                declaration.m_configuration = AZStd::move(configuration);
                declaration.m_providerBindingId = providerBinding;
                result.m_declarations.push_back(AZStd::move(declaration));
            });
        });
    }
    if (result.m_error != SpawnError::None) {
        result.m_bindings.clear();
        result.m_declarations.clear();
    }
    return result;
}

[[nodiscard]] ScenePolytreeSpawnFailure SceneFailure(ScenePolytreeResultCode result) {
    return {SpawnError::SceneCommandFailed, result, ScenePolytreeResultCode::Success};
}

[[nodiscard]] ScenePolytreeSpawnFailure ControllerFailure(ScenePolytreeResultCode result) {
    const auto error = result == ScenePolytreeResultCode::ControllerFactoryNotFound
                           ? SpawnError::ControllerFactoryNotFound
                       : result == ScenePolytreeResultCode::ControllerInvalidConfiguration
                           ? SpawnError::InvalidControllerConfiguration
                       : result == ScenePolytreeResultCode::ControllerTargetNotFound
                           ? SpawnError::ControllerTargetNotFound
                       : result == ScenePolytreeResultCode::ControllerTargetOutsideInstance
                           ? SpawnError::CrossInstanceControllerTarget
                       : result == ScenePolytreeResultCode::ControllerWriteConflict
                           ? SpawnError::ControllerWriteConflict
                       : result == ScenePolytreeResultCode::ControllerConstructionFailed
                           ? SpawnError::ControllerConstructionFailed
                           : SpawnError::SceneCommandFailed;
    return {error, result, ScenePolytreeResultCode::Success};
}
} // namespace

struct ScenePolytreeSpawnerComponent::RuntimeState {
    enum class InstanceState : AZ::u8 {
        Resetting,
        Placing,
        Spawning,
        Binding,
        AttachingControllers,
        Active,
        DetachingControllers,
        Unbinding,
        DespawningEntities,
        CleanupResetting,
        Releasing,
    };

    struct InstanceRecord {
        SpawnRequestId m_spawnRequest;
        ScenePolytreeSpawnRequest m_request;
        InstanceHandle m_instance;
        AZ::Transform m_placement{AZ::Transform::CreateIdentity()};
        InstanceState m_state{InstanceState::Resetting};
        SceneCommandId m_pendingCommand;
        SceneCommandType m_pendingType{SceneCommandType::ResetSlot};
        AZStd::unique_ptr<AzFramework::EntitySpawnTicket> m_ticket;
        AZStd::shared_ptr<SpawnCallbackState> m_callbackState;
        AZStd::vector<ScenePolytreeControllerDeclaration> m_declarations;
        ScenePolytreeSpawnFailure m_spawnFailure;
        DespawnRequestId m_despawnRequest;
        ScenePolytreeDespawnFailure m_despawnFailure;
        bool m_terminalSpawnFailure{};
        bool m_controllersAttached{};
    };

    [[nodiscard]] InstanceRecord *Find(SpawnRequestId request) {
        const auto found = m_instances.find(request.m_value);
        return request.m_spawnerGeneration == m_generation && found != m_instances.end()
                   ? found->second.get()
                   : nullptr;
    }

    [[nodiscard]] InstanceRecord *Find(InstanceHandle instance) {
        const auto found = std::ranges::find_if(
            m_instances, [&](const auto &entry) { return entry.second->m_instance == instance; });
        return instance.m_spawnerGeneration == m_generation && found != m_instances.end()
                   ? found->second.get()
                   : nullptr;
    }

    [[nodiscard]] InstanceRecord *Find(SceneCommandId command) {
        const auto found = std::ranges::find_if(m_instances, [&](const auto &entry) {
            return entry.second->m_pendingCommand == command;
        });
        return found != m_instances.end() ? found->second.get() : nullptr;
    }

    ScenePolytreeSpawnerLifecycle m_lifecycle{ScenePolytreeSpawnerLifecycle::Inactive};
    AZ::u32 m_generation{};
    AZ::u64 m_nextSpawnRequest{1};
    AZ::u64 m_nextDespawnRequest{1};
    RegistrationToken m_registration;
    SpawnerHandle m_spawner;
    AZStd::vector<ScenePolytreeNodeDescriptor> m_topology;
    AZStd::unordered_map<AZ::u64, AZStd::unique_ptr<InstanceRecord>> m_instances;
};

void ScenePolytreeSpawnerConfig::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Enum<SpawnTriggerMode>()
            ->Value("OnReady", SpawnTriggerMode::OnReady)
            ->Value("ExternalRequestsOnly", SpawnTriggerMode::ExternalRequestsOnly)
            ->Value("OnReadyAndExternalRequests", SpawnTriggerMode::OnReadyAndExternalRequests);
        serializeContext->Enum<DefaultPlacement>()->Value("SpawnerWorldTransform",
                                                          DefaultPlacement::SpawnerWorldTransform);
        serializeContext->Class<ScenePolytreeSpawnerConfig, AZ::ComponentConfig>()
            ->Version(1)
            ->Field("SceneEntity", &ScenePolytreeSpawnerConfig::m_sceneEntity)
            ->Field("Prefab", &ScenePolytreeSpawnerConfig::m_prefab)
            ->Field("Capacity", &ScenePolytreeSpawnerConfig::m_capacity)
            ->Field("DefaultPlacement", &ScenePolytreeSpawnerConfig::m_defaultPlacement)
            ->Field("TriggerMode", &ScenePolytreeSpawnerConfig::m_triggerMode)
            ->Field("InitialSpawnCount", &ScenePolytreeSpawnerConfig::m_initialSpawnCount);

        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<ScenePolytreeSpawnerConfig>("Scene Polytree Spawner Configuration",
                                                    "Prefab-backed logical slot settings.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Visibility,
                            AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeSpawnerConfig::m_sceneEntity, "Scene",
                              "Optional explicit Scene Polytree entity; empty uses the default.")
                ->DataElement(AZ::Edit::UIHandlers::Default, &ScenePolytreeSpawnerConfig::m_prefab,
                              "Prefab",
                              "Prefab whose Transform hierarchy defines the logical topology.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeSpawnerConfig::m_capacity, "Capacity",
                              "Maximum concurrently allocated Prefab instances.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeSpawnerConfig::m_defaultPlacement, "Default Placement",
                              "Placement used without an explicit transform.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeSpawnerConfig::m_triggerMode, "Trigger Mode",
                              "Controls initial and external spawn requests.")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default, &ScenePolytreeSpawnerConfig::m_initialSpawnCount,
                    "Initial Spawn Count", "Instances requested when the spawner is ready.");
        }
    }
}

void ScenePolytreeSpawnerComponent::Reflect(AZ::ReflectContext *context) {
    ScenePolytreeSpawnerConfig::Reflect(context);
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<ScenePolytreeSpawnerComponent, AZ::Component>()->Version(1)->Field(
            "Configuration", &ScenePolytreeSpawnerComponent::m_configuration);
        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<ScenePolytreeSpawnerComponent>(
                    "Scene Polytree Spawner",
                    "Registers and asynchronously spawns one Prefab topology.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeSpawnerComponent::m_configuration, "Configuration",
                              "Prefab registration and spawn settings.");
        }
    }
}

void ScenePolytreeSpawnerComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeSpawnerService"));
}

void ScenePolytreeSpawnerComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeSpawnerService"));
}

void ScenePolytreeSpawnerComponent::GetRequiredServices(
    AZ::ComponentDescriptor::DependencyArrayType &required) {
    required.push_back(AZ_CRC_CE("TransformService"));
}

ScenePolytreeSpawnerComponent::ScenePolytreeSpawnerComponent()
    : m_runtime(AZStd::make_unique<RuntimeState>()) {}

ScenePolytreeSpawnerComponent::ScenePolytreeSpawnerComponent(
    const ScenePolytreeSpawnerConfig &configuration)
    : m_configuration(configuration), m_runtime(AZStd::make_unique<RuntimeState>()) {}

ScenePolytreeSpawnerComponent::~ScenePolytreeSpawnerComponent() = default;

void ScenePolytreeSpawnerComponent::Activate() {
    if (++m_runtime->m_generation == 0) {
        ++m_runtime->m_generation;
    }
    m_runtime->m_lifecycle = ScenePolytreeSpawnerLifecycle::Registering;
    m_runtime->m_registration = {};
    m_runtime->m_spawner = {};
    m_runtime->m_topology.clear();
    m_runtime->m_instances.clear();

    ScenePolytreeSpawnerRequestBus::Handler::BusConnect(GetEntityId());
    ScenePolytreeRegistrationNotificationBus::Handler::BusConnect(GetEntityId());
    ScenePolytreeCommandNotificationBus::Handler::BusConnect(GetEntityId());
    Internal::ScenePolytreeSpawnerAsyncNotificationBus::Handler::BusConnect(GetEntityId());

    if (!m_configuration.m_prefab.GetId().IsValid()) {
        EnterFailure(SpawnError::InvalidPrefab, {ScenePolytreeResultCode::InvalidPrefab});
        return;
    }
    if (m_configuration.m_capacity == 0) {
        EnterFailure(SpawnError::InvalidConfiguration, {ScenePolytreeResultCode::ZeroCapacity});
        return;
    }
    if (m_configuration.m_initialSpawnCount > m_configuration.m_capacity) {
        EnterFailure(SpawnError::InvalidConfiguration,
                     {ScenePolytreeResultCode::InvalidConfiguration});
        return;
    }
    if (m_configuration.m_defaultPlacement != DefaultPlacement::SpawnerWorldTransform ||
        (m_configuration.m_triggerMode != SpawnTriggerMode::OnReady &&
         m_configuration.m_triggerMode != SpawnTriggerMode::ExternalRequestsOnly &&
         m_configuration.m_triggerMode != SpawnTriggerMode::OnReadyAndExternalRequests)) {
        EnterFailure(SpawnError::InvalidConfiguration,
                     {ScenePolytreeResultCode::InvalidConfiguration});
        return;
    }

    auto *registry = AZ::Interface<ScenePolytreeRegistrationRequests>::Get();
    if (registry == nullptr) {
        EnterFailure(SpawnError::NotReady, {ScenePolytreeResultCode::SceneNotFound});
        return;
    }
    const auto registered = registry->RegisterPrefab(
        GetEntityId(), m_configuration.m_sceneEntity,
        {m_configuration.m_prefab, m_configuration.m_capacity, AZ::Name("ScenePolytreeSpawner")});
    if (!registered.IsSuccess()) {
        EnterFailure(registered.m_code == ScenePolytreeResultCode::InvalidPrefab
                         ? SpawnError::InvalidPrefab
                         : SpawnError::NotReady,
                     {registered.m_code});
        return;
    }
    m_runtime->m_registration = registered.m_token;
}

void ScenePolytreeSpawnerComponent::Deactivate() {
    if (m_runtime->m_lifecycle == ScenePolytreeSpawnerLifecycle::Inactive) {
        return;
    }
    m_runtime->m_lifecycle = ScenePolytreeSpawnerLifecycle::Deactivating;
    if (++m_runtime->m_generation == 0) {
        ++m_runtime->m_generation;
    }

    auto *sceneRequests = AZ::Interface<ScenePolytreeRequests>::Get();
    auto *controllerRequests = AZ::Interface<ScenePolytreeControllerLifecycleRequests>::Get();
    auto *spawnRequests = AzFramework::SpawnableEntitiesInterface::Get();
    std::ranges::for_each(m_runtime->m_instances, [&](auto &entry) {
        auto &record = *entry.second;
        if (record.m_callbackState != nullptr) {
            AZ::u8 expected = static_cast<AZ::u8>(SpawnBoundary::Cancelable);
            (void)record.m_callbackState->m_boundary.compare_exchange_strong(
                expected, static_cast<AZ::u8>(SpawnBoundary::Cancelled));
        }
        if (controllerRequests != nullptr && record.m_controllersAttached) {
            (void)controllerRequests->DestroyControllersImmediately(record.m_instance);
        }
        if (sceneRequests != nullptr) {
            (void)sceneRequests->UnbindSlot(record.m_instance.m_slot);
            (void)sceneRequests->ResetSlot(record.m_instance.m_slot);
            (void)sceneRequests->ReleaseSlot(record.m_instance.m_slot);
        }
        if (spawnRequests != nullptr && record.m_ticket != nullptr && record.m_ticket->IsValid()) {
            spawnRequests->DespawnAllEntities(*record.m_ticket);
        }
        record.m_ticket.reset();
    });
    m_runtime->m_instances.clear();

    if (auto *registry = AZ::Interface<ScenePolytreeRegistrationRequests>::Get()) {
        registry->UnregisterPrefab(m_runtime->m_registration);
    }
    Internal::ScenePolytreeSpawnerAsyncNotificationBus::Handler::BusDisconnect();
    ScenePolytreeCommandNotificationBus::Handler::BusDisconnect();
    ScenePolytreeRegistrationNotificationBus::Handler::BusDisconnect();
    ScenePolytreeSpawnerRequestBus::Handler::BusDisconnect();
    m_runtime->m_registration = {};
    m_runtime->m_spawner = {};
    m_runtime->m_topology.clear();
    m_runtime->m_lifecycle = ScenePolytreeSpawnerLifecycle::Inactive;
}

bool ScenePolytreeSpawnerComponent::ReadInConfig(const AZ::ComponentConfig *baseConfig) {
    if (const auto *configuration = azrtti_cast<const ScenePolytreeSpawnerConfig *>(baseConfig)) {
        m_configuration = *configuration;
        return true;
    }
    return false;
}

bool ScenePolytreeSpawnerComponent::WriteOutConfig(AZ::ComponentConfig *outBaseConfig) const {
    if (auto *configuration = azrtti_cast<ScenePolytreeSpawnerConfig *>(outBaseConfig)) {
        *configuration = m_configuration;
        return true;
    }
    return false;
}

SpawnRequestId ScenePolytreeSpawnerComponent::AllocateSpawnRequest() {
    return {m_runtime->m_generation, m_runtime->m_nextSpawnRequest++};
}

DespawnRequestId ScenePolytreeSpawnerComponent::AllocateDespawnRequest() {
    return {m_runtime->m_generation, m_runtime->m_nextDespawnRequest++};
}

SpawnRequestId ScenePolytreeSpawnerComponent::Spawn(const ScenePolytreeSpawnRequest &request) {
    const SpawnRequestId requestId = AllocateSpawnRequest();
    if (m_runtime->m_lifecycle == ScenePolytreeSpawnerLifecycle::Deactivating ||
        m_runtime->m_lifecycle == ScenePolytreeSpawnerLifecycle::Inactive) {
        QueueSpawnFailure(requestId, {SpawnError::ShuttingDown}, request.m_context);
    } else if (m_runtime->m_lifecycle != ScenePolytreeSpawnerLifecycle::Ready) {
        QueueSpawnFailure(requestId, {SpawnError::NotReady}, request.m_context);
    } else if (m_configuration.m_triggerMode == SpawnTriggerMode::OnReady) {
        QueueSpawnFailure(requestId, {SpawnError::ExternalRequestsDisabled}, request.m_context);
    } else {
        BeginSpawn(requestId, request);
    }
    return requestId;
}

DespawnRequestId ScenePolytreeSpawnerComponent::Despawn(InstanceHandle instance) {
    const DespawnRequestId requestId = AllocateDespawnRequest();
    if (m_runtime->m_lifecycle == ScenePolytreeSpawnerLifecycle::Deactivating ||
        m_runtime->m_lifecycle == ScenePolytreeSpawnerLifecycle::Inactive) {
        QueueDespawnFailure(requestId, instance, {DespawnError::ShuttingDown});
        return requestId;
    }
    if (m_runtime->m_lifecycle != ScenePolytreeSpawnerLifecycle::Ready) {
        QueueDespawnFailure(requestId, instance, {DespawnError::NotReady});
        return requestId;
    }
    auto *record = m_runtime->Find(instance);
    if (record == nullptr) {
        QueueDespawnFailure(requestId, instance, {DespawnError::StaleInstance});
        return requestId;
    }
    if (record->m_state != RuntimeState::InstanceState::Active) {
        QueueDespawnFailure(requestId, instance, {DespawnError::InstanceNotActive});
        return requestId;
    }
    record->m_terminalSpawnFailure = false;
    record->m_despawnRequest = requestId;
    if (record->m_controllersAttached) {
        SubmitDetachControllers(record->m_spawnRequest);
    } else {
        SubmitUnbind(record->m_spawnRequest);
    }
    return requestId;
}

bool ScenePolytreeSpawnerComponent::CancelSpawn(SpawnRequestId request) {
    auto *record = m_runtime->Find(request);
    if (record == nullptr || record->m_state == RuntimeState::InstanceState::Active ||
        record->m_terminalSpawnFailure || record->m_callbackState == nullptr) {
        return false;
    }
    AZ::u8 expected = static_cast<AZ::u8>(SpawnBoundary::Cancelable);
    return record->m_callbackState->m_boundary.compare_exchange_strong(
        expected, static_cast<AZ::u8>(SpawnBoundary::Cancelled));
}

ScenePolytreeSpawnerLifecycle ScenePolytreeSpawnerComponent::GetSpawnerLifecycle() const {
    return m_runtime->m_lifecycle;
}

void ScenePolytreeSpawnerComponent::EnterFailure(SpawnError error,
                                                 const ScenePolytreeFailure &registrationFailure) {
    m_runtime->m_lifecycle = ScenePolytreeSpawnerLifecycle::Failed;
    const AZ::EntityId entityId = GetEntityId();
    const ScenePolytreeSpawnerFailure failure{error, registrationFailure};
    AZ::TickBus::QueueFunction([entityId, failure]() {
        ScenePolytreeSpawnerNotificationBus::Event(
            entityId, &ScenePolytreeSpawnerNotifications::OnScenePolytreeSpawnerFailed, failure);
    });
}

void ScenePolytreeSpawnerComponent::OnScenePolytreeRegistrationReady(RegistrationToken token,
                                                                     SpawnerHandle spawner) {
    if (m_runtime->m_lifecycle != ScenePolytreeSpawnerLifecycle::Registering ||
        token != m_runtime->m_registration || !spawner.IsValid()) {
        return;
    }
    if (!m_configuration.m_prefab.IsReady() && AZ::Data::AssetManager::IsReady()) {
        m_configuration.m_prefab =
            AZ::Data::AssetManager::Instance().FindAsset<AzFramework::Spawnable>(
                m_configuration.m_prefab.GetId(), AZ::Data::AssetLoadBehavior::Default);
    }
    if (!m_configuration.m_prefab.IsReady()) {
        EnterFailure(
            SpawnError::InvalidPrefab,
            {ScenePolytreeResultCode::AssetLoadFailed, {}, m_configuration.m_prefab.GetId(), {}});
        return;
    }
    auto topology = Internal::ExtractPrefabTopology(*m_configuration.m_prefab);
    if (!topology.IsSuccess()) {
        EnterFailure(SpawnError::InvalidPrefab, topology.m_failure);
        return;
    }
    m_runtime->m_spawner = spawner;
    m_runtime->m_topology = AZStd::move(topology.m_nodes);
    m_runtime->m_lifecycle = ScenePolytreeSpawnerLifecycle::Ready;
    ScenePolytreeSpawnerNotificationBus::Event(
        GetEntityId(), &ScenePolytreeSpawnerNotifications::OnScenePolytreeSpawnerReady, spawner);
    BeginInitialSpawns();
}

void ScenePolytreeSpawnerComponent::OnScenePolytreeRegistrationFailed(
    RegistrationToken token, const ScenePolytreeFailure &failure) {
    if (token == m_runtime->m_registration &&
        m_runtime->m_lifecycle == ScenePolytreeSpawnerLifecycle::Registering) {
        EnterFailure(failure.m_code == ScenePolytreeResultCode::InvalidPrefab
                         ? SpawnError::InvalidPrefab
                         : SpawnError::NotReady,
                     failure);
    }
}

void ScenePolytreeSpawnerComponent::BeginInitialSpawns() {
    if (m_configuration.m_triggerMode == SpawnTriggerMode::ExternalRequestsOnly ||
        m_configuration.m_initialSpawnCount == 0) {
        return;
    }
    AZ::Transform placement = AZ::Transform::CreateIdentity();
    AZ::TransformBus::EventResult(placement, GetEntityId(), &AZ::TransformBus::Events::GetWorldTM);
    const auto indices = std::views::iota(AZ::u32{}, m_configuration.m_initialSpawnCount);
    std::ranges::for_each(indices, [&](AZ::u32 index) {
        ScenePolytreeSpawnRequest request;
        request.m_context = {GetEntityId(), index};
        BeginSpawn(AllocateSpawnRequest(), request, &placement);
    });
}

void ScenePolytreeSpawnerComponent::BeginSpawn(SpawnRequestId requestId,
                                               const ScenePolytreeSpawnRequest &request,
                                               const AZ::Transform *initialPlacement) {
    AZ::Transform placement = AZ::Transform::CreateIdentity();
    if (initialPlacement != nullptr) {
        placement = *initialPlacement;
    } else if (request.m_hasExplicitWorldTransform) {
        placement = request.m_worldTransform;
    } else {
        AZ::TransformBus::EventResult(placement, GetEntityId(),
                                      &AZ::TransformBus::Events::GetWorldTM);
    }
    if (!placement.IsFinite()) {
        QueueSpawnFailure(requestId, {SpawnError::InvalidPlacement}, request.m_context);
        return;
    }

    auto *sceneRequests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (sceneRequests == nullptr) {
        QueueSpawnFailure(requestId, {SpawnError::NotReady}, request.m_context);
        return;
    }
    const SlotResult reserved = sceneRequests->ReserveSlot(m_runtime->m_spawner);
    if (!reserved.IsSuccess()) {
        QueueSpawnFailure(requestId,
                          {reserved.m_code == ScenePolytreeResultCode::SlotUnavailable
                               ? SpawnError::NoCapacity
                               : SpawnError::SceneCommandFailed,
                           reserved.m_code},
                          request.m_context);
        return;
    }

    auto record = AZStd::make_unique<RuntimeState::InstanceRecord>();
    record->m_spawnRequest = requestId;
    record->m_request = request;
    record->m_instance = {reserved.m_handle, m_runtime->m_generation};
    record->m_placement = placement;
    record->m_callbackState =
        AZStd::make_shared<SpawnCallbackState>(m_runtime->m_topology, placement);
    m_runtime->m_instances.emplace(requestId.m_value, AZStd::move(record));
    SubmitReset(requestId);
}

void ScenePolytreeSpawnerComponent::SubmitReset(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        BeginSpawnFailure(requestId, {SpawnError::NotReady});
        return;
    }
    const auto submitted = requests->SubmitResetSlot(record->m_instance.m_slot, GetEntityId());
    if (!submitted.IsAccepted()) {
        BeginSpawnFailure(requestId, SceneFailure(submitted.m_code));
        return;
    }
    record->m_state = RuntimeState::InstanceState::Resetting;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::ResetSlot;
}

void ScenePolytreeSpawnerComponent::SubmitPlace(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        BeginSpawnFailure(requestId, {SpawnError::NotReady});
        return;
    }
    const auto submitted =
        requests->SubmitPlaceSlot(record->m_instance.m_slot, record->m_placement, GetEntityId());
    if (!submitted.IsAccepted()) {
        BeginSpawnFailure(requestId, SceneFailure(submitted.m_code));
        return;
    }
    record->m_state = RuntimeState::InstanceState::Placing;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::PlaceSlot;
}

void ScenePolytreeSpawnerComponent::BeginEntitySpawn(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    if (record == nullptr) {
        return;
    }
    if (static_cast<SpawnBoundary>(record->m_callbackState->m_boundary.load()) ==
        SpawnBoundary::Cancelled) {
        BeginSpawnFailure(requestId, {SpawnError::Cancelled});
        return;
    }
    auto *spawnRequests = AzFramework::SpawnableEntitiesInterface::Get();
    if (spawnRequests == nullptr) {
        BeginSpawnFailure(requestId, {SpawnError::SpawnServiceUnavailable});
        return;
    }
    record->m_ticket = AZStd::make_unique<AzFramework::EntitySpawnTicket>(m_configuration.m_prefab);
    if (!record->m_ticket->IsValid()) {
        BeginSpawnFailure(requestId, {SpawnError::O3deSpawnFailed});
        return;
    }

    const AZ::EntityId entityId = GetEntityId();
    const AZ::u32 generation = m_runtime->m_generation;
    const AZ::u32 ticketId = record->m_ticket->GetId();
    const auto callbackState = record->m_callbackState;
    AzFramework::SpawnAllEntitiesOptionalArgs optionalArgs;
    optionalArgs.m_preInsertionCallback =
        [callbackState]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id callbackTicket,
                        AzFramework::SpawnableEntityContainerView entities) {
            PreparePrefabInstance(callbackState, entities);
        };
    optionalArgs.m_completionCallback = [entityId, generation, requestId, ticketId, callbackState](
                                            [[maybe_unused]] AzFramework::EntitySpawnTicket::Id
                                                callbackTicket,
                                            AzFramework::SpawnableConstEntityContainerView
                                                entities) mutable {
        CollectedBindings collected = CollectPrefabBindings(callbackState, entities);
        AZ::TickBus::QueueFunction([entityId, generation, requestId, ticketId,
                                    error = collected.m_error,
                                    bindings = AZStd::move(collected.m_bindings),
                                    declarations =
                                        AZStd::move(collected.m_declarations)]() mutable {
            Internal::ScenePolytreeSpawnerAsyncNotificationBus::Event(
                entityId,
                &Internal::ScenePolytreeSpawnerAsyncNotifications::OnScenePolytreeSpawnCompleted,
                generation, requestId, ticketId, error, AZStd::move(bindings),
                AZStd::move(declarations));
        });
    };
    record->m_state = RuntimeState::InstanceState::Spawning;
    spawnRequests->SpawnAllEntities(*record->m_ticket, AZStd::move(optionalArgs));
}

void ScenePolytreeSpawnerComponent::OnScenePolytreeSpawnCompleted(
    AZ::u32 spawnerGeneration, SpawnRequestId requestId, AZ::u32 ticketId, SpawnError error,
    AZStd::vector<ScenePolytreeEntityBinding> bindings,
    AZStd::vector<ScenePolytreeControllerDeclaration> declarations) {
    auto *record = m_runtime->Find(requestId);
    if (spawnerGeneration != m_runtime->m_generation || record == nullptr ||
        record->m_state != RuntimeState::InstanceState::Spawning || record->m_ticket == nullptr ||
        record->m_ticket->GetId() != ticketId) {
        return;
    }
    if (error != SpawnError::None) {
        const auto sceneResult = error == SpawnError::InvalidTransform
                                     ? ScenePolytreeResultCode::InvalidTransform
                                 : error == SpawnError::InvalidControllerConfiguration
                                     ? ScenePolytreeResultCode::ControllerInvalidConfiguration
                                     : ScenePolytreeResultCode::InvalidBinding;
        BeginSpawnFailure(requestId, {error, sceneResult});
        return;
    }
    record->m_declarations = AZStd::move(declarations);
    SubmitBind(requestId, AZStd::move(bindings));
}

void ScenePolytreeSpawnerComponent::SubmitBind(SpawnRequestId requestId,
                                               AZStd::vector<ScenePolytreeEntityBinding> bindings) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        BeginSpawnFailure(requestId, {SpawnError::NotReady});
        return;
    }
    const auto submitted =
        requests->SubmitBindSlot(record->m_instance.m_slot, bindings, GetEntityId());
    if (!submitted.IsAccepted()) {
        BeginSpawnFailure(requestId, SceneFailure(submitted.m_code));
        return;
    }
    record->m_state = RuntimeState::InstanceState::Binding;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::BindSlot;
}

void ScenePolytreeSpawnerComponent::SubmitAttachControllers(
    SpawnRequestId requestId, AZStd::vector<ScenePolytreeControllerDeclaration> declarations) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeControllerLifecycleRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        BeginSpawnFailure(requestId, {SpawnError::NotReady});
        return;
    }
    const auto submitted = requests->SubmitAttachControllers(
        record->m_instance, AZStd::move(declarations), GetEntityId());
    if (!submitted.IsAccepted()) {
        BeginSpawnFailure(requestId, ControllerFailure(submitted.m_code));
        return;
    }
    record->m_state = RuntimeState::InstanceState::AttachingControllers;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::AttachControllers;
}

void ScenePolytreeSpawnerComponent::SubmitDetachControllers(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeControllerLifecycleRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        record->m_despawnFailure = {DespawnError::SceneCommandFailed,
                                    ScenePolytreeResultCode::SceneNotFound,
                                    ScenePolytreeResultCode::SceneNotFound};
        SubmitUnbind(requestId);
        return;
    }
    const auto submitted = requests->SubmitDetachControllers(record->m_instance, GetEntityId());
    if (!submitted.IsAccepted()) {
        (void)requests->DestroyControllersImmediately(record->m_instance);
        record->m_controllersAttached = false;
        record->m_despawnFailure = {DespawnError::SceneCommandFailed, submitted.m_code,
                                    submitted.m_code};
        SubmitUnbind(requestId);
        return;
    }
    record->m_state = RuntimeState::InstanceState::DetachingControllers;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::DetachControllers;
}

void ScenePolytreeSpawnerComponent::BeginSpawnFailure(SpawnRequestId requestId,
                                                      const ScenePolytreeSpawnFailure &failure) {
    auto *record = m_runtime->Find(requestId);
    if (record == nullptr || record->m_terminalSpawnFailure) {
        return;
    }
    record->m_terminalSpawnFailure = true;
    record->m_spawnFailure = failure;
    SubmitUnbind(requestId);
}

void ScenePolytreeSpawnerComponent::SubmitUnbind(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        if (record->m_terminalSpawnFailure) {
            record->m_spawnFailure.m_cleanupResult = ScenePolytreeResultCode::SceneNotFound;
        } else {
            record->m_despawnFailure = {DespawnError::SceneCommandFailed,
                                        ScenePolytreeResultCode::SceneNotFound,
                                        ScenePolytreeResultCode::SceneNotFound};
        }
        BeginEntityDespawn(requestId);
        return;
    }
    const auto submitted = requests->SubmitUnbindSlot(record->m_instance.m_slot, GetEntityId());
    if (!submitted.IsAccepted()) {
        if (record->m_terminalSpawnFailure) {
            record->m_spawnFailure.m_cleanupResult = submitted.m_code;
        } else {
            record->m_despawnFailure = {DespawnError::SceneCommandFailed, submitted.m_code,
                                        submitted.m_code};
        }
        BeginEntityDespawn(requestId);
        return;
    }
    record->m_state = RuntimeState::InstanceState::Unbinding;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::UnbindSlot;
}

void ScenePolytreeSpawnerComponent::BeginEntityDespawn(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    if (record == nullptr) {
        return;
    }
    if (record->m_ticket == nullptr || !record->m_ticket->IsValid()) {
        SubmitCleanupReset(requestId);
        return;
    }
    auto *spawnRequests = AzFramework::SpawnableEntitiesInterface::Get();
    if (spawnRequests == nullptr) {
        if (record->m_terminalSpawnFailure) {
            record->m_spawnFailure.m_cleanupResult = ScenePolytreeResultCode::ConstructionFailed;
        } else {
            record->m_despawnFailure = {DespawnError::DespawnServiceUnavailable};
        }
        record->m_ticket.reset();
        SubmitCleanupReset(requestId);
        return;
    }

    const AZ::EntityId entityId = GetEntityId();
    const AZ::u32 generation = m_runtime->m_generation;
    const AZ::u32 ticketId = record->m_ticket->GetId();
    AzFramework::DespawnAllEntitiesOptionalArgs optionalArgs;
    optionalArgs.m_completionCallback = [entityId, generation, requestId, ticketId](
                                            [[maybe_unused]] AzFramework::EntitySpawnTicket::Id
                                                callbackTicket) {
        AZ::TickBus::QueueFunction([entityId, generation, requestId, ticketId]() {
            Internal::ScenePolytreeSpawnerAsyncNotificationBus::Event(
                entityId,
                &Internal::ScenePolytreeSpawnerAsyncNotifications::OnScenePolytreeDespawnCompleted,
                generation, requestId, ticketId);
        });
    };
    record->m_state = RuntimeState::InstanceState::DespawningEntities;
    spawnRequests->DespawnAllEntities(*record->m_ticket, AZStd::move(optionalArgs));
}

void ScenePolytreeSpawnerComponent::OnScenePolytreeDespawnCompleted(AZ::u32 spawnerGeneration,
                                                                    SpawnRequestId requestId,
                                                                    AZ::u32 ticketId) {
    auto *record = m_runtime->Find(requestId);
    if (spawnerGeneration != m_runtime->m_generation || record == nullptr ||
        record->m_state != RuntimeState::InstanceState::DespawningEntities ||
        record->m_ticket == nullptr || record->m_ticket->GetId() != ticketId) {
        return;
    }
    record->m_ticket.reset();
    SubmitCleanupReset(requestId);
}

void ScenePolytreeSpawnerComponent::SubmitCleanupReset(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        if (record->m_terminalSpawnFailure) {
            record->m_spawnFailure.m_cleanupResult = ScenePolytreeResultCode::SceneNotFound;
        } else {
            record->m_despawnFailure.m_cleanupResult = ScenePolytreeResultCode::SceneNotFound;
        }
        SubmitRelease(requestId);
        return;
    }
    const auto submitted = requests->SubmitResetSlot(record->m_instance.m_slot, GetEntityId());
    if (!submitted.IsAccepted()) {
        if (record->m_terminalSpawnFailure) {
            record->m_spawnFailure.m_cleanupResult = submitted.m_code;
        } else {
            record->m_despawnFailure.m_cleanupResult = submitted.m_code;
        }
        SubmitRelease(requestId);
        return;
    }
    record->m_state = RuntimeState::InstanceState::CleanupResetting;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::ResetSlot;
}

void ScenePolytreeSpawnerComponent::SubmitRelease(SpawnRequestId requestId) {
    auto *record = m_runtime->Find(requestId);
    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (record == nullptr) {
        return;
    }
    if (requests == nullptr) {
        FinishRelease(requestId, ScenePolytreeResultCode::SceneNotFound);
        return;
    }
    const auto submitted = requests->SubmitReleaseSlot(record->m_instance.m_slot, GetEntityId());
    if (!submitted.IsAccepted()) {
        FinishRelease(requestId, submitted.m_code);
        return;
    }
    record->m_state = RuntimeState::InstanceState::Releasing;
    record->m_pendingCommand = submitted.m_command;
    record->m_pendingType = SceneCommandType::ReleaseSlot;
}

void ScenePolytreeSpawnerComponent::OnScenePolytreeCommandCompleted(
    SceneCommandId command, SceneCommandType type, ScenePolytreeResultCode result) {
    auto *record = m_runtime->Find(command);
    if (record == nullptr || record->m_pendingType != type) {
        return;
    }
    const SpawnRequestId requestId = record->m_spawnRequest;
    record->m_pendingCommand = {};
    switch (record->m_state) {
    case RuntimeState::InstanceState::Resetting:
        if (result != ScenePolytreeResultCode::Success) {
            BeginSpawnFailure(requestId, SceneFailure(result));
        } else if (static_cast<SpawnBoundary>(record->m_callbackState->m_boundary.load()) ==
                   SpawnBoundary::Cancelled) {
            BeginSpawnFailure(requestId, {SpawnError::Cancelled});
        } else {
            SubmitPlace(requestId);
        }
        break;
    case RuntimeState::InstanceState::Placing:
        if (result != ScenePolytreeResultCode::Success) {
            BeginSpawnFailure(requestId, SceneFailure(result));
        } else if (static_cast<SpawnBoundary>(record->m_callbackState->m_boundary.load()) ==
                   SpawnBoundary::Cancelled) {
            BeginSpawnFailure(requestId, {SpawnError::Cancelled});
        } else {
            BeginEntitySpawn(requestId);
        }
        break;
    case RuntimeState::InstanceState::Binding:
        if (result != ScenePolytreeResultCode::Success) {
            BeginSpawnFailure(requestId, SceneFailure(result));
        } else if (!record->m_declarations.empty()) {
            SubmitAttachControllers(requestId, AZStd::move(record->m_declarations));
        } else {
            record->m_state = RuntimeState::InstanceState::Active;
            ScenePolytreeSpawnerNotificationBus::Event(
                GetEntityId(), &ScenePolytreeSpawnerNotifications::OnSpawnSucceeded,
                record->m_spawnRequest, record->m_instance, record->m_request.m_context);
        }
        break;
    case RuntimeState::InstanceState::AttachingControllers:
        if (result != ScenePolytreeResultCode::Success) {
            BeginSpawnFailure(requestId, ControllerFailure(result));
        } else {
            record->m_controllersAttached = true;
            record->m_state = RuntimeState::InstanceState::Active;
            ScenePolytreeSpawnerNotificationBus::Event(
                GetEntityId(), &ScenePolytreeSpawnerNotifications::OnSpawnSucceeded,
                record->m_spawnRequest, record->m_instance, record->m_request.m_context);
        }
        break;
    case RuntimeState::InstanceState::DetachingControllers:
        record->m_controllersAttached = false;
        if (result != ScenePolytreeResultCode::Success) {
            record->m_despawnFailure = {DespawnError::SceneCommandFailed, result, result};
        }
        SubmitUnbind(requestId);
        break;
    case RuntimeState::InstanceState::Unbinding:
        if (result != ScenePolytreeResultCode::Success) {
            if (record->m_terminalSpawnFailure) {
                record->m_spawnFailure.m_cleanupResult = result;
            } else {
                record->m_despawnFailure = {DespawnError::SceneCommandFailed, result, result};
            }
        }
        BeginEntityDespawn(requestId);
        break;
    case RuntimeState::InstanceState::CleanupResetting:
        if (result != ScenePolytreeResultCode::Success) {
            if (record->m_terminalSpawnFailure) {
                record->m_spawnFailure.m_cleanupResult = result;
            } else {
                record->m_despawnFailure.m_cleanupResult = result;
            }
        }
        SubmitRelease(requestId);
        break;
    case RuntimeState::InstanceState::Releasing:
        FinishRelease(requestId, result);
        break;
    default:
        break;
    }
}

void ScenePolytreeSpawnerComponent::FinishRelease(SpawnRequestId requestId,
                                                  ScenePolytreeResultCode result) {
    auto *record = m_runtime->Find(requestId);
    if (record == nullptr) {
        return;
    }
    const auto requestContext = record->m_request.m_context;
    const auto instance = record->m_instance;
    const auto spawnFailure = record->m_spawnFailure;
    const auto despawnRequest = record->m_despawnRequest;
    auto despawnFailure = record->m_despawnFailure;
    const bool spawnTerminal = record->m_terminalSpawnFailure;
    ScenePolytreeSpawnFailure finalSpawnFailure = spawnFailure;
    if (result != ScenePolytreeResultCode::Success) {
        if (spawnTerminal) {
            finalSpawnFailure.m_cleanupResult = result;
        } else {
            despawnFailure.m_cleanupResult = result;
        }
    }
    m_runtime->m_instances.erase(requestId.m_value);

    if (spawnTerminal) {
        QueueSpawnFailure(requestId, finalSpawnFailure, requestContext);
    } else if (despawnFailure.m_error != DespawnError::None ||
               despawnFailure.m_cleanupResult != ScenePolytreeResultCode::Success) {
        if (despawnFailure.m_error == DespawnError::None) {
            despawnFailure.m_error = DespawnError::CleanupFailed;
        }
        QueueDespawnFailure(despawnRequest, instance, despawnFailure);
    } else {
        ScenePolytreeSpawnerNotificationBus::Event(
            GetEntityId(), &ScenePolytreeSpawnerNotifications::OnDespawnSucceeded, despawnRequest,
            instance);
    }
}

void ScenePolytreeSpawnerComponent::QueueSpawnFailure(
    SpawnRequestId requestId, const ScenePolytreeSpawnFailure &failure,
    const ScenePolytreeRequestContext &context) const {
    const AZ::EntityId entityId = GetEntityId();
    AZ::TickBus::QueueFunction([entityId, requestId, failure, context]() {
        ScenePolytreeSpawnerNotificationBus::Event(
            entityId, &ScenePolytreeSpawnerNotifications::OnSpawnFailed, requestId, failure,
            context);
    });
}

void ScenePolytreeSpawnerComponent::QueueDespawnFailure(
    DespawnRequestId requestId, InstanceHandle instance,
    const ScenePolytreeDespawnFailure &failure) const {
    const AZ::EntityId entityId = GetEntityId();
    AZ::TickBus::QueueFunction([entityId, requestId, instance, failure]() {
        ScenePolytreeSpawnerNotificationBus::Event(
            entityId, &ScenePolytreeSpawnerNotifications::OnDespawnFailed, requestId, instance,
            failure);
    });
}
} // namespace ScenePolytree
