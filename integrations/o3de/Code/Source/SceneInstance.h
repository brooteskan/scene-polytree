#pragma once

#include "AzTransformPolicy.h"

#include <ScenePolytree/ScenePolytreeBus.h>

#include <AzCore/std/function/function_fwd.h>

#include <scene_polytree/authoring_scene.hpp>
#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_freeze.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace ScenePolytree::Internal {
struct StableTankInstance {
    wz::core::graph::StableNodeId m_hull;
    wz::core::graph::StableNodeId m_turret;
    wz::core::graph::StableNodeId m_gun;
};

struct RuntimeTankInstance {
    wz::core::graph::NodeHandle m_hull{wz::core::graph::INVALID_NODE};
    wz::core::graph::NodeHandle m_turret{wz::core::graph::INVALID_NODE};
    wz::core::graph::NodeHandle m_gun{wz::core::graph::INVALID_NODE};
};

struct StableNodeBinding {
    AZ::Name m_bindingId;
    wz::core::graph::StableNodeId m_node;
    AZ::Transform m_initialLocal{AZ::Transform::CreateIdentity()};
};

struct StableSlot {
    AZStd::vector<StableNodeBinding> m_nodes;
};

struct StablePartition {
    AZ::u64 m_partition{};
    AZStd::vector<StableSlot> m_slots;
};

struct RuntimeNodeBinding {
    AZ::Name m_bindingId;
    wz::core::graph::NodeHandle m_node{wz::core::graph::INVALID_NODE};
    AZ::Transform m_initialLocal{AZ::Transform::CreateIdentity()};
};

struct RuntimeSlot {
    AZStd::vector<RuntimeNodeBinding> m_nodes;
    AZ::u32 m_generation{1};
    AZ::u32 m_tankIndex{std::numeric_limits<AZ::u32>::max()};
    bool m_reserved{};
};

struct RuntimePartition {
    AZ::u64 m_partition{};
    AZStd::vector<RuntimeSlot> m_slots;
};

struct EntityTarget {
    AZ::EntityId m_entity;
    AZ::Transform m_nodeToTarget{AZ::Transform::CreateIdentity()};

    EntityTarget() = default;
    EntityTarget(AZ::EntityId entity, const AZ::Transform &nodeToTarget)
        : m_entity(entity), m_nodeToTarget(nodeToTarget) {}
};

class SceneInstance final {
  public:
    using AuthoringScene =
        scene_polytree::basic_authoring_scene<ScenePolytreeNodeType, ScenePolytreeJointType,
                                              AzTransformValue>;
    using RuntimeScene =
        scene_polytree::basic_runtime_scene<ScenePolytreeNodeType, ScenePolytreeJointType,
                                            AzTransformValue>;
    using ActiveSet = scene_polytree::motion::active_motion_set<AZ::Vector3, AZ::Vector3>;
    using TransformWriter = AZStd::function<void(AZ::EntityId, const AZ::Transform &)>;

    [[nodiscard]] static std::unique_ptr<SceneInstance>
    Create(const TankSceneDescriptor &descriptor);
    [[nodiscard]] static std::unique_ptr<SceneInstance>
    Create(const ScenePolytreeSceneDescriptor &descriptor);

    [[nodiscard]] SlotResult ReserveSlot(SpawnerHandle spawner);
    [[nodiscard]] ScenePolytreeResultCode PlaceSlot(SlotHandle slot,
                                                    const AZ::Transform &rootWorld);
    [[nodiscard]] ScenePolytreeResultCode
    BindSlot(SlotHandle slot, const AZStd::vector<ScenePolytreeEntityBinding> &bindings);
    [[nodiscard]] ScenePolytreeResultCode UnbindSlot(SlotHandle slot);
    [[nodiscard]] ScenePolytreeResultCode ResetSlot(SlotHandle slot);
    [[nodiscard]] ScenePolytreeResultCode ReleaseSlot(SlotHandle slot);
    [[nodiscard]] NodeResult ResolveNode(SlotHandle slot, const AZ::Name &bindingId) const;
    [[nodiscard]] TankHandle ResolveTank(SlotHandle slot) const;

