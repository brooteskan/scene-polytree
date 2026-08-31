#include "SceneInstance.h"

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/Debug/Trace.h>

#include <algorithm>
#include <array>
#include <ranges>

namespace ScenePolytree::Internal {
namespace {
[[nodiscard]] StableTankInstance InstantiateTank(SceneInstance::AuthoringScene &scene,
                                                 const AZ::Transform &spawnTransform) {
    const auto hull = scene.insert_root(TankNode::Hull, AzTransformValue(spawnTransform)).value();
    const auto turret = scene
                            .insert_child(hull, TankNode::Turret, TankJoint::Turret,
                                          AzTransformValue(AZ::Transform::CreateIdentity()))
                            .value();
    const auto gun = scene
                         .insert_child(turret, TankNode::Gun, TankJoint::Gun,
                                       AzTransformValue(AZ::Transform::CreateIdentity()))
                         .value();
    return {hull, turret, gun};
}

[[nodiscard]] RuntimeTankInstance ResolveTank(const SceneInstance::RuntimeScene &runtime,
                                              const StableTankInstance &stable) {
    return {
        runtime.identities().runtime_handle(stable.m_hull).value(),
        runtime.identities().runtime_handle(stable.m_turret).value(),
        runtime.identities().runtime_handle(stable.m_gun).value(),
    };
}
} // namespace

std::unique_ptr<SceneInstance> SceneInstance::Create(const TankSceneDescriptor &descriptor) {
    if (descriptor.m_spawnTransforms.empty() || descriptor.m_fixedStepNanoseconds <= 0 ||
        descriptor.m_maxCatchUpSteps == 0) {
        return {};
    }

    AuthoringScene authoring;
    std::vector<StableTankInstance> stableTanks;
    stableTanks.reserve(descriptor.m_spawnTransforms.size());
    std::ranges::transform(descriptor.m_spawnTransforms, std::back_inserter(stableTanks),
                           [&](const AZ::Transform &spawnTransform) {
                               return InstantiateTank(authoring, spawnTransform);
                           });

    wz::core::graph::FreezeWorkspace freezeWorkspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freezeWorkspace);
    if (!frozen) {
        return {};
    }

    RuntimeScene runtime = std::move(frozen).value();
    std::vector<RuntimeTankInstance> runtimeTanks;
    runtimeTanks.reserve(stableTanks.size());
    std::ranges::transform(
        stableTanks, std::back_inserter(runtimeTanks),
        [&](const StableTankInstance &tank) { return ResolveTank(runtime, tank); });

    return std::unique_ptr<SceneInstance>(new SceneInstance(
        std::move(runtime), std::move(runtimeTanks),
        std::chrono::nanoseconds(descriptor.m_fixedStepNanoseconds), descriptor.m_maxCatchUpSteps));
}

SceneInstance::SceneInstance(RuntimeScene runtime, std::vector<RuntimeTankInstance> tanks,
                             std::chrono::nanoseconds fixedStep, AZ::u32 maxCatchUpSteps)
    : m_runtime(std::move(runtime)), m_tanks(std::move(tanks)),
      m_activeMotion(m_runtime.topology()), m_entityBindings(m_runtime.state().size()),
      m_readyTanks(m_tanks.size(), false), m_stepSequence(fixedStep),
      m_maxCatchUpSteps(maxCatchUpSteps) {}

bool SceneInstance::MarkReady(AZ::u32 tankIndex) {
    if (tankIndex >= m_readyTanks.size()) {
        return false;
    }
    m_readyTanks[tankIndex] = true;
    return true;
}

bool SceneInstance::Bind(AZ::u32 tankIndex, const TankEntityBindings &bindings) {
    const std::array identityOffsets{
        AZ::Transform::CreateIdentity(),
        AZ::Transform::CreateIdentity(),
        AZ::Transform::CreateIdentity(),
    };
    return BindWithOffsets(tankIndex, bindings, identityOffsets);
}

bool SceneInstance::BindProjected(AZ::u32 tankIndex, const TankEntityBindings &bindings,
                                  const std::array<AZ::Transform, 3> &targetWorldTransforms,
                                  const std::array<AZ::Transform, 2> &pivotWorldTransforms) {
    if (tankIndex >= m_tanks.size() || !bindings.IsComplete()) {
        return false;
    }

    const RuntimeTankInstance &tank = m_tanks[tankIndex];
    EvaluateDirty();
    const AZ::Transform hullWorld = m_runtime.state().world(tank.m_hull).m_value;
    const AZ::Transform turretLocal = hullWorld.GetInverse() * pivotWorldTransforms[0];
    const AZ::Transform gunLocal = pivotWorldTransforms[0].GetInverse() * pivotWorldTransforms[1];
    if (m_runtime.set_local(tank.m_turret, AzTransformValue(turretLocal)) !=
            scene_polytree::transform_error::none ||
        m_runtime.set_local(tank.m_gun, AzTransformValue(gunLocal)) !=
            scene_polytree::transform_error::none) {
        return false;
    }
    EvaluateDirty();
    const std::array nodes{tank.m_hull, tank.m_turret, tank.m_gun};
    std::array<AZ::Transform, 3> nodeToTargets;
    std::ranges::transform(nodes, targetWorldTransforms, nodeToTargets.begin(),
                           [&](wz::core::graph::NodeHandle node, const AZ::Transform &targetWorld) {
                               return m_runtime.state().world(node).m_value.GetInverse() *
                                      targetWorld;
                           });
    return BindWithOffsets(tankIndex, bindings, nodeToTargets);
}

