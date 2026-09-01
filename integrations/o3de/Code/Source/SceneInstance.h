#pragma once

#include "AzTransformPolicy.h"

#include <ScenePolytree/ScenePolytreeBus.h>

#include <AzCore/std/function/function_fwd.h>

#include <scene_polytree/authoring_scene.hpp>
#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_freeze.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ScenePolytree::Internal {
enum class TransformNodePayload : AZ::u8 { Transform };
enum class TransformEdgePayload : AZ::u8 { Parent };

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
        scene_polytree::basic_authoring_scene<TransformNodePayload, TransformEdgePayload,
                                              AzTransformValue>;
    using RuntimeScene =
        scene_polytree::basic_runtime_scene<TransformNodePayload, TransformEdgePayload,
                                            AzTransformValue>;
    using ActiveSet = scene_polytree::motion::active_motion_set<AZ::Vector3, AZ::Vector3>;
    using TransformWriter = AZStd::function<void(AZ::EntityId, const AZ::Transform &)>;

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

    bool SetActive(bool active);
    bool CorrectLocal(const SceneNodeHandle &node, const AZ::Transform &local);
    bool CorrectWorld(const SceneNodeHandle &node, const AZ::Transform &world);
    void Advance(std::chrono::nanoseconds frameDelta, const TransformWriter &writer);

    [[nodiscard]] bool NeedsTick() const;
    [[nodiscard]] SceneStatistics GetStatistics() const;

  private:
    SceneInstance(RuntimeScene runtime, AZStd::vector<RuntimePartition> partitions,
                  std::chrono::nanoseconds fixedStep, AZ::u32 maxCatchUpSteps);

    [[nodiscard]] bool HasDirtyTransforms() const;
    void EvaluateDirty();
    void QueueEvaluated(std::span<const wz::core::graph::NodeHandle> changed);
    void ResetEvaluatedBatch();
    void Synchronize(const TransformWriter &writer);
    [[nodiscard]] RuntimePartition *FindPartition(const SpawnerHandle &spawner);
    [[nodiscard]] const RuntimePartition *FindPartition(const SpawnerHandle &spawner) const;
    [[nodiscard]] RuntimeSlot *FindSlot(const SlotHandle &slot);
    [[nodiscard]] const RuntimeSlot *FindSlot(const SlotHandle &slot) const;
    [[nodiscard]] RuntimeNodeBinding *FindNode(const SceneNodeHandle &node);
    [[nodiscard]] const RuntimeNodeBinding *FindNode(const SceneNodeHandle &node) const;
    void ClearSlot(RuntimeSlot &slot);

    RuntimeScene m_runtime;
    AZStd::vector<RuntimePartition> m_partitions;
    ActiveSet m_activeMotion;
    std::vector<EntityTarget> m_entityBindings;
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
    bool m_active{};
};
} // namespace ScenePolytree::Internal
