#pragma once

#include "AzTransformPolicy.h"

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeController.h>

#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/function/function_fwd.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>

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
    using MotionUpdateWorkspace =
        scene_polytree::motion::active_motion_update_workspace<AZ::Vector3, AZ::Vector3>;
    using TransformWriter = AZStd::function<void(AZ::EntityId, const AZ::Transform &)>;
    using ControllerFactoryResolver =
        AZStd::function<AZStd::shared_ptr<const ScenePolytreeControllerFactory>(
            ScenePolytreeControllerTypeId)>;

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
    [[nodiscard]] ScenePolytreeResultCode
    AttachControllers(InstanceHandle instance,
                      AZStd::vector<ScenePolytreeControllerDeclaration> declarations,
                      const ControllerFactoryResolver &resolveFactory);
    [[nodiscard]] ScenePolytreeControllerResultCode CloseControllerInput(InstanceHandle instance);
    [[nodiscard]] ScenePolytreeControllerResultCode DetachControllers(InstanceHandle instance);
    [[nodiscard]] ScenePolytreeControllerLookupResult
    FindController(InstanceHandle instance, const AZ::Name &declarationId) const;
    [[nodiscard]] ScenePolytreeControllerResultCode
    SubmitControllerInput(ScenePolytreeControllerHandle controller,
                          const ScenePolytreeControllerInput &input);
    [[nodiscard]] bool HasControllerType(ScenePolytreeControllerTypeId typeId) const;

    bool SetActive(bool active);
    bool CorrectLocal(const SceneNodeHandle &node, const AZ::Transform &local);
    bool CorrectWorld(const SceneNodeHandle &node, const AZ::Transform &world);
    void Advance(std::chrono::nanoseconds frameDelta, const TransformWriter &writer);

    [[nodiscard]] bool NeedsTick() const;
    [[nodiscard]] SceneStatistics GetStatistics() const;

  private:
    friend class ControllerCommandSinkAccess;

    struct ControllerTargetRecord {
        SceneNodeHandle m_node;
        wz::core::graph::NodeHandle m_runtimeNode{wz::core::graph::INVALID_NODE};
        ScenePolytreeControllerTargetAccess m_access{ScenePolytreeControllerTargetAccess::ReadOnly};
        AZ::Transform m_pendingLocal{AZ::Transform::CreateIdentity()};
        scene_polytree::motion::motion_state<AZ::Vector3, AZ::Vector3> m_pendingMotion;
        AZ::u64 m_localEpoch{};
        AZ::u64 m_motionEpoch{};
        AZ::u32 m_generation{1};
        bool m_active{};
    };

    struct ControllerRecord {
        ScenePolytreeControllerHandle m_handle;
        ScenePolytreeControllerStateHandle m_batchState;
        AZ::Name m_declarationId;
        AZStd::vector<ScenePolytreeControllerTargetToken> m_targets;
        bool m_inputClosed{};
    };

    struct ControllerBatchEntry {
        ScenePolytreeControllerTypeId m_typeId{AZ::TypeId::CreateNull()};
        AZStd::shared_ptr<const ScenePolytreeControllerFactory> m_factory;
        AZStd::unique_ptr<ScenePolytreeControllerBatch> m_batch;
    };

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
    [[nodiscard]] NodeResult ResolveEntityNode(SlotHandle slot, AZ::EntityId entity) const;
    [[nodiscard]] ControllerBatchEntry *FindControllerBatch(ScenePolytreeControllerTypeId typeId);
    [[nodiscard]] const ControllerBatchEntry *
    FindControllerBatch(ScenePolytreeControllerTypeId typeId) const;
    [[nodiscard]] ControllerTargetRecord *
    FindControllerTarget(ScenePolytreeControllerTargetToken target, AZ::u64 epoch);
    [[nodiscard]] const ControllerTargetRecord *
    FindControllerTarget(ScenePolytreeControllerTargetToken target, AZ::u64 epoch) const;
    [[nodiscard]] ScenePolytreeControllerTargetToken
    AllocateControllerTarget(const SceneNodeHandle &node, wz::core::graph::NodeHandle runtimeNode,
                             ScenePolytreeControllerTargetAccess access);
    void ReleaseControllerTarget(ScenePolytreeControllerTargetToken target);
    [[nodiscard]] bool HasControllers(SlotHandle slot) const;
    void BeginControllerCommands(ScenePolytreeControllerCommandSink &sink);
    void EndControllerCommands(ScenePolytreeControllerCommandSink &sink, bool apply);
    [[nodiscard]] bool HasRunningControllers() const;
    void RunControllers(scene_polytree::motion::fixed_motion_step step);
    [[nodiscard]] ScenePolytreeControllerResultCode
    SetControllerLocal(ScenePolytreeControllerTargetToken target, AZ::u64 epoch,
                       const AZ::Transform &local);
    [[nodiscard]] ScenePolytreeControllerResultCode
    SetControllerVelocity(ScenePolytreeControllerTargetToken target, AZ::u64 epoch,
                          const AZ::Vector3 &linear, const AZ::Vector3 &angular);
    [[nodiscard]] ScenePolytreeControllerResultCode
    GetControllerLocal(ScenePolytreeControllerTargetToken target, AZ::u64 epoch,
                       AZ::Transform &local) const;
    void ClearSlot(RuntimeSlot &slot);

    RuntimeScene m_runtime;
    AZStd::vector<RuntimePartition> m_partitions;
    ActiveSet m_activeMotion;
    MotionUpdateWorkspace m_activeMotionUpdateWorkspace;
    std::vector<EntityTarget> m_entityBindings;
    AZStd::unordered_map<AZ::EntityId, wz::core::graph::NodeHandle> m_entityToNode;
    AZStd::vector<ControllerBatchEntry> m_controllerBatches;
    AZStd::vector<ControllerRecord> m_controllers;
    AZStd::vector<ControllerTargetRecord> m_controllerTargets;
    AZStd::vector<AZ::u32> m_freeControllerTargets;
    AZStd::vector<AZ::u32> m_controllerWriteOwners;
    AZStd::vector<AZ::u32> m_localTouched;
    AZStd::vector<AZ::u32> m_motionTouched;
    AZStd::vector<ActiveSet::update_type> m_controllerMotionUpdates;
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
    AZ::u64 m_controllerCommandEpoch{1};
    AZ::u32 m_nextControllerHandle{1};
    AZ::u32 m_controllerHandleGeneration{1};
    bool m_directBatchValid{true};
    bool m_active{};
};
} // namespace ScenePolytree::Internal