bool SceneInstance::BindWithOffsets(AZ::u32 tankIndex, const TankEntityBindings &bindings,
                                    const std::array<AZ::Transform, 3> &nodeToTargets) {
    if (tankIndex >= m_tanks.size() || !bindings.IsComplete()) {
        return false;
    }

    const RuntimeTankInstance &tank = m_tanks[tankIndex];
    const std::array nodes{tank.m_hull, tank.m_turret, tank.m_gun};
    const std::array entities{bindings.m_hull, bindings.m_turret, bindings.m_gun};
    const bool duplicatesWithinTank =
        entities[0] == entities[1] || entities[0] == entities[2] || entities[1] == entities[2];
    const bool duplicatesAnotherNode = std::ranges::any_of(entities, [&](AZ::EntityId entityId) {
        return std::ranges::any_of(m_entityBindings, [&](const EntityTarget &target) {
            if (target.m_entity != entityId) {
                return false;
            }
            const auto existing = static_cast<std::size_t>(&target - m_entityBindings.data());
            return std::ranges::find(nodes, existing) == nodes.end();
        });
    });
    if (duplicatesWithinTank || duplicatesAnotherNode) {
        return false;
    }

    const auto indices = std::views::iota(std::size_t{}, nodes.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        m_entityBindings[nodes[index]] = {entities[index], nodeToTargets[index]};
        (void)m_runtime.state().mark_dirty(nodes[index]);
    });
    return true;
}

bool SceneInstance::Unbind(AZ::u32 tankIndex) {
    if (tankIndex >= m_tanks.size()) {
        return false;
    }
    const RuntimeTankInstance &tank = m_tanks[tankIndex];
    const std::array nodes{tank.m_hull, tank.m_turret, tank.m_gun};
    std::ranges::for_each(
        nodes, [&](wz::core::graph::NodeHandle node) { m_entityBindings[node] = EntityTarget{}; });
    m_readyTanks[tankIndex] = false;
    m_active = false;
    m_activeMotion.clear();
    m_accumulator = {};
    return true;
}

bool SceneInstance::SetActive(bool active) {
    if (active && (!AllBound() || !AllReady())) {
        return false;
    }
    m_active = active;
    if (!active) {
        m_activeMotion.clear();
        m_accumulator = {};
    }
    return true;
}

bool SceneInstance::SubmitIntent(AZ::u32 tankIndex, const TankIntent &intent) {
    if (tankIndex >= m_tanks.size()) {
        return false;
    }
    using MotionState = scene_polytree::motion::motion_state<AZ::Vector3, AZ::Vector3>;
    using MotionUpdate = scene_polytree::motion::motion_update<AZ::Vector3, AZ::Vector3>;
    const RuntimeTankInstance &tank = m_tanks[tankIndex];
    const std::array updates{
        MotionUpdate{tank.m_hull, MotionState{AZ::Vector3(0.0f, intent.m_forwardSpeed, 0.0f),
                                              AZ::Vector3(0.0f, 0.0f, intent.m_hullYawRate)}},
        MotionUpdate{tank.m_turret, MotionState{AZ::Vector3::CreateZero(),
                                                AZ::Vector3(0.0f, 0.0f, intent.m_turretYawRate)}},
        MotionUpdate{tank.m_gun, MotionState{AZ::Vector3::CreateZero(),
                                             AZ::Vector3(intent.m_gunPitchRate, 0.0f, 0.0f)}},
    };
    return m_activeMotion.apply_updates(updates, m_policy) ==
           scene_polytree::motion::motion_error::none;
}

bool SceneInstance::CorrectLocal(AZ::u32 tankIndex, TankNodeRole role, const AZ::Transform &local) {
    const auto node = NodeForRole(tankIndex, role);
    return node != wz::core::graph::INVALID_NODE &&
           m_runtime.set_local(node, AzTransformValue(local)) ==
               scene_polytree::transform_error::none;
}

