#include "ScenePolytreeSystemComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/SerializeContext.h>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <utility>

namespace ScenePolytree {
void ScenePolytreeSystemComponent::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<ScenePolytreeSystemComponent, AZ::Component>()->Version(1);
    }
}

void ScenePolytreeSystemComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeService"));
}

void ScenePolytreeSystemComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeService"));
}

void ScenePolytreeSystemComponent::Activate() {
    AZ::Interface<ScenePolytreeRequests>::Register(this);
    AZ::Interface<ScenePolytreeRegistrationRequests>::Register(this);
    ScenePolytreeRequestBus::Handler::BusConnect();
    AzFramework::GameEntityContextEventBus::Handler::BusConnect();
    AzFramework::RootSpawnableNotificationBus::Handler::BusConnect();
}

void ScenePolytreeSystemComponent::Deactivate() {
    AZStd::scoped_lock lock(m_mutex);
    AZ::TickBus::Handler::BusDisconnect();
    AzFramework::RootSpawnableNotificationBus::Handler::BusDisconnect();
    AzFramework::GameEntityContextEventBus::Handler::BusDisconnect();
    ScenePolytreeRequestBus::Handler::BusDisconnect();
    AZ::Interface<ScenePolytreeRegistrationRequests>::Unregister(this);
    AZ::Interface<ScenePolytreeRequests>::Unregister(this);
    m_commands.clear();
    m_scenes.clear();
    m_registeredSceneEntities.clear();
    m_pendingPrefabRegistrations.clear();
    m_collectionClosed = false;
}

SceneHandle
ScenePolytreeSystemComponent::CreateScene(const ScenePolytreeSceneDescriptor &descriptor) {
    AZStd::scoped_lock lock(m_mutex);
    if (descriptor.m_fixedStepNanoseconds <= 0 || descriptor.m_maxCatchUpSteps == 0 ||
        (descriptor.m_permanentNodes.empty() && descriptor.m_partitions.empty())) {
        return {};
    }
    const SceneHandle handle{m_nextSceneId++};
    m_scenes.emplace(handle.m_value, SceneEntry{});
    Enqueue(CreateCommand{handle, descriptor});
    return handle;
}

void ScenePolytreeSystemComponent::DestroyScene(SceneHandle scene) {
    AZStd::scoped_lock lock(m_mutex);
    const auto found = m_scenes.find(scene.m_value);
    if (found != m_scenes.end() && found->second.m_life != SceneLife::Destroying) {
        found->second.m_life = SceneLife::Destroying;
        Enqueue(DestroyCommand{scene});
    }
}

SlotResult ScenePolytreeSystemComponent::ReserveSlot(SpawnerHandle spawner) {
    AZStd::scoped_lock lock(m_mutex);
    Internal::SceneInstance *scene = FindScene(spawner.m_scene);
    return scene != nullptr ? scene->ReserveSlot(spawner)
                            : SlotResult{AcceptsCommands(spawner.m_scene)
                                             ? ScenePolytreeResultCode::SceneNotReady
                                             : ScenePolytreeResultCode::SceneNotFound,
                                         {}};
}

ScenePolytreeResultCode ScenePolytreeSystemComponent::PlaceSlot(SlotHandle slot,
                                                                const AZ::Transform &rootWorld) {
    return SubmitPlaceSlot(slot, rootWorld, AZ::EntityId{}).m_code;
}

SceneCommandSubmission
ScenePolytreeSystemComponent::SubmitPlaceSlot(SlotHandle slot, const AZ::Transform &rootWorld,
                                              AZ::EntityId completionEntity) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(slot.m_spawner.m_scene)) {
        return {ScenePolytreeResultCode::SceneNotFound, {}};
    }
    if (!rootWorld.IsFinite()) {
        return {ScenePolytreeResultCode::InvalidTransform, {}};
    }
    const SceneCommandId command{m_nextCommandId++};
    Enqueue(PlaceSlotCommand{slot, rootWorld, command, completionEntity});
    return {ScenePolytreeResultCode::Success, command};
}

ScenePolytreeResultCode
ScenePolytreeSystemComponent::BindSlot(SlotHandle slot,
                                       const AZStd::vector<ScenePolytreeEntityBinding> &bindings) {
    return SubmitBindSlot(slot, bindings, AZ::EntityId{}).m_code;
}

