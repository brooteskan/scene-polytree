#include <ScenePolytree/ScenePolytreeComponent.h>

#include "PrefabTopology.h"

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

#include <algorithm>
#include <ranges>

namespace ScenePolytree {
void ScenePolytreeComponentConfig::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<ScenePolytreeNodeDescriptor>()
            ->Version(2)
            ->Field("BindingId", &ScenePolytreeNodeDescriptor::m_bindingId)
            ->Field("ParentBindingId", &ScenePolytreeNodeDescriptor::m_parentBindingId)
            ->Field("InitialLocal", &ScenePolytreeNodeDescriptor::m_initialLocal);
        serializeContext->Class<ScenePolytreeComponentConfig, AZ::ComponentConfig>()
            ->Version(1)
            ->Field("IsDefault", &ScenePolytreeComponentConfig::m_isDefault)
            ->Field("FixedStepNanoseconds", &ScenePolytreeComponentConfig::m_fixedStepNanoseconds)
            ->Field("MaxCatchUpSteps", &ScenePolytreeComponentConfig::m_maxCatchUpSteps)
            ->Field("PermanentNodes", &ScenePolytreeComponentConfig::m_permanentNodes);

        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<ScenePolytreeComponentConfig>("Scene Polytree Configuration",
                                                      "Level-owned forest settings.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Visibility,
                            AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeComponentConfig::m_isDefault, "Is Default",
                              "Use this forest for spawners without an explicit target.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeComponentConfig::m_fixedStepNanoseconds,
                              "Fixed Step (ns)", "Positive fixed simulation step.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeComponentConfig::m_maxCatchUpSteps,
                              "Maximum Catch-up Steps", "Positive per-frame catch-up bound.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeComponentConfig::m_permanentNodes, "Permanent Nodes",
                              "Programmatic level nodes frozen with Prefab partitions.");
        }
    }
}

void ScenePolytreeComponent::Reflect(AZ::ReflectContext *context) {
    ScenePolytreeComponentConfig::Reflect(context);
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<ScenePolytreeComponent, AZ::Component>()->Version(1)->Field(
            "Configuration", &ScenePolytreeComponent::m_configuration);
        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<ScenePolytreeComponent>(
                    "Scene Polytree",
                    "Owns one level-authored frozen scene-polytree forest lifetime.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeComponent::m_configuration, "Configuration",
                              "Forest construction settings.");
        }
    }
}

void ScenePolytreeComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeLevelSceneService"));
}

void ScenePolytreeComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeLevelSceneService"));
}

ScenePolytreeComponent::ScenePolytreeComponent(const ScenePolytreeComponentConfig &configuration)
    : m_configuration(configuration) {}

void ScenePolytreeComponent::Activate() {
    m_lifecycle = ScenePolytreeLifecycle::Collecting;
    m_failure = {};
    ScenePolytreeComponentRequestBus::Handler::BusConnect(GetEntityId());
    auto *registry = AZ::Interface<ScenePolytreeRegistrationRequests>::Get();
    const auto result =
        registry != nullptr
            ? registry->RegisterSceneEntity(GetEntityId(), m_configuration.m_isDefault)
            : ScenePolytreeResultCode::SceneNotFound;
    if (result != ScenePolytreeResultCode::Success) {
        FailBuild({result});
    }
}

void ScenePolytreeComponent::Deactivate() {
    m_lifecycle = ScenePolytreeLifecycle::Destroying;
    AZ::Data::AssetBus::MultiHandler::BusDisconnect();
    ScenePolytreeSystemNotificationBus::Handler::BusDisconnect();
    if (auto *registry = AZ::Interface<ScenePolytreeRegistrationRequests>::Get()) {
        registry->UnregisterSceneEntity(GetEntityId());
    }
    if (auto *requests = AZ::Interface<ScenePolytreeRequests>::Get()) {
        requests->DestroyScene(m_scene);
    }
    m_scene = {};
    m_registrations.clear();
    ScenePolytreeComponentRequestBus::Handler::BusDisconnect();
}

bool ScenePolytreeComponent::ReadInConfig(const AZ::ComponentConfig *baseConfig) {
    if (const auto *configuration = azrtti_cast<const ScenePolytreeComponentConfig *>(baseConfig)) {
        m_configuration = *configuration;
        return true;
    }
    return false;
}

bool ScenePolytreeComponent::WriteOutConfig(AZ::ComponentConfig *outBaseConfig) const {
    if (auto *configuration = azrtti_cast<ScenePolytreeComponentConfig *>(outBaseConfig)) {
        *configuration = m_configuration;
        return true;
    }
    return false;
}

void ScenePolytreeComponent::BeginBuild(
    const AZStd::vector<ResolvedScenePolytreeRegistration> &registrations) {
    if (m_lifecycle != ScenePolytreeLifecycle::Collecting) {
        FailBuild({ScenePolytreeResultCode::RegistrationClosed});
        return;
    }
    m_lifecycle = ScenePolytreeLifecycle::Building;
    m_registrations = registrations;
    if (m_registrations.empty() && m_configuration.m_permanentNodes.empty()) {
        FailBuild({ScenePolytreeResultCode::EmptyScene});
        return;
    }

    std::ranges::for_each(m_registrations, [&](auto &registration) {
        if (m_lifecycle != ScenePolytreeLifecycle::Building || m_scene.IsValid()) {
            return;
        }
        const AZ::Data::AssetId assetId = registration.m_descriptor.m_prefab.GetId();
        if (!AZ::Data::AssetBus::MultiHandler::BusIsConnectedId(assetId)) {
            AZ::Data::AssetBus::MultiHandler::BusConnect(assetId);
        }
        if (m_lifecycle == ScenePolytreeLifecycle::Building && !m_scene.IsValid()) {
            registration.m_descriptor.m_prefab.QueueLoad();
        }
    });
    TryBuild();
}