bool SceneInstance::CorrectWorld(AZ::u32 tankIndex, TankNodeRole role, const AZ::Transform &world) {
    const auto node = NodeForRole(tankIndex, role);
    if (node == wz::core::graph::INVALID_NODE) {
        return false;
    }
    if (HasDirtyTransforms()) {
        EvaluateDirty();
    }
    const auto parent = wz::core::graph::parent(m_runtime.topology(), node);
    const AZ::Transform local = parent == wz::core::graph::INVALID_NODE
                                    ? world
                                    : m_runtime.state().world(parent).m_value.GetInverse() * world;
    return m_runtime.set_local(node, AzTransformValue(local)) ==
           scene_polytree::transform_error::none;
}

void SceneInstance::Advance(std::chrono::nanoseconds frameDelta, const TransformWriter &writer) {
    m_lastSynchronizedNodeCount = 0;
    if (!m_active) {
        return;
    }

    const auto fixedStep = m_stepSequence.delta();
    const auto maximumBacklog = fixedStep * m_maxCatchUpSteps;
    m_accumulator += std::max(frameDelta, std::chrono::nanoseconds::zero());
    if (m_accumulator > maximumBacklog) {
        AZ_Warning("ScenePolytree", false,
                   "Discarding scene time beyond the configured fixed-step backlog.");
        m_accumulator = maximumBacklog;
    }
    const auto availableSteps = static_cast<AZ::u64>(m_accumulator.count() / fixedStep.count());
    const auto stepCount =
        static_cast<AZ::u32>(std::min<AZ::u64>(availableSteps, m_maxCatchUpSteps));

    const auto steps = std::views::iota(AZ::u32{}, stepCount);
    std::ranges::for_each(steps, [&](AZ::u32) {
        const auto result = scene_polytree::motion::advance_motion_scene(
            m_runtime.topology(), m_runtime.state(), m_activeMotion, m_stepSequence,
            m_motionWorkspace, m_transformWorkspace, m_policy, m_policy);
        AZ_Error("ScenePolytree", result, "Failed to advance a scene-polytree fixed step.");
    });

    m_accumulator -= fixedStep * stepCount;
    if (stepCount == 0 && HasDirtyTransforms()) {
        EvaluateDirty();
    }
    Synchronize(writer);
}

bool SceneInstance::NeedsTick() const {
    return m_active && (!m_activeMotion.empty() || HasDirtyTransforms());
}

SceneStatistics SceneInstance::GetStatistics() const {
    return {
        aznumeric_cast<AZ::u32>(m_tanks.size()),
        aznumeric_cast<AZ::u32>(m_activeMotion.size()),
        m_lastSynchronizedNodeCount,
        m_stepSequence.next_tick(),
        m_active,
    };
}

bool SceneInstance::AllBound() const {
    return std::ranges::all_of(
        m_entityBindings, [](const EntityTarget &target) { return target.m_entity.IsValid(); });
}

bool SceneInstance::AllReady() const {
    return std::ranges::all_of(m_readyTanks, [](bool ready) { return ready; });
}

bool SceneInstance::HasDirtyTransforms() const {
    return std::ranges::any_of(m_runtime.state().records(),
                               [](const auto &record) { return record.dirty; });
}

void SceneInstance::EvaluateDirty() {
    auto plan = scene_polytree::make_transform_evaluation_plan(
        m_runtime.topology(), m_runtime.state(), m_transformWorkspace);
    if (!plan) {
        AZ_Error("ScenePolytree", false, "Failed to plan scene transform evaluation.");
        return;
    }
    const auto result = scene_polytree::evaluate_transforms(m_runtime.topology(), m_runtime.state(),
                                                            plan.value(), m_policy);
    AZ_Error("ScenePolytree", result, "Failed to evaluate scene transforms.");
}

void SceneInstance::Synchronize(const TransformWriter &writer) {
    const auto changed = scene_polytree::changed_transform_nodes_since(
        m_runtime.topology(), m_runtime.state(), m_lastSynchronizedRevision, m_changedScratch);
    std::ranges::for_each(changed, [&](wz::core::graph::NodeHandle node) {
        const EntityTarget &target = m_entityBindings[node];
        if (target.m_entity.IsValid()) {
            writer(target.m_entity, m_runtime.state().world(node).m_value * target.m_nodeToTarget);
            ++m_lastSynchronizedNodeCount;
        }
    });
    m_lastSynchronizedRevision = m_runtime.state().revision();
}

wz::core::graph::NodeHandle SceneInstance::NodeForRole(AZ::u32 tankIndex, TankNodeRole role) const {
    if (tankIndex >= m_tanks.size()) {
        return wz::core::graph::INVALID_NODE;
    }
    const RuntimeTankInstance &tank = m_tanks[tankIndex];
    switch (role) {
    case TankNodeRole::Hull:
        return tank.m_hull;
    case TankNodeRole::Turret:
        return tank.m_turret;
    case TankNodeRole::Gun:
        return tank.m_gun;
    }
    return wz::core::graph::INVALID_NODE;
}
} // namespace ScenePolytree::Internal