SceneCommandSubmission ScenePolytreeSystemComponent::SubmitBindSlot(
    SlotHandle slot, const AZStd::vector<ScenePolytreeEntityBinding> &bindings,
    AZ::EntityId completionEntity) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(slot.m_spawner.m_scene)) {
        return {ScenePolytreeResultCode::SceneNotFound, {}};
    }
    if (bindings.empty()) {
        return {ScenePolytreeResultCode::InvalidBinding, {}};
    }
    const SceneCommandId command{m_nextCommandId++};
    Enqueue(BindSlotCommand{slot, bindings, command, completionEntity});
    return {ScenePolytreeResultCode::Success, command};
}

ScenePolytreeResultCode ScenePolytreeSystemComponent::UnbindSlot(SlotHandle slot) {
    return SubmitUnbindSlot(slot, AZ::EntityId{}).m_code;
}

SceneCommandSubmission
ScenePolytreeSystemComponent::SubmitUnbindSlot(SlotHandle slot, AZ::EntityId completionEntity) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(slot.m_spawner.m_scene)) {
        return {ScenePolytreeResultCode::SceneNotFound, {}};
    }
    const SceneCommandId command{m_nextCommandId++};
    Enqueue(UnbindSlotCommand{slot, command, completionEntity});
    return {ScenePolytreeResultCode::Success, command};
}

ScenePolytreeResultCode ScenePolytreeSystemComponent::ResetSlot(SlotHandle slot) {
    return SubmitResetSlot(slot, AZ::EntityId{}).m_code;
}

SceneCommandSubmission
ScenePolytreeSystemComponent::SubmitResetSlot(SlotHandle slot, AZ::EntityId completionEntity) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(slot.m_spawner.m_scene)) {
        return {ScenePolytreeResultCode::SceneNotFound, {}};
    }
    const SceneCommandId command{m_nextCommandId++};
    Enqueue(ResetSlotCommand{slot, command, completionEntity});
    return {ScenePolytreeResultCode::Success, command};
}

ScenePolytreeResultCode ScenePolytreeSystemComponent::ReleaseSlot(SlotHandle slot) {
    return SubmitReleaseSlot(slot, AZ::EntityId{}).m_code;
}

SceneCommandSubmission
ScenePolytreeSystemComponent::SubmitReleaseSlot(SlotHandle slot, AZ::EntityId completionEntity) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(slot.m_spawner.m_scene)) {
        return {ScenePolytreeResultCode::SceneNotFound, {}};
    }
    const SceneCommandId command{m_nextCommandId++};
    Enqueue(ReleaseSlotCommand{slot, command, completionEntity});
    return {ScenePolytreeResultCode::Success, command};
}

NodeResult ScenePolytreeSystemComponent::ResolveNode(SlotHandle slot,
                                                     const AZ::Name &bindingId) const {
    AZStd::scoped_lock lock(m_mutex);
    const Internal::SceneInstance *scene = FindScene(slot.m_spawner.m_scene);
    return scene != nullptr ? scene->ResolveNode(slot, bindingId)
                            : NodeResult{ScenePolytreeResultCode::SceneNotReady, {}};
}

bool ScenePolytreeSystemComponent::SetSceneActive(SceneHandle handle, bool active) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(handle)) {
        return false;
    }
    Enqueue(ActiveCommand{handle, active});
    return true;
}

bool ScenePolytreeSystemComponent::RequestCorrection(const SceneCorrection &correction) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(correction.m_node.m_slot.m_spawner.m_scene) ||
        !correction.m_node.IsValid() || !correction.m_transform.IsFinite()) {
        return false;
    }
    Enqueue(CorrectionCommand{correction});
    return true;
}

SceneStatistics ScenePolytreeSystemComponent::GetSceneStatistics(SceneHandle handle) const {
    AZStd::scoped_lock lock(m_mutex);
    const Internal::SceneInstance *scene = FindScene(handle);
    return scene != nullptr ? scene->GetStatistics() : SceneStatistics{};
}

bool ScenePolytreeSystemComponent::IsSceneAlive(SceneHandle scene) const {
    AZStd::scoped_lock lock(m_mutex);
    return AcceptsCommands(scene);
}

bool ScenePolytreeSystemComponent::IsSceneReady(SceneHandle scene) const {
    AZStd::scoped_lock lock(m_mutex);
    return FindScene(scene) != nullptr;
}

