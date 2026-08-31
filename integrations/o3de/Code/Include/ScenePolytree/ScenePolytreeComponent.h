#pragma once

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>

namespace ScenePolytree {
class ScenePolytreeComponentConfig final : public AZ::ComponentConfig {
  public:
    AZ_CLASS_ALLOCATOR(ScenePolytreeComponentConfig, AZ::SystemAllocator);
    AZ_RTTI(ScenePolytreeComponentConfig, ScenePolytreeComponentConfigTypeId, AZ::ComponentConfig);

    static void Reflect(AZ::ReflectContext *context);

    bool m_isDefault{true};
    AZ::s64 m_fixedStepNanoseconds{16'666'667};
    AZ::u32 m_maxCatchUpSteps{4};
    AZStd::vector<ScenePolytreeNodeDescriptor> m_permanentNodes;
};

class ScenePolytreeComponent final : public AZ::Component,
                                     public ScenePolytreeComponentRequestBus::Handler,
                                     public AZ::Data::AssetBus::MultiHandler,
                                     public ScenePolytreeSystemNotificationBus::Handler {
  public:
    AZ_COMPONENT(ScenePolytreeComponent, ScenePolytreeComponentTypeId);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);

    ScenePolytreeComponent() = default;
    explicit ScenePolytreeComponent(const ScenePolytreeComponentConfig &configuration);

    void Activate() override;
    void Deactivate() override;
    bool ReadInConfig(const AZ::ComponentConfig *baseConfig) override;
    bool WriteOutConfig(AZ::ComponentConfig *outBaseConfig) const override;

    void BeginBuild(const AZStd::vector<ResolvedScenePolytreeRegistration> &registrations) override;
    void FailBuild(const ScenePolytreeFailure &failure) override;
    ScenePolytreeLifecycle GetLifecycle() const override { return m_lifecycle; }
    ScenePolytreeFailure GetFailure() const override { return m_failure; }
    [[nodiscard]] SceneHandle GetSceneHandle() const noexcept { return m_scene; }

    void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override;
    void OnAssetError(AZ::Data::Asset<AZ::Data::AssetData> asset) override;
    void OnAssetCanceled(AZ::Data::AssetId assetId) override;
    void OnSceneReady(SceneHandle scene) override;
    void OnSceneFailed(SceneHandle scene, const ScenePolytreeFailure &failure) override;

  private:
    void TryBuild();
    void NotifyFailure();

    ScenePolytreeComponentConfig m_configuration;
    AZStd::vector<ResolvedScenePolytreeRegistration> m_registrations;
    ScenePolytreeFailure m_failure;
    SceneHandle m_scene;
    ScenePolytreeLifecycle m_lifecycle{ScenePolytreeLifecycle::Collecting};
};
} // namespace ScenePolytree
