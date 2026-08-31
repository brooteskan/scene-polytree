#include <ScenePolytree/Tank/ScenePolytreeTankSpawnerComponent.h>

#include <ScenePolytree/Tank/AiTankIntentAdapterComponent.h>
#include <ScenePolytree/Tank/PlayerTankIntentAdapterComponent.h>
#include <ScenePolytree/Tank/TankArticulationBindingComponent.h>
#include <ScenePolytree/Tank/TankNodeBindingComponent.h>

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>

#include <algorithm>
#include <cmath>
#include <ranges>

namespace ScenePolytree {
namespace {
[[nodiscard]] AZ::Entity *FindEntityById(AzFramework::SpawnableEntityContainerView entities,
                                         AZ::EntityId entityId) {
    const auto found = std::ranges::find_if(
        entities, [entityId](const AZ::Entity *entity) { return entity->GetId() == entityId; });
    return found != entities.end() ? *found : nullptr;
}

[[nodiscard]] const AZ::Entity *
FindEntityById(AzFramework::SpawnableConstEntityContainerView entities, AZ::EntityId entityId) {
    const auto found = std::ranges::find_if(
        entities, [entityId](const AZ::Entity *entity) { return entity->GetId() == entityId; });
    return found != entities.end() ? *found : nullptr;
}

[[nodiscard]] AZ::Entity *FindBoundEntity(AzFramework::SpawnableEntityContainerView entities,
                                          TankNodeRole role) {
    const auto found = std::ranges::find_if(entities, [role](AZ::Entity *entity) {
        const auto *binding = entity->FindComponent<TankNodeBindingComponent>();
        return binding != nullptr && binding->GetRole() == role;
    });
    return found != entities.end() ? *found : nullptr;
}

[[nodiscard]] const AZ::Entity *
FindBoundEntity(AzFramework::SpawnableConstEntityContainerView entities, TankNodeRole role) {
    const auto found = std::ranges::find_if(entities, [role](const AZ::Entity *entity) {
        const auto *binding = entity->FindComponent<TankNodeBindingComponent>();
        return binding != nullptr && binding->GetRole() == role;
    });
    return found != entities.end() ? *found : nullptr;
}

[[nodiscard]] bool HasExactlyOneBinding(AzFramework::SpawnableConstEntityContainerView entities,
                                        TankNodeRole role) {
    return std::ranges::count_if(entities, [role](const AZ::Entity *entity) {
               const auto *binding = entity->FindComponent<TankNodeBindingComponent>();
               return binding != nullptr && binding->GetRole() == role;
           }) == 1;
}

[[nodiscard]] bool
HasExactlyOneArticulationBinding(AzFramework::SpawnableConstEntityContainerView entities) {
    return std::ranges::count_if(entities, [](const AZ::Entity *entity) {
               return entity->FindComponent<TankArticulationBindingComponent>() != nullptr;
           }) == 1;
}

[[nodiscard]] AZ::Entity *
FindArticulationEntity(AzFramework::SpawnableEntityContainerView entities) {
    const auto found = std::ranges::find_if(entities, [](AZ::Entity *entity) {
        return entity->FindComponent<TankArticulationBindingComponent>() != nullptr;
    });
    return found != entities.end() ? *found : nullptr;
}

[[nodiscard]] const AZ::Entity *
FindArticulationEntity(AzFramework::SpawnableConstEntityContainerView entities) {
    const auto found = std::ranges::find_if(entities, [](const AZ::Entity *entity) {
        return entity->FindComponent<TankArticulationBindingComponent>() != nullptr;
    });
    return found != entities.end() ? *found : nullptr;
}

[[nodiscard]] bool IsRigidBasis(const AZ::Transform &basis) {
    return basis.IsFinite() && basis.IsOrthogonal() &&
           std::abs(basis.GetUniformScale() - 1.0f) <= 1.0e-4f;
}

void PrepareMappedEntity(AZ::Entity *entity, const AZ::Transform &spawnTransform,
                         const AZ::Transform &assetToLogicalBasis) {
    if (entity != nullptr) {
        if (auto *transform = entity->FindComponent<AzFramework::TransformComponent>()) {
            const AZ::Transform authoredTransform = transform->GetLocalTM();
            transform->SetParent(AZ::EntityId());
            transform->SetWorldTM(spawnTransform * assetToLogicalBasis * authoredTransform);
        }
    }
}
} // namespace

AZ_COMPONENT_IMPL(ScenePolytreeTankSpawnerComponent, "ScenePolytreeTankSpawnerComponent",
                  ScenePolytreeTankSpawnerComponentTypeId);

void ScenePolytreeTankSpawnerConfig::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<ScenePolytreeTankSpawnerConfig, AZ::ComponentConfig>()
            ->Version(1)
            ->Field("TankPrefab", &ScenePolytreeTankSpawnerConfig::m_tankPrefab)
            ->Field("AiTankCount", &ScenePolytreeTankSpawnerConfig::m_aiTankCount)
            ->Field("Spacing", &ScenePolytreeTankSpawnerConfig::m_spacing);

        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<ScenePolytreeTankSpawnerConfig>("Scene Polytree Tank Spawner Configuration",
                                                        "Creates one player tank and an AI group.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Visibility,
                            AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeTankSpawnerConfig::m_tankPrefab, "Tank Prefab",
                              "Spawnable whose Hull, Turret, and Gun entities are projected by "
                              "scene-polytree.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeTankSpawnerConfig::m_aiTankCount, "AI Tank Count",
                              "Number of AI tanks in addition to the player tank.")
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeTankSpawnerConfig::m_spacing, "Spacing",
                              "Distance between spawned tanks.");
        }
    }
}