ScenePolytreeResultCode ScenePolytreeSystemComponent::RegisterSceneEntity(AZ::EntityId sceneEntity,
                                                                          bool isDefault) {
    AZStd::scoped_lock lock(m_mutex);
    if (m_collectionClosed) {
        return ScenePolytreeResultCode::RegistrationClosed;
    }
    const auto found =
        std::ranges::find(m_registeredSceneEntities, sceneEntity, &RegisteredSceneEntity::m_entity);
    if (!sceneEntity.IsValid() || found != m_registeredSceneEntities.end()) {
        return ScenePolytreeResultCode::InvalidBinding;
    }
    m_registeredSceneEntities.push_back({sceneEntity, isDefault});
    return ScenePolytreeResultCode::Success;
}

void ScenePolytreeSystemComponent::UnregisterSceneEntity(AZ::EntityId sceneEntity) {
    AZStd::scoped_lock lock(m_mutex);
    AZStd::erase_if(m_registeredSceneEntities,
                    [&](const auto &entry) { return entry.m_entity == sceneEntity; });
}

RegistrationResult ScenePolytreeSystemComponent::RegisterPrefab(
    AZ::EntityId ownerEntity, AZ::EntityId targetScene,
    const ScenePolytreePrefabRegistrationDescriptor &descriptor) {
    AZStd::scoped_lock lock(m_mutex);
    if (m_collectionClosed) {
        return {ScenePolytreeResultCode::RegistrationClosed, {}};
    }
    if (!ownerEntity.IsValid() || !descriptor.m_prefab.GetId().IsValid()) {
        return {ScenePolytreeResultCode::InvalidPrefab, {}};
    }
    if (descriptor.m_capacity == 0) {
        return {ScenePolytreeResultCode::ZeroCapacity, {}};
    }
    if (descriptor.m_registrationKey.IsEmpty() ||
        std::ranges::any_of(m_pendingPrefabRegistrations, [&](const auto &entry) {
            return entry.m_ownerEntity == ownerEntity &&
                   entry.m_descriptor.m_registrationKey == descriptor.m_registrationKey;
        })) {
        return {ScenePolytreeResultCode::DuplicateBindingId, {}};
    }
    const RegistrationToken token{m_nextRegistrationId++};
    m_pendingPrefabRegistrations.push_back({token, ownerEntity, targetScene, descriptor});
    return {ScenePolytreeResultCode::Success, token};
}

void ScenePolytreeSystemComponent::UnregisterPrefab(RegistrationToken token) {
    AZStd::scoped_lock lock(m_mutex);
    AZStd::erase_if(m_pendingPrefabRegistrations,
                    [&](const auto &entry) { return entry.m_token == token; });
}

void ScenePolytreeSystemComponent::OnGameEntitiesStarted() {
    // O3DE emits this before its asynchronous root Spawnable has necessarily activated entities.
    // Collection closes in OnRootSpawnableReady instead.
}

void ScenePolytreeSystemComponent::OnRootSpawnableReady(
    [[maybe_unused]] AZ::Data::Asset<AzFramework::Spawnable> rootSpawnable,
    [[maybe_unused]] AZ::u32 generation) {
    AZStd::scoped_lock lock(m_mutex);
    if (m_collectionClosed) {
        return;
    }
    m_collectionClosed = true;

    AZStd::vector<AZ::EntityId> defaults;
    std::ranges::transform(m_registeredSceneEntities | std::views::filter([](const auto &entry) {
                               return entry.m_default;
                           }),
                           std::back_inserter(defaults), &RegisteredSceneEntity::m_entity);

    auto sorted = m_pendingPrefabRegistrations;
    std::ranges::sort(sorted, [](const auto &left, const auto &right) {
        const auto leftOwner = static_cast<AZ::u64>(left.m_ownerEntity);
        const auto rightOwner = static_cast<AZ::u64>(right.m_ownerEntity);
        return leftOwner != rightOwner ? leftOwner < rightOwner
                                       : left.m_descriptor.m_registrationKey.GetStringView() <
                                             right.m_descriptor.m_registrationKey.GetStringView();
    });

    std::ranges::for_each(m_registeredSceneEntities, [&](const auto &sceneEntry) {
        AZStd::vector<ResolvedScenePolytreeRegistration> resolved;
        AZ::u64 nextPartition = 1;
        std::ranges::for_each(sorted, [&](const auto &registration) {
            const bool usesDefault = !registration.m_targetScene.IsValid();
            const bool targetsScene =
                usesDefault ? defaults.size() == 1 && defaults.front() == sceneEntry.m_entity
                            : registration.m_targetScene == sceneEntry.m_entity;
            if (targetsScene) {
                resolved.push_back({registration.m_token, registration.m_ownerEntity,
                                    nextPartition++, registration.m_descriptor});
            }
        });
        ScenePolytreeComponentRequestBus::Event(
            sceneEntry.m_entity, &ScenePolytreeComponentRequests::BeginBuild, resolved);
    });

    std::ranges::for_each(sorted, [&](const auto &registration) {
        const bool usesDefault = !registration.m_targetScene.IsValid();
        const bool explicitTargetExists =
            !usesDefault && std::ranges::any_of(m_registeredSceneEntities, [&](const auto &entry) {
                return entry.m_entity == registration.m_targetScene;
            });
        const auto failureCode =
            usesDefault && defaults.empty()         ? ScenePolytreeResultCode::MissingDefaultScene
            : usesDefault && defaults.size() != 1   ? ScenePolytreeResultCode::DuplicateDefaultScene
            : !usesDefault && !explicitTargetExists ? ScenePolytreeResultCode::SceneNotFound
                                                    : ScenePolytreeResultCode::Success;
        if (failureCode != ScenePolytreeResultCode::Success) {
            ScenePolytreeRegistrationNotificationBus::Event(
                registration.m_ownerEntity,
                &ScenePolytreeRegistrationNotifications::OnScenePolytreeRegistrationFailed,
                registration.m_token,
                ScenePolytreeFailure{failureCode,
                                     registration.m_descriptor.m_registrationKey,
                                     registration.m_descriptor.m_prefab.GetId(),
                                     {}});
        }
    });
}

