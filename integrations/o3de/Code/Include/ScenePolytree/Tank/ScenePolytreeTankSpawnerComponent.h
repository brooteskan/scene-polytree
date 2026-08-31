#pragma once

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesContainer.h>

namespace ScenePolytree {
class ScenePolytreeTankSpawnerConfig final : public AZ::ComponentConfig {
  public:
    AZ_CLASS_ALLOCATOR(ScenePolytreeTankSpawnerConfig, AZ::SystemAllocator);
    AZ_RTTI(ScenePolytreeTankSpawnerConfig, ScenePolytreeTankSpawnerConfigTypeId,
            AZ::ComponentConfig);

    static void Reflect(AZ::ReflectContext *context);

    AZ::Data::Asset<AzFramework::Spawnable> m_tankPrefab;
    AZ::u32 m_aiTankCount{3};
    float m_spacing{8.0f};
};

class ScenePolytreeTankSpawnerComponent final : public AZ::Component {
  public:
    AZ_COMPONENT_DECL(ScenePolytreeTankSpawnerComponent);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);
    static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType &required);

    ScenePolytreeTankSpawnerComponent() = default;
    explicit ScenePolytreeTankSpawnerComponent(const ScenePolytreeTankSpawnerConfig &configuration);
    ScenePolytreeTankSpawnerComponent(const ScenePolytreeTankSpawnerComponent &) = delete;
    ScenePolytreeTankSpawnerComponent &
    operator=(const ScenePolytreeTankSpawnerComponent &) = delete;

    void Activate() override;
    void Deactivate() override;
    bool ReadInConfig(const AZ::ComponentConfig *baseConfig) override;
    bool WriteOutConfig(AZ::ComponentConfig *outBaseConfig) const override;

  private:
    void SpawnTank(AZ::u32 tankIndex, const AZ::Transform &spawnTransform);

    ScenePolytreeTankSpawnerConfig m_configuration;
    SceneHandle m_scene;
    AZStd::vector<AZStd::unique_ptr<AzFramework::SpawnableEntitiesContainer>> m_spawnedTanks;
};
} // namespace ScenePolytree