void ScenePolytreeTankSpawnerComponent::Reflect(AZ::ReflectContext *context) {
    ScenePolytreeTankSpawnerConfig::Reflect(context);
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<ScenePolytreeTankSpawnerComponent, AZ::Component>()
            ->Version(1)
            ->Field("Configuration", &ScenePolytreeTankSpawnerComponent::m_configuration);
        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<ScenePolytreeTankSpawnerComponent>(
                    "Scene Polytree Tank Spawner",
                    "Spawns and binds one player tank plus multiple AI tanks.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default,
                              &ScenePolytreeTankSpawnerComponent::m_configuration, "Configuration",
                              "Tank scene settings.");
        }
    }
}

void ScenePolytreeTankSpawnerComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeTankSpawnerService"));
}

void ScenePolytreeTankSpawnerComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeTankSpawnerService"));
    incompatible.push_back(AZ_CRC_CE("TankSpawnerService"));
}

void ScenePolytreeTankSpawnerComponent::GetRequiredServices(
    AZ::ComponentDescriptor::DependencyArrayType &required) {
    required.push_back(AZ_CRC_CE("TransformService"));
}

ScenePolytreeTankSpawnerComponent::ScenePolytreeTankSpawnerComponent(
    const ScenePolytreeTankSpawnerConfig &configuration)
    : m_configuration(configuration) {}

void ScenePolytreeTankSpawnerComponent::Activate() {
    auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
    if (requests == nullptr || !m_configuration.m_tankPrefab.GetId().IsValid()) {
        AZ_Warning("ScenePolytree", false,
                   "Tank spawner requires the ScenePolytree system and a tank prefab.");
        return;
    }

    AZ::Transform origin = AZ::Transform::CreateIdentity();
    AZ::TransformBus::EventResult(origin, GetEntityId(), &AZ::TransformBus::Events::GetWorldTM);

    TankSceneDescriptor descriptor;
    const AZ::u32 tankCount = m_configuration.m_aiTankCount + 1;
    descriptor.m_spawnTransforms.reserve(tankCount);
    const auto tankIndices = std::views::iota(AZ::u32{}, tankCount);
    std::ranges::transform(
        tankIndices, std::back_inserter(descriptor.m_spawnTransforms), [&](AZ::u32 index) {
            return origin * AZ::Transform::CreateTranslation(
                                AZ::Vector3(m_configuration.m_spacing * index, 0.0f, 0.0f));
        });

    m_scene = requests->CreateTankScene(descriptor);
    if (!m_scene.IsValid()) {
        AZ_Error("ScenePolytree", false, "Failed to create the tank runtime forest.");
        return;
    }

    m_spawnedTanks.reserve(tankCount);
    std::ranges::for_each(
        tankIndices, [&](AZ::u32 index) { SpawnTank(index, descriptor.m_spawnTransforms[index]); });
}

void ScenePolytreeTankSpawnerComponent::Deactivate() {
    if (auto *requests = AZ::Interface<ScenePolytreeRequests>::Get()) {
        requests->DestroyScene(m_scene);
    }
    m_scene = {};
    std::ranges::for_each(m_spawnedTanks, [](auto &container) { container->Clear(); });
    m_spawnedTanks.clear();
}

bool ScenePolytreeTankSpawnerComponent::ReadInConfig(const AZ::ComponentConfig *baseConfig) {
    if (const auto *configuration =
            azrtti_cast<const ScenePolytreeTankSpawnerConfig *>(baseConfig)) {
        m_configuration = *configuration;
        return true;
    }
    return false;
}

bool ScenePolytreeTankSpawnerComponent::WriteOutConfig(AZ::ComponentConfig *outBaseConfig) const {
    if (auto *configuration = azrtti_cast<ScenePolytreeTankSpawnerConfig *>(outBaseConfig)) {
        *configuration = m_configuration;
        return true;
    }
    return false;
}