void ScenePolytreeSystemComponent::OnGameEntitiesReset() {
    AZStd::scoped_lock lock(m_mutex);
    m_collectionClosed = false;
    m_registeredSceneEntities.clear();
    m_pendingPrefabRegistrations.clear();
}

void ScenePolytreeSystemComponent::OnTick(float deltaTime,
                                          [[maybe_unused]] AZ::ScriptTimePoint time) {
    AZStd::scoped_lock lock(m_mutex);
    DrainCommands();
    const auto frameDelta = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<float>(std::max(deltaTime, 0.0f)));
    std::ranges::for_each(m_scenes, [&](auto &entry) {
        if (entry.second.m_life == SceneLife::Alive) {
            entry.second.m_instance->Advance(
                frameDelta, [](AZ::EntityId entityId, const AZ::Transform &world) {
                    AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetWorldTM, world);
                });
        }
    });
    RefreshTickConnection();
}

int ScenePolytreeSystemComponent::GetTickOrder() { return AZ::TICK_GAME + 1; }

Internal::SceneInstance *ScenePolytreeSystemComponent::FindScene(SceneHandle scene) {
    const auto found = m_scenes.find(scene.m_value);
    return found != m_scenes.end() && found->second.m_life == SceneLife::Alive
               ? found->second.m_instance.get()
               : nullptr;
}

const Internal::SceneInstance *ScenePolytreeSystemComponent::FindScene(SceneHandle scene) const {
    const auto found = m_scenes.find(scene.m_value);
    return found != m_scenes.end() && found->second.m_life == SceneLife::Alive
               ? found->second.m_instance.get()
               : nullptr;
}

bool ScenePolytreeSystemComponent::AcceptsCommands(SceneHandle scene) const {
    const auto found = m_scenes.find(scene.m_value);
    return scene.IsValid() && found != m_scenes.end() &&
           found->second.m_life != SceneLife::Destroying;
}

void ScenePolytreeSystemComponent::Enqueue(Command command) {
    m_commands.push_back(std::move(command));
    RefreshTickConnection();
}

void ScenePolytreeSystemComponent::DrainCommands() {
    std::vector<Command> commands;
    commands.swap(m_commands);
    std::ranges::for_each(commands, [&](const Command &command) {
        std::visit([&](const auto &typed) { Process(typed); }, command);
    });
}

void ScenePolytreeSystemComponent::Process(const CreateCommand &command) {
    const auto found = m_scenes.find(command.m_scene.m_value);
    if (found == m_scenes.end() || found->second.m_life != SceneLife::Pending) {
        return;
    }
    found->second.m_instance = Internal::SceneInstance::Create(command.m_descriptor);
    if (found->second.m_instance) {
        found->second.m_life = SceneLife::Alive;
        ScenePolytreeSystemNotificationBus::Event(command.m_scene.m_value,
                                                  &ScenePolytreeSystemNotifications::OnSceneReady,
                                                  command.m_scene);
    } else {
        const ScenePolytreeFailure failure{ScenePolytreeResultCode::ConstructionFailed};
        ScenePolytreeSystemNotificationBus::Event(command.m_scene.m_value,
                                                  &ScenePolytreeSystemNotifications::OnSceneFailed,
                                                  command.m_scene, failure);
        AZ_Error("ScenePolytree", false, "Failed to create a queued shared scene.");
        m_scenes.erase(found);
    }
}

