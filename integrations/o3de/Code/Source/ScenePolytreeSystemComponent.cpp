#include "ScenePolytreeSystemComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/SerializeContext.h>

#include <algorithm>
#include <array>
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
    ScenePolytreeRequestBus::Handler::BusConnect();
}

void ScenePolytreeSystemComponent::Deactivate() {
    AZStd::scoped_lock lock(m_mutex);
    AZ::TickBus::Handler::BusDisconnect();
    ScenePolytreeRequestBus::Handler::BusDisconnect();
    AZ::Interface<ScenePolytreeRequests>::Unregister(this);
    m_commands.clear();
    m_scenes.clear();
}

SceneHandle ScenePolytreeSystemComponent::CreateTankScene(const TankSceneDescriptor &descriptor) {
    AZStd::scoped_lock lock(m_mutex);
    if (descriptor.m_spawnTransforms.empty() || descriptor.m_fixedStepNanoseconds <= 0 ||
        descriptor.m_maxCatchUpSteps == 0) {
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

bool ScenePolytreeSystemComponent::BindTankEntities(TankHandle tank,
                                                    const TankEntityBindings &bindings) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(tank.m_scene) || !bindings.IsComplete()) {
        return false;
    }
    Enqueue(BindCommand{tank, bindings});
    return true;
}

bool ScenePolytreeSystemComponent::RemoveTankEntities(TankHandle tank) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(tank.m_scene)) {
        return false;
    }
    Enqueue(UnbindCommand{tank});
    return true;
}

bool ScenePolytreeSystemComponent::MarkTankReady(TankHandle tank) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(tank.m_scene)) {
        return false;
    }
    Enqueue(ReadyCommand{tank});
    return true;
}

bool ScenePolytreeSystemComponent::SetSceneActive(SceneHandle handle, bool active) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(handle)) {
        return false;
    }
    Enqueue(ActiveCommand{handle, active});
    return true;
}

bool ScenePolytreeSystemComponent::SubmitTankIntent(TankHandle tank, const TankIntent &intent) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(tank.m_scene)) {
        return false;
    }
    Enqueue(IntentCommand{tank, intent});
    return true;
}

bool ScenePolytreeSystemComponent::RequestCorrection(const SceneCorrection &correction) {
    AZStd::scoped_lock lock(m_mutex);
    if (!AcceptsCommands(correction.m_tank.m_scene)) {
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
    } else {
        AZ_Error("ScenePolytree", false, "Failed to create a queued tank scene.");
        m_scenes.erase(found);
    }
}

void ScenePolytreeSystemComponent::Process(const DestroyCommand &command) {
    m_scenes.erase(command.m_scene.m_value);
}

void ScenePolytreeSystemComponent::Process(const BindCommand &command) {
    Internal::SceneInstance *scene = FindScene(command.m_tank.m_scene);
    if (scene == nullptr) {
        return;
    }
    auto *application = AZ::Interface<AZ::ComponentApplicationRequests>::Get();
    const std::array entityIds{
        command.m_bindings.m_hull,
        command.m_bindings.m_turret,
        command.m_bindings.m_gun,
    };
    std::array<AZ::Transform, 3> targetWorldTransforms;
    bool valid = application != nullptr;
    const auto indices = std::views::iota(std::size_t{}, entityIds.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        AZ::Entity *entity = valid ? application->FindEntity(entityIds[index]) : nullptr;
        auto *transform = valid ? AZ::TransformBus::FindFirstHandler(entityIds[index]) : nullptr;
        const bool targetValid = entity != nullptr &&
                                 entity->GetState() == AZ::Entity::State::Active &&
                                 transform != nullptr && !transform->GetParentId().IsValid();
        valid = valid && targetValid;
        if (targetValid) {
            targetWorldTransforms[index] = transform->GetWorldTM();
        }
    });
    const bool bound = valid && scene->BindProjected(command.m_tank.m_index, command.m_bindings,
                                                     targetWorldTransforms);
    AZ_Error("ScenePolytree", bound,
             "Rejected missing, duplicate, inactive, or parented tank projection bindings.");
}

void ScenePolytreeSystemComponent::Process(const UnbindCommand &command) {
    if (Internal::SceneInstance *scene = FindScene(command.m_tank.m_scene)) {
        (void)scene->Unbind(command.m_tank.m_index);
    }
}

void ScenePolytreeSystemComponent::Process(const ReadyCommand &command) {
    if (Internal::SceneInstance *scene = FindScene(command.m_tank.m_scene)) {
        const bool ready = scene->MarkReady(command.m_tank.m_index);
        AZ_Error("ScenePolytree", ready, "Rejected an invalid tank readiness command.");
    }
}

void ScenePolytreeSystemComponent::Process(const ActiveCommand &command) {
    if (Internal::SceneInstance *scene = FindScene(command.m_scene)) {
        (void)scene->SetActive(command.m_active);
    }
}

void ScenePolytreeSystemComponent::Process(const IntentCommand &command) {
    if (Internal::SceneInstance *scene = FindScene(command.m_tank.m_scene)) {
        const bool accepted = scene->SubmitIntent(command.m_tank.m_index, command.m_intent);
        AZ_Error("ScenePolytree", accepted, "Rejected an invalid tank intent command.");
    }
}

void ScenePolytreeSystemComponent::Process(const CorrectionCommand &command) {
    if (Internal::SceneInstance *scene = FindScene(command.m_correction.m_tank.m_scene)) {
        const bool accepted =
            command.m_correction.m_space == SceneCorrectionSpace::Local
                ? scene->CorrectLocal(command.m_correction.m_tank.m_index,
                                      command.m_correction.m_role, command.m_correction.m_transform)
                : scene->CorrectWorld(command.m_correction.m_tank.m_index,
                                      command.m_correction.m_role,
                                      command.m_correction.m_transform);
        AZ_Error("ScenePolytree", accepted, "Rejected an invalid scene correction command.");
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