void ScenePolytreeTankSpawnerComponent::SpawnTank(AZ::u32 tankIndex,
                                                  const AZ::Transform &spawnTransform) {
    auto container = AZStd::make_unique<AzFramework::SpawnableEntitiesContainer>();
    container->Reset(m_configuration.m_tankPrefab);
    const TankHandle tank{m_scene, tankIndex};
    const bool isPlayer = tankIndex == 0;

    AzFramework::SpawnAllEntitiesOptionalArgs optionalArgs;
    optionalArgs
        .m_preInsertionCallback = [spawnTransform, isPlayer](
                                      [[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId,
                                      AzFramework::SpawnableEntityContainerView entities) {
        if (entities.empty()) {
            return;
        }
        AZ::Entity *adapterEntity = FindArticulationEntity(entities);
        adapterEntity = adapterEntity != nullptr ? adapterEntity : entities[0];
        if (auto *rootTransform = adapterEntity->FindComponent<AzFramework::TransformComponent>()) {
            rootTransform->SetWorldTM(spawnTransform);
        }
        if (isPlayer) {
            adapterEntity->CreateComponent<PlayerTankIntentAdapterComponent>();
        } else {
            adapterEntity->CreateComponent<AiTankIntentAdapterComponent>();
        }

        auto *articulation = adapterEntity->FindComponent<TankArticulationBindingComponent>();
        if (articulation == nullptr || !IsRigidBasis(articulation->GetAssetToLogicalBasis())) {
            AZ_Error("ScenePolytree", false,
                     "Tank spawn requires one articulation binding with a rigid asset basis.");
            return;
        }

        const AZ::Transform &basis = articulation->GetAssetToLogicalBasis();
        PrepareMappedEntity(FindBoundEntity(entities, TankNodeRole::Hull), spawnTransform, basis);
        PrepareMappedEntity(FindBoundEntity(entities, TankNodeRole::Turret), spawnTransform, basis);
        PrepareMappedEntity(FindBoundEntity(entities, TankNodeRole::Gun), spawnTransform, basis);
        PrepareMappedEntity(FindEntityById(entities, articulation->GetTurretPivot()),
                            spawnTransform, basis);
        PrepareMappedEntity(FindEntityById(entities, articulation->GetGunPivot()), spawnTransform,
                            basis);
    };

    optionalArgs.m_completionCallback =
        [tank, isPlayer]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId,
                         AzFramework::SpawnableConstEntityContainerView entities) {
            const AZ::Entity *adapterEntity = FindArticulationEntity(entities);
            const AZ::EntityId adapterEntityId =
                adapterEntity != nullptr
                    ? adapterEntity->GetId()
                    : (entities.empty() ? AZ::EntityId{} : entities[0]->GetId());
            const AZ::Entity *hull = FindBoundEntity(entities, TankNodeRole::Hull);
            const AZ::Entity *turret = FindBoundEntity(entities, TankNodeRole::Turret);
            const AZ::Entity *gun = FindBoundEntity(entities, TankNodeRole::Gun);
            const auto *articulation =
                adapterEntity != nullptr
                    ? adapterEntity->FindComponent<TankArticulationBindingComponent>()
                    : nullptr;
            const bool rolesAreUnique = HasExactlyOneBinding(entities, TankNodeRole::Hull) &&
                                        HasExactlyOneBinding(entities, TankNodeRole::Turret) &&
                                        HasExactlyOneBinding(entities, TankNodeRole::Gun) &&
                                        HasExactlyOneArticulationBinding(entities);
            const AZ::Entity *turretPivot =
                articulation != nullptr ? FindEntityById(entities, articulation->GetTurretPivot())
                                        : nullptr;
            const AZ::Entity *gunPivot = articulation != nullptr
                                             ? FindEntityById(entities, articulation->GetGunPivot())
                                             : nullptr;
            const TankEntityBindings bindings{
                rolesAreUnique && hull != nullptr ? hull->GetId() : AZ::EntityId{},
                rolesAreUnique && turret != nullptr ? turret->GetId() : AZ::EntityId{},
                rolesAreUnique && gun != nullptr ? gun->GetId() : AZ::EntityId{},
                rolesAreUnique && turretPivot != nullptr ? turretPivot->GetId() : AZ::EntityId{},
                rolesAreUnique && gunPivot != nullptr ? gunPivot->GetId() : AZ::EntityId{},
            };
            AZ::TickBus::QueueFunction([tank, isPlayer, adapterEntityId, bindings]() {
                auto *requests = AZ::Interface<ScenePolytreeRequests>::Get();
                if (requests == nullptr || !requests->IsSceneAlive(tank.m_scene)) {
                    return;
                }
                if (!requests->BindTankEntities(tank, bindings)) {
                    AZ_Error("ScenePolytree", false,
                             "Tank spawn did not provide complete projection bindings.");
                    return;
                }
                TankAdapterRequestBus::Event(adapterEntityId, &TankAdapterRequests::AssignTank,
                                             tank);
                if (!isPlayer) {
                    const float direction = (tank.m_index % 2) == 0 ? 1.0f : -1.0f;
                    AiTankGoalRequestBus::Event(
                        adapterEntityId, &AiTankGoalRequests::SubmitGoal,
                        AiTankGoal{1.25f, 0.15f * direction, -0.1f * direction, 0.0f});
                }
                (void)requests->MarkTankReady(tank);
                (void)requests->SetSceneActive(tank.m_scene, true);
            });
        };

    container->SpawnAllEntities(AZStd::move(optionalArgs));
    m_spawnedTanks.push_back(AZStd::move(container));
}
} // namespace ScenePolytree