void ScenePolytreeSystemComponent::Process(const DestroyCommand &command) {
    m_scenes.erase(command.m_scene.m_value);
}

void ScenePolytreeSystemComponent::Process(const PlaceSlotCommand &command) {
    Internal::SceneInstance *scene = FindScene(command.m_slot.m_spawner.m_scene);
    const auto result = scene != nullptr ? scene->PlaceSlot(command.m_slot, command.m_rootWorld)
                                         : ScenePolytreeResultCode::SceneNotFound;
    Complete(command.m_command, command.m_completionEntity, SceneCommandType::PlaceSlot, result);
}

void ScenePolytreeSystemComponent::Process(const BindSlotCommand &command) {
    Internal::SceneInstance *scene = FindScene(command.m_slot.m_spawner.m_scene);
    const auto result = scene != nullptr ? scene->BindSlot(command.m_slot, command.m_bindings)
                                         : ScenePolytreeResultCode::SceneNotFound;
    Complete(command.m_command, command.m_completionEntity, SceneCommandType::BindSlot, result);
}

void ScenePolytreeSystemComponent::Process(const UnbindSlotCommand &command) {
    Internal::SceneInstance *scene = FindScene(command.m_slot.m_spawner.m_scene);
    const auto result = scene != nullptr ? scene->UnbindSlot(command.m_slot)
                                         : ScenePolytreeResultCode::SceneNotFound;
    Complete(command.m_command, command.m_completionEntity, SceneCommandType::UnbindSlot, result);
}

void ScenePolytreeSystemComponent::Process(const ResetSlotCommand &command) {
    Internal::SceneInstance *scene = FindScene(command.m_slot.m_spawner.m_scene);
    const auto result = scene != nullptr ? scene->ResetSlot(command.m_slot)
                                         : ScenePolytreeResultCode::SceneNotFound;
    Complete(command.m_command, command.m_completionEntity, SceneCommandType::ResetSlot, result);
}

void ScenePolytreeSystemComponent::Process(const ReleaseSlotCommand &command) {
    Internal::SceneInstance *scene = FindScene(command.m_slot.m_spawner.m_scene);
    const auto result = scene != nullptr ? scene->ReleaseSlot(command.m_slot)
                                         : ScenePolytreeResultCode::SceneNotFound;
    Complete(command.m_command, command.m_completionEntity, SceneCommandType::ReleaseSlot, result);
}

void ScenePolytreeSystemComponent::Process(const ActiveCommand &command) {
    if (Internal::SceneInstance *scene = FindScene(command.m_scene)) {
        (void)scene->SetActive(command.m_active);
    }
}

void ScenePolytreeSystemComponent::Process(const CorrectionCommand &command) {
    if (Internal::SceneInstance *scene =
            FindScene(command.m_correction.m_node.m_slot.m_spawner.m_scene)) {
        const bool accepted =
            command.m_correction.m_space == SceneCorrectionSpace::Local
                ? scene->CorrectLocal(command.m_correction.m_node, command.m_correction.m_transform)
                : scene->CorrectWorld(command.m_correction.m_node,
                                      command.m_correction.m_transform);
        AZ_Error("ScenePolytree", accepted, "Rejected an invalid scene correction command.");
    }
}

void ScenePolytreeSystemComponent::Complete(SceneCommandId command, AZ::EntityId completionEntity,
                                            SceneCommandType type, ScenePolytreeResultCode result) {
    if (command.IsValid() && completionEntity.IsValid()) {
        ScenePolytreeCommandNotificationBus::Event(
            completionEntity, &ScenePolytreeCommandNotifications::OnScenePolytreeCommandCompleted,
            command, type, result);
    }
}

void ScenePolytreeSystemComponent::RefreshTickConnection() {
    const bool needsTick =
        !m_commands.empty() || std::ranges::any_of(m_scenes, [](const auto &entry) {
            return entry.second.m_life == SceneLife::Alive && entry.second.m_instance->NeedsTick();
        });
    if (needsTick && !AZ::TickBus::Handler::BusIsConnected()) {
        AZ::TickBus::Handler::BusConnect();
    } else if (!needsTick && AZ::TickBus::Handler::BusIsConnected()) {
        AZ::TickBus::Handler::BusDisconnect();
    }
}
} // namespace ScenePolytree
