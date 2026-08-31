#pragma once

#include "AzTransformPolicy.h"

#include <ScenePolytree/ScenePolytreeBus.h>

#include <AzCore/std/function/function_fwd.h>

#include <scene_polytree/authoring_scene.hpp>
#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_freeze.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <vector>

namespace ScenePolytree::Internal {
enum class TankNode : AZ::u8 { Hull, Turret, Gun };
enum class TankJoint : AZ::u8 { Turret, Gun };

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
        scene_polytree::basic_authoring_scene<TankNode, TankJoint, AzTransformValue>;
    using RuntimeScene = scene_polytree::basic_runtime_scene<TankNode, TankJoint, AzTransformValue>;
    using ActiveSet = scene_polytree::motion::active_motion_set<AZ::Vector3, AZ::Vector3>;
    using TransformWriter = AZStd::function<void(AZ::EntityId, const AZ::Transform &)>;

    [[nodiscard]] static std::unique_ptr<SceneInstance>
    Create(const TankSceneDescriptor &descriptor);

    bool Bind(AZ::u32 tankIndex, const TankEntityBindings &bindings);
    bool BindProjected(AZ::u32 tankIndex, const TankEntityBindings &bindings,
                       const std::array<AZ::Transform, 3> &targetWorldTransforms);
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
                  std::chrono::nanoseconds fixedStep, AZ::u32 maxCatchUpSteps);

    [[nodiscard]] bool AllBound() const;
    [[nodiscard]] bool AllReady() const;
    [[nodiscard]] bool HasDirtyTransforms() const;
    [[nodiscard]] bool BindWithOffsets(AZ::u32 tankIndex, const TankEntityBindings &bindings,
                                       const std::array<AZ::Transform, 3> &nodeToTargets);
    void EvaluateDirty();
    void Synchronize(const TransformWriter &writer);
    [[nodiscard]] wz::core::graph::NodeHandle NodeForRole(AZ::u32 tankIndex,
                                                          TankNodeRole role) const;

    RuntimeScene m_runtime;
    std::vector<RuntimeTankInstance> m_tanks;
    ActiveSet m_activeMotion;
    std::vector<EntityTarget> m_entityBindings;
    std::vector<bool> m_readyTanks;
    scene_polytree::motion::fixed_step_sequence m_stepSequence;
    scene_polytree::motion::motion_evaluation_workspace<AzTransformValue> m_motionWorkspace;
    scene_polytree::transform_evaluation_workspace m_transformWorkspace;
    AzTransformPolicy m_policy;
    std::vector<wz::core::graph::NodeHandle> m_changedScratch;
    std::chrono::nanoseconds m_accumulator{};
    scene_polytree::scene_revision m_lastSynchronizedRevision{};
    AZ::u32 m_maxCatchUpSteps{4};
    AZ::u32 m_lastSynchronizedNodeCount{};
    bool m_active{};
};
} // namespace ScenePolytree::Internal
