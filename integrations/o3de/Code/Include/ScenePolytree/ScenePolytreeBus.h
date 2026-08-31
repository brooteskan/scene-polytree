#pragma once

#include <ScenePolytree/ScenePolytreeTypes.h>

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/RTTI/TypeInfo.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/parallel/mutex.h>

namespace ScenePolytree {
struct TankHandle {
    SceneHandle m_scene;
    AZ::u32 m_index{};
    [[nodiscard]] bool IsValid() const noexcept { return m_scene.IsValid(); }
    friend bool operator==(const TankHandle &, const TankHandle &) = default;
};

enum class TankNodeRole : AZ::u8 { Hull, Turret, Gun };

enum class SceneCorrectionSpace : AZ::u8 { Local, World };
enum class SceneCorrectionSource : AZ::u8 { Terrain, Physics, Gameplay, External };

struct TankIntent {
    float m_forwardSpeed{};
    float m_hullYawRate{};
    float m_turretYawRate{};
    float m_gunPitchRate{};
    friend bool operator==(const TankIntent &, const TankIntent &) = default;
};

struct PlayerTankInput {
    float m_moveAxis{};
    float m_turnAxis{};
    float m_turretAxis{};
    float m_gunAxis{};
};

struct AiTankGoal {
    float m_forwardSpeed{};
    float m_hullYawRate{};
    float m_turretYawRate{};
    float m_gunPitchRate{};
};

struct TankTuning {
    float m_maxForwardSpeed{5.0f};
    float m_maxHullYawRate{1.5f};
    float m_maxTurretYawRate{2.0f};
    float m_maxGunPitchRate{1.0f};
};

[[nodiscard]] TankIntent MakePlayerTankIntent(const PlayerTankInput &input,
                                              const TankTuning &tuning) noexcept;
[[nodiscard]] TankIntent MakeAiTankIntent(const AiTankGoal &goal) noexcept;

struct TankSceneDescriptor {
    AZStd::vector<AZ::Transform> m_spawnTransforms;
    AZ::s64 m_fixedStepNanoseconds{16'666'667};
    AZ::u32 m_maxCatchUpSteps{4};
};

struct TankEntityBindings {
    AZ::EntityId m_hull;
    AZ::EntityId m_turret;
    AZ::EntityId m_gun;
    AZ::EntityId m_turretPivot;
    AZ::EntityId m_gunPivot;
    [[nodiscard]] bool IsComplete() const noexcept {
        return m_hull.IsValid() && m_turret.IsValid() && m_gun.IsValid() &&
               m_turretPivot.IsValid() && m_gunPivot.IsValid();
    }
};

struct SceneCorrection {
    TankHandle m_tank;
    TankNodeRole m_role{TankNodeRole::Hull};
    AZ::Transform m_transform{AZ::Transform::CreateIdentity()};
    SceneCorrectionSpace m_space{SceneCorrectionSpace::Local};
    SceneCorrectionSource m_source{SceneCorrectionSource::External};
};

struct SceneStatistics {
    AZ::u32 m_tankCount{};
    AZ::u32 m_activeMotionCount{};
    AZ::u32 m_lastSynchronizedNodeCount{};
    AZ::u64 m_completedFixedSteps{};
    bool m_active{};
    AZ::u32 m_partitionCount{};
    AZ::u32 m_slotCapacity{};
    AZ::u32 m_reservedSlotCount{};
    AZ::u32 m_boundSlotCount{};
};

class ScenePolytreeRequests {
  public:
    AZ_RTTI(ScenePolytreeRequests, "{8B7CA3BE-A7A9-4492-AA97-A2C4BCCD7E39}");
    virtual ~ScenePolytreeRequests() = default;
    virtual SceneHandle CreateScene(const ScenePolytreeSceneDescriptor &descriptor) = 0;
    virtual SceneHandle CreateTankScene(const TankSceneDescriptor &descriptor) = 0;
    virtual void DestroyScene(SceneHandle scene) = 0;
    virtual SlotResult ReserveSlot(SpawnerHandle spawner) = 0;
    virtual ScenePolytreeResultCode PlaceSlot(SlotHandle slot, const AZ::Transform &rootWorld) = 0;
    virtual ScenePolytreeResultCode
    BindSlot(SlotHandle slot, const AZStd::vector<ScenePolytreeEntityBinding> &bindings) = 0;
    virtual ScenePolytreeResultCode UnbindSlot(SlotHandle slot) = 0;
    virtual ScenePolytreeResultCode ResetSlot(SlotHandle slot) = 0;
    virtual ScenePolytreeResultCode ReleaseSlot(SlotHandle slot) = 0;
    virtual NodeResult ResolveNode(SlotHandle slot, const AZ::Name &bindingId) const = 0;
    virtual TankHandle ResolveTank(SlotHandle slot) const = 0;
    virtual bool BindTankEntities(TankHandle tank, const TankEntityBindings &bindings) = 0;
    virtual bool RemoveTankEntities(TankHandle tank) = 0;
    virtual bool MarkTankReady(TankHandle tank) = 0;
    virtual bool SetSceneActive(SceneHandle scene, bool active) = 0;
    virtual bool SubmitTankIntent(TankHandle tank, const TankIntent &intent) = 0;
    virtual bool RequestCorrection(const SceneCorrection &correction) = 0;
    virtual SceneStatistics GetSceneStatistics(SceneHandle scene) const = 0;
    virtual bool IsSceneAlive(SceneHandle scene) const = 0;
    virtual bool IsSceneReady(SceneHandle scene) const = 0;
};

class ScenePolytreeRequestBusTraits final : public AZ::EBusTraits {
  public:
    static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
    static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;
    using MutexType = AZStd::recursive_mutex;
};
using ScenePolytreeRequestBus = AZ::EBus<ScenePolytreeRequests, ScenePolytreeRequestBusTraits>;

class ScenePolytreeRegistrationRequests {
  public:
    AZ_RTTI(ScenePolytreeRegistrationRequests, "{55862746-519E-42A8-B9C6-BB105F4F7EE6}");
    virtual ~ScenePolytreeRegistrationRequests() = default;
    virtual ScenePolytreeResultCode RegisterSceneEntity(AZ::EntityId sceneEntity,
                                                        bool isDefault) = 0;
    virtual void UnregisterSceneEntity(AZ::EntityId sceneEntity) = 0;
    virtual RegistrationResult
    RegisterPrefab(AZ::EntityId ownerEntity, AZ::EntityId targetScene,
                   const ScenePolytreePrefabRegistrationDescriptor &descriptor) = 0;
    virtual void UnregisterPrefab(RegistrationToken token) = 0;
};

class ScenePolytreeComponentRequests : public AZ::ComponentBus {
  public:
    virtual void
    BeginBuild(const AZStd::vector<ResolvedScenePolytreeRegistration> &registrations) = 0;
    virtual void FailBuild(const ScenePolytreeFailure &failure) = 0;
    virtual ScenePolytreeLifecycle GetLifecycle() const = 0;
    virtual ScenePolytreeFailure GetFailure() const = 0;
};
using ScenePolytreeComponentRequestBus = AZ::EBus<ScenePolytreeComponentRequests>;

class ScenePolytreeRegistrationNotifications : public AZ::ComponentBus {
  public:
    virtual void OnScenePolytreeRegistrationReady(RegistrationToken token,
                                                  SpawnerHandle spawner) = 0;
    virtual void OnScenePolytreeRegistrationFailed(RegistrationToken token,
                                                   const ScenePolytreeFailure &failure) = 0;
};
using ScenePolytreeRegistrationNotificationBus = AZ::EBus<ScenePolytreeRegistrationNotifications>;

class ScenePolytreeSystemNotifications : public AZ::EBusTraits {
  public:
    static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
    static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
    using BusIdType = AZ::u64;
    virtual void OnSceneReady(SceneHandle scene) = 0;
    virtual void OnSceneFailed(SceneHandle scene, const ScenePolytreeFailure &failure) = 0;
};
using ScenePolytreeSystemNotificationBus = AZ::EBus<ScenePolytreeSystemNotifications>;

class TankAdapterRequests : public AZ::ComponentBus {
  public:
    virtual void AssignTank(TankHandle tank) = 0;
};
using TankAdapterRequestBus = AZ::EBus<TankAdapterRequests>;

class AiTankGoalRequests : public AZ::ComponentBus {
  public:
    virtual void SubmitGoal(const AiTankGoal &goal) = 0;
};
using AiTankGoalRequestBus = AZ::EBus<AiTankGoalRequests>;
} // namespace ScenePolytree

namespace AZ {
AZ_TYPE_INFO_SPECIALIZE(ScenePolytree::TankNodeRole, "{0FF1E318-B350-443C-BA88-4C497E41649D}");
}