    bool Bind(AZ::u32 tankIndex, const TankEntityBindings &bindings);
    bool BindProjected(AZ::u32 tankIndex, const TankEntityBindings &bindings,
                       const std::array<AZ::Transform, 3> &targetWorldTransforms,
                       const std::array<AZ::Transform, 2> &pivotWorldTransforms);
    bool Unbind(AZ::u32 tankIndex);
    bool MarkReady(AZ::u32 tankIndex);
    bool SetActive(bool active);
    bool SubmitIntent(AZ::u32 tankIndex, const TankIntent &intent);
    bool CorrectLocal(AZ::u32 tankIndex, TankNodeRole role, const AZ::Transform &local);
    bool CorrectWorld(AZ::u32 tankIndex, TankNodeRole role, const AZ::Transform &world);
    void Advance(std::chrono::nanoseconds frameDelta, const TransformWriter &writer);

    [[nodiscard]] bool NeedsTick() const;
    [[nodiscard]] SceneStatistics GetStatistics() const;
    [[nodiscard]] std::size_t TankCount() const noexcept { return m_tanks.size(); }

  private:
    SceneInstance(RuntimeScene runtime, std::vector<RuntimeTankInstance> tanks,
                  AZStd::vector<RuntimePartition> partitions, std::chrono::nanoseconds fixedStep,
                  AZ::u32 maxCatchUpSteps, bool requireAllTanksReady);

    [[nodiscard]] bool AllBound() const;
    [[nodiscard]] bool AllReady() const;
    [[nodiscard]] bool AnyReadyAndBound() const;
    [[nodiscard]] bool HasDirtyTransforms() const;
    [[nodiscard]] bool BindWithOffsets(AZ::u32 tankIndex, const TankEntityBindings &bindings,
                                       const std::array<AZ::Transform, 3> &nodeToTargets);
    void EvaluateDirty();
    void QueueEvaluated(std::span<const wz::core::graph::NodeHandle> changed);
    void ResetEvaluatedBatch();
    void Synchronize(const TransformWriter &writer);
    [[nodiscard]] wz::core::graph::NodeHandle NodeForRole(AZ::u32 tankIndex,
                                                          TankNodeRole role) const;
    [[nodiscard]] RuntimePartition *FindPartition(const SpawnerHandle &spawner);
    [[nodiscard]] const RuntimePartition *FindPartition(const SpawnerHandle &spawner) const;
    [[nodiscard]] RuntimeSlot *FindSlot(const SlotHandle &slot);
    [[nodiscard]] const RuntimeSlot *FindSlot(const SlotHandle &slot) const;
    void ClearSlot(RuntimeSlot &slot);

    RuntimeScene m_runtime;
    std::vector<RuntimeTankInstance> m_tanks;
    AZStd::vector<RuntimePartition> m_partitions;
    ActiveSet m_activeMotion;
    std::vector<EntityTarget> m_entityBindings;
    std::vector<bool> m_readyTanks;
    scene_polytree::motion::fixed_step_sequence m_stepSequence;
    scene_polytree::motion::motion_evaluation_workspace<AzTransformValue> m_motionWorkspace;
    scene_polytree::transform_evaluation_workspace m_transformWorkspace;
    AzTransformPolicy m_policy;
    std::vector<wz::core::graph::NodeHandle> m_changedScratch;
    std::vector<wz::core::graph::NodeHandle> m_evaluatedChanges;
    std::vector<std::uint64_t> m_evaluatedStamp;
    std::vector<std::uint32_t> m_topologicalRank;
    std::chrono::nanoseconds m_accumulator{};
    scene_polytree::scene_revision m_lastSynchronizedRevision{};
    AZ::u32 m_maxCatchUpSteps{4};
    AZ::u32 m_lastSynchronizedNodeCount{};
    std::uint64_t m_evaluatedEpoch{1};
    bool m_directBatchValid{true};
    bool m_requireAllTanksReady{true};
    bool m_active{};
};
} // namespace ScenePolytree::Internal