void ScenePolytreeComponent::FailBuild(const ScenePolytreeFailure &failure) {
    if (m_lifecycle == ScenePolytreeLifecycle::Destroying) {
        return;
    }
    m_failure = failure;
    m_lifecycle = ScenePolytreeLifecycle::Failed;
    AZ::Data::AssetBus::MultiHandler::BusDisconnect();
    ScenePolytreeSystemNotificationBus::Handler::BusDisconnect();
    NotifyFailure();
}

void ScenePolytreeComponent::OnAssetReady(
    [[maybe_unused]] AZ::Data::Asset<AZ::Data::AssetData> asset) {
    TryBuild();
}

void ScenePolytreeComponent::OnAssetError(AZ::Data::Asset<AZ::Data::AssetData> asset) {
    const auto registration = std::ranges::find_if(m_registrations, [&](const auto &entry) {
        return entry.m_descriptor.m_prefab.GetId() == asset.GetId();
    });
    FailBuild({ScenePolytreeResultCode::AssetLoadFailed,
               registration != m_registrations.end() ? registration->m_descriptor.m_registrationKey
                                                     : AZ::Name(),
               asset.GetId(),
               {}});
}

void ScenePolytreeComponent::OnAssetCanceled(AZ::Data::AssetId assetId) {
    const auto registration = std::ranges::find_if(m_registrations, [&](const auto &entry) {
        return entry.m_descriptor.m_prefab.GetId() == assetId;
    });
    FailBuild({ScenePolytreeResultCode::AssetLoadFailed,
               registration != m_registrations.end() ? registration->m_descriptor.m_registrationKey
                                                     : AZ::Name(),
               assetId,
               {}});
}

void ScenePolytreeComponent::OnSceneReady(SceneHandle scene) {
    if (m_lifecycle != ScenePolytreeLifecycle::Building || scene != m_scene) {
        return;
    }
    m_lifecycle = ScenePolytreeLifecycle::Ready;
    ScenePolytreeSystemNotificationBus::Handler::BusDisconnect();
    std::ranges::for_each(m_registrations, [&](const auto &registration) {
        ScenePolytreeRegistrationNotificationBus::Event(
            registration.m_ownerEntity,
            &ScenePolytreeRegistrationNotifications::OnScenePolytreeRegistrationReady,
            registration.m_token, SpawnerHandle{m_scene, registration.m_partition, 1});
    });
}

void ScenePolytreeComponent::OnSceneFailed(SceneHandle scene, const ScenePolytreeFailure &failure) {
    if (scene == m_scene) {
        FailBuild(failure);
    }
}

void ScenePolytreeComponent::TryBuild() {
    if (m_lifecycle != ScenePolytreeLifecycle::Building || m_scene.IsValid() ||
        std::ranges::any_of(m_registrations, [](const auto &registration) {
            return !registration.m_descriptor.m_prefab.IsReady();
        })) {
        return;
    }

    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_fixedStepNanoseconds = m_configuration.m_fixedStepNanoseconds;
    descriptor.m_maxCatchUpSteps = m_configuration.m_maxCatchUpSteps;
    if (!m_configuration.m_permanentNodes.empty()) {
        auto permanent = Internal::ValidateAndNormalizeTopology(m_configuration.m_permanentNodes);
        if (!permanent.IsSuccess()) {
            FailBuild(permanent.m_failure);
            return;
        }
        descriptor.m_permanentNodes = AZStd::move(permanent.m_nodes);
    }

    bool valid = true;
    std::ranges::for_each(m_registrations, [&](const auto &registration) {
        if (!valid) {
            return;
        }
        auto topology = Internal::ExtractPrefabTopology(*registration.m_descriptor.m_prefab);
        if (!topology.IsSuccess()) {
            topology.m_failure.m_registrationKey = registration.m_descriptor.m_registrationKey;
            topology.m_failure.m_assetId = registration.m_descriptor.m_prefab.GetId();
            valid = false;
            FailBuild(topology.m_failure);
            return;
        }
        descriptor.m_partitions.push_back({registration.m_partition,
                                           registration.m_descriptor.m_capacity,
                                           AZStd::move(topology.m_nodes)});
    });
    if (!valid) {
        return;
    }

    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    m_scene = requests != nullptr ? requests->CreateScene(descriptor) : SceneHandle{};
    if (!m_scene.IsValid()) {
        FailBuild({ScenePolytreeResultCode::ConstructionFailed});
        return;
    }
    AZ::Data::AssetBus::MultiHandler::BusDisconnect();
    ScenePolytreeSystemNotificationBus::Handler::BusConnect(m_scene.m_value);
}

void ScenePolytreeComponent::NotifyFailure() {
    std::ranges::for_each(m_registrations, [&](const auto &registration) {
        ScenePolytreeRegistrationNotificationBus::Event(
            registration.m_ownerEntity,
            &ScenePolytreeRegistrationNotifications::OnScenePolytreeRegistrationFailed,
            registration.m_token, m_failure);
    });
}
} // namespace ScenePolytree
