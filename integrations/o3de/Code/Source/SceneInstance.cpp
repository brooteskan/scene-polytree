#include "SceneInstance.h"

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/Debug/Trace.h>

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>

namespace ScenePolytree::Internal {
namespace {
[[nodiscard]] StableTankInstance InstantiateTank(SceneInstance::AuthoringScene &scene,
                                                 const AZ::Transform &spawnTransform) {
    const auto hull =
        scene.insert_root(ScenePolytreeNodeType::Transform, AzTransformValue(spawnTransform))
            .value();
    const auto turret =
        scene
            .insert_child(hull, ScenePolytreeNodeType::Transform, ScenePolytreeJointType::Yaw,
                          AzTransformValue(AZ::Transform::CreateIdentity()))
            .value();
    const auto gun =
        scene
            .insert_child(turret, ScenePolytreeNodeType::Transform, ScenePolytreeJointType::Pitch,
                          AzTransformValue(AZ::Transform::CreateIdentity()))
            .value();
    return {hull, turret, gun};
}

[[nodiscard]] RuntimeTankInstance ResolveLegacyTank(const SceneInstance::RuntimeScene &runtime,
                                                    const StableTankInstance &stable) {
    return {
        runtime.identities().runtime_handle(stable.m_hull).value(),
        runtime.identities().runtime_handle(stable.m_turret).value(),
        runtime.identities().runtime_handle(stable.m_gun).value(),
    };
}

[[nodiscard]] std::optional<StableSlot>
InstantiateTopology(SceneInstance::AuthoringScene &scene,
                    const AZStd::vector<ScenePolytreeNodeDescriptor> &nodes) {
    StableSlot slot;
    slot.m_nodes.reserve(nodes.size());
    bool valid = true;
    std::ranges::for_each(nodes, [&](const ScenePolytreeNodeDescriptor &node) {
        if (!valid) {
            return;
        }
        const auto parent = std::ranges::find(slot.m_nodes, node.m_parentBindingId,
                                              &StableNodeBinding::m_bindingId);
        const auto inserted =
            node.m_parentBindingId.IsEmpty()
                ? scene.insert_root(node.m_nodeType, AzTransformValue(node.m_initialLocal))
            : parent != slot.m_nodes.end()
                ? scene.insert_child(parent->m_node, node.m_nodeType, node.m_jointType,
                                     AzTransformValue(node.m_initialLocal))
                : wz::core::graph::MutationResult<wz::core::graph::StableNodeId>::failure(
                      wz::core::graph::MutationError::invalid_parent);
        valid = static_cast<bool>(inserted);
        if (inserted) {
            slot.m_nodes.push_back({node.m_bindingId, inserted.value(), node.m_initialLocal});
        }
    });
    return valid ? std::optional<StableSlot>(AZStd::move(slot)) : std::nullopt;
}

[[nodiscard]] RuntimeNodeBinding ResolveNodeBinding(const SceneInstance::RuntimeScene &runtime,
                                                    const StableNodeBinding &stable) {
    return {stable.m_bindingId, runtime.identities().runtime_handle(stable.m_node).value(),
            stable.m_initialLocal};
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
        [&](const StableTankInstance &tank) { return ResolveLegacyTank(runtime, tank); });

    return std::unique_ptr<SceneInstance>(
        new SceneInstance(std::move(runtime), std::move(runtimeTanks), {},
                          std::chrono::nanoseconds(descriptor.m_fixedStepNanoseconds),
                          descriptor.m_maxCatchUpSteps, true));
}

std::unique_ptr<SceneInstance>
SceneInstance::Create(const ScenePolytreeSceneDescriptor &descriptor) {
    if (descriptor.m_fixedStepNanoseconds <= 0 || descriptor.m_maxCatchUpSteps == 0 ||
        (descriptor.m_permanentNodes.empty() && descriptor.m_partitions.empty()) ||
        std::ranges::any_of(descriptor.m_partitions, [](const auto &partition) {
            return partition.m_partition == 0 || partition.m_capacity == 0 ||
                   partition.m_nodes.empty();
        })) {
        return {};
    }

    AuthoringScene authoring;
    if (!descriptor.m_permanentNodes.empty() &&
        !InstantiateTopology(authoring, descriptor.m_permanentNodes)) {
        return {};
    }

    AZStd::vector<StablePartition> stablePartitions;
    stablePartitions.reserve(descriptor.m_partitions.size());
    bool valid = true;
    std::ranges::for_each(descriptor.m_partitions, [&](const auto &partition) {
        StablePartition stablePartition;
        stablePartition.m_partition = partition.m_partition;
        stablePartition.m_slots.reserve(partition.m_capacity);
        const auto slots = std::views::iota(AZ::u32{}, partition.m_capacity);
        std::ranges::for_each(slots, [&](AZ::u32) {
            if (valid) {
                auto slot = InstantiateTopology(authoring, partition.m_nodes);
                valid = slot.has_value();
                if (slot) {
                    stablePartition.m_slots.push_back(AZStd::move(*slot));
                }
            }
        });
        stablePartitions.push_back(AZStd::move(stablePartition));
    });
    if (!valid) {
        return {};
    }

    wz::core::graph::FreezeWorkspace freezeWorkspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freezeWorkspace);
    if (!frozen) {
        return {};
    }

    RuntimeScene runtime = std::move(frozen).value();
    std::vector<RuntimeTankInstance> runtimeTanks;
    AZStd::vector<RuntimePartition> runtimePartitions;
    runtimePartitions.reserve(stablePartitions.size());
    std::ranges::for_each(stablePartitions, [&](const StablePartition &stablePartition) {
        RuntimePartition runtimePartition;
        runtimePartition.m_partition = stablePartition.m_partition;
        runtimePartition.m_slots.reserve(stablePartition.m_slots.size());
        std::ranges::for_each(stablePartition.m_slots, [&](const StableSlot &stableSlot) {
            RuntimeSlot runtimeSlot;
            runtimeSlot.m_nodes.reserve(stableSlot.m_nodes.size());
            std::ranges::transform(stableSlot.m_nodes, std::back_inserter(runtimeSlot.m_nodes),
                                   [&](const StableNodeBinding &binding) {
                                       return ResolveNodeBinding(runtime, binding);
                                   });
            const auto hull = std::ranges::find(runtimeSlot.m_nodes, AZ::Name("Hull"),
                                                &RuntimeNodeBinding::m_bindingId);
            const auto turret = std::ranges::find(runtimeSlot.m_nodes, AZ::Name("Turret"),
                                                  &RuntimeNodeBinding::m_bindingId);
            const auto gun = std::ranges::find(runtimeSlot.m_nodes, AZ::Name("Gun"),
                                               &RuntimeNodeBinding::m_bindingId);
            if (hull != runtimeSlot.m_nodes.end() && turret != runtimeSlot.m_nodes.end() &&
                gun != runtimeSlot.m_nodes.end()) {
                runtimeSlot.m_tankIndex = aznumeric_cast<AZ::u32>(runtimeTanks.size());
                runtimeTanks.push_back({hull->m_node, turret->m_node, gun->m_node});
            }
            runtimePartition.m_slots.push_back(AZStd::move(runtimeSlot));
        });
        runtimePartitions.push_back(AZStd::move(runtimePartition));
    });

    return std::unique_ptr<SceneInstance>(new SceneInstance(
        std::move(runtime), std::move(runtimeTanks), AZStd::move(runtimePartitions),
        std::chrono::nanoseconds(descriptor.m_fixedStepNanoseconds), descriptor.m_maxCatchUpSteps,
        false));
}

SceneInstance::SceneInstance(RuntimeScene runtime, std::vector<RuntimeTankInstance> tanks,
                             AZStd::vector<RuntimePartition> partitions,
                             std::chrono::nanoseconds fixedStep, AZ::u32 maxCatchUpSteps,
                             bool requireAllTanksReady)
    : m_runtime(std::move(runtime)), m_tanks(std::move(tanks)),
      m_partitions(AZStd::move(partitions)), m_activeMotion(m_runtime.topology()),
      m_entityBindings(m_runtime.state().size()), m_readyTanks(m_tanks.size(), false),
      m_stepSequence(fixedStep), m_evaluatedStamp(m_runtime.state().size()),
      m_topologicalRank(m_runtime.state().size()), m_maxCatchUpSteps(maxCatchUpSteps),
      m_requireAllTanksReady(requireAllTanksReady) {
    m_evaluatedChanges.reserve(m_runtime.state().size());
    const auto order = wz::core::graph::evaluation_plan(m_runtime.topology()).topological_order;
    std::ranges::for_each(std::views::iota(std::size_t{}, order.size()), [&](std::size_t index) {
        m_topologicalRank[order[index]] = static_cast<std::uint32_t>(index);
    });
}

SlotResult SceneInstance::ReserveSlot(SpawnerHandle spawner) {
    RuntimePartition *partition = FindPartition(spawner);
    if (partition == nullptr) {
        return {ScenePolytreeResultCode::PartitionMismatch, {}};
    }
    const auto available = std::ranges::find_if(
        partition->m_slots, [](const RuntimeSlot &slot) { return !slot.m_reserved; });
    if (available == partition->m_slots.end()) {
        return {ScenePolytreeResultCode::SlotUnavailable, {}};
    }
    available->m_reserved = true;
    const auto slotIndex = aznumeric_cast<AZ::u32>(available - partition->m_slots.begin());
    return {ScenePolytreeResultCode::Success,
            SlotHandle{spawner, slotIndex, available->m_generation}};
}

ScenePolytreeResultCode SceneInstance::PlaceSlot(SlotHandle slot, const AZ::Transform &rootWorld) {
    RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    if (!rootWorld.IsFinite()) {
        return ScenePolytreeResultCode::InvalidTransform;
    }
    bool applied = true;
    std::ranges::for_each(runtimeSlot->m_nodes, [&](const RuntimeNodeBinding &node) {
        if (wz::core::graph::parent(m_runtime.topology(), node.m_node) ==
            wz::core::graph::INVALID_NODE) {
            applied =
                applied && m_runtime.set_local(node.m_node,
                                               AzTransformValue(rootWorld * node.m_initialLocal)) ==
                               scene_polytree::transform_error::none;
        }
    });
    return applied ? ScenePolytreeResultCode::Success : ScenePolytreeResultCode::InvalidTransform;
}

ScenePolytreeResultCode
SceneInstance::BindSlot(SlotHandle slot,
                        const AZStd::vector<ScenePolytreeEntityBinding> &bindings) {
    RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    if (bindings.empty() || std::ranges::any_of(bindings, [](const auto &binding) {
            return binding.m_bindingId.IsEmpty() || !binding.m_entity.IsValid() ||
                   !binding.m_nodeToEntity.IsFinite();
        })) {
        return ScenePolytreeResultCode::InvalidBinding;
    }
    const auto duplicateBinding = std::ranges::find_if(bindings, [&](const auto &binding) {
        return std::ranges::count(bindings, binding.m_bindingId,
                                  &ScenePolytreeEntityBinding::m_bindingId) != 1 ||
               std::ranges::count(bindings, binding.m_entity,
                                  &ScenePolytreeEntityBinding::m_entity) != 1;
    });
    if (duplicateBinding != bindings.end()) {
        return ScenePolytreeResultCode::InvalidBinding;
    }

    const auto missing = std::ranges::find_if(bindings, [&](const auto &binding) {
        return std::ranges::find(runtimeSlot->m_nodes, binding.m_bindingId,
                                 &RuntimeNodeBinding::m_bindingId) == runtimeSlot->m_nodes.end();
    });
    if (missing != bindings.end()) {
        return ScenePolytreeResultCode::PartitionMismatch;
    }

    const auto aliasesAnotherNode = std::ranges::find_if(bindings, [&](const auto &binding) {
        const auto existing =
            std::ranges::find(m_entityBindings, binding.m_entity, &EntityTarget::m_entity);
        if (existing == m_entityBindings.end()) {
            return false;
        }
        const auto existingNode =
            static_cast<wz::core::graph::NodeHandle>(existing - m_entityBindings.begin());
        return std::ranges::find(runtimeSlot->m_nodes, existingNode, &RuntimeNodeBinding::m_node) ==
               runtimeSlot->m_nodes.end();
    });
    if (aliasesAnotherNode != bindings.end()) {
        return ScenePolytreeResultCode::InvalidBinding;
    }

    std::ranges::for_each(bindings, [&](const ScenePolytreeEntityBinding &binding) {
        const auto node = std::ranges::find(runtimeSlot->m_nodes, binding.m_bindingId,
                                            &RuntimeNodeBinding::m_bindingId);
        m_entityBindings[node->m_node] = {binding.m_entity, binding.m_nodeToEntity};
        (void)m_runtime.state().mark_dirty(node->m_node);
    });
    m_active = true;
    return ScenePolytreeResultCode::Success;
}

ScenePolytreeResultCode SceneInstance::UnbindSlot(SlotHandle slot) {
    RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    std::ranges::for_each(runtimeSlot->m_nodes, [&](const RuntimeNodeBinding &node) {
        m_entityBindings[node.m_node] = EntityTarget{};
    });
    if (runtimeSlot->m_tankIndex < m_readyTanks.size()) {
        m_readyTanks[runtimeSlot->m_tankIndex] = false;
    }
    return ScenePolytreeResultCode::Success;
}

ScenePolytreeResultCode SceneInstance::ResetSlot(SlotHandle slot) {
    RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    ClearSlot(*runtimeSlot);
    return ScenePolytreeResultCode::Success;
}

ScenePolytreeResultCode SceneInstance::ReleaseSlot(SlotHandle slot) {
    RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    ClearSlot(*runtimeSlot);
    runtimeSlot->m_reserved = false;
    if (++runtimeSlot->m_generation == 0) {
        ++runtimeSlot->m_generation;
    }
    return ScenePolytreeResultCode::Success;
}

NodeResult SceneInstance::ResolveNode(SlotHandle slot, const AZ::Name &bindingId) const {
    const RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return {ScenePolytreeResultCode::StaleHandle, {}};
    }
    const auto found =
        std::ranges::find(runtimeSlot->m_nodes, bindingId, &RuntimeNodeBinding::m_bindingId);
    return found == runtimeSlot->m_nodes.end()
               ? NodeResult{ScenePolytreeResultCode::InvalidBinding, {}}
               : NodeResult{ScenePolytreeResultCode::Success,
                            SceneNodeHandle{slot, found->m_bindingId}};
}

TankHandle SceneInstance::ResolveTank(SlotHandle slot) const {
    const RuntimeSlot *runtimeSlot = FindSlot(slot);
    return runtimeSlot != nullptr && runtimeSlot->m_tankIndex < m_tanks.size()
               ? TankHandle{slot.m_spawner.m_scene, runtimeSlot->m_tankIndex}
               : TankHandle{};
}

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
    std::ranges::for_each(nodes, [&](wz::core::graph::NodeHandle node) {
        m_entityBindings[node] = EntityTarget{};
        (void)m_activeMotion.deactivate(node);
    });
    m_readyTanks[tankIndex] = false;
    if (m_requireAllTanksReady) {
        m_active = false;
        m_activeMotion.clear();
        m_accumulator = {};
    }
    return true;
}

bool SceneInstance::SetActive(bool active) {
    if (active && (m_requireAllTanksReady ? (!AllBound() || !AllReady()) : !AnyReadyAndBound())) {
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
        if (result) {
            QueueEvaluated(result.changed_nodes);
        } else {
            m_directBatchValid = false;
        }
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
    AZ::u32 capacity{};
    AZ::u32 reserved{};
    AZ::u32 bound{};
    std::ranges::for_each(m_partitions, [&](const RuntimePartition &partition) {
        capacity += aznumeric_cast<AZ::u32>(partition.m_slots.size());
        reserved += aznumeric_cast<AZ::u32>(
            std::ranges::count(partition.m_slots, true, &RuntimeSlot::m_reserved));
        bound += aznumeric_cast<AZ::u32>(
            std::ranges::count_if(partition.m_slots, [&](const RuntimeSlot &slot) {
                return std::ranges::any_of(slot.m_nodes, [&](const auto &node) {
                    return m_entityBindings[node.m_node].m_entity.IsValid();
                });
            }));
    });
    return {
        aznumeric_cast<AZ::u32>(m_tanks.size()),
        aznumeric_cast<AZ::u32>(m_activeMotion.size()),
        m_lastSynchronizedNodeCount,
        m_stepSequence.next_tick(),
        m_active,
        aznumeric_cast<AZ::u32>(m_partitions.size()),
        capacity,
        reserved,
        bound,
    };
}

bool SceneInstance::AllBound() const {
    return std::ranges::all_of(
        m_entityBindings, [](const EntityTarget &target) { return target.m_entity.IsValid(); });
}

bool SceneInstance::AllReady() const {
    return std::ranges::all_of(m_readyTanks, [](bool ready) { return ready; });
}

bool SceneInstance::AnyReadyAndBound() const {
    const auto indices = std::views::iota(std::size_t{}, m_tanks.size());
    return std::ranges::any_of(indices, [&](std::size_t index) {
        const RuntimeTankInstance &tank = m_tanks[index];
        const std::array nodes{tank.m_hull, tank.m_turret, tank.m_gun};
        return m_readyTanks[index] && std::ranges::all_of(nodes, [&](const auto node) {
                   return m_entityBindings[node].m_entity.IsValid();
               });
    });
}

bool SceneInstance::HasDirtyTransforms() const { return m_runtime.state().has_dirty_transforms(); }

void SceneInstance::EvaluateDirty() {
    auto plan = scene_polytree::make_transform_evaluation_plan(
        m_runtime.topology(), m_runtime.state(), m_transformWorkspace);
    if (!plan) {
        m_directBatchValid = false;
        AZ_Error("ScenePolytree", false, "Failed to plan scene transform evaluation.");
        return;
    }
    const auto result = scene_polytree::evaluate_transforms(m_runtime.topology(), m_runtime.state(),
                                                            plan.value(), m_policy);
    AZ_Error("ScenePolytree", result, "Failed to evaluate scene transforms.");
    if (result) {
        QueueEvaluated(result.changed_nodes);
    } else {
        m_directBatchValid = false;
    }
}

void SceneInstance::QueueEvaluated(std::span<const wz::core::graph::NodeHandle> changed) {
    std::ranges::for_each(changed, [&](wz::core::graph::NodeHandle node) {
        if (m_evaluatedStamp[node] != m_evaluatedEpoch) {
            m_evaluatedStamp[node] = m_evaluatedEpoch;
            m_evaluatedChanges.push_back(node);
        }
    });
}

void SceneInstance::ResetEvaluatedBatch() {
    m_evaluatedChanges.clear();
    if (++m_evaluatedEpoch == 0) {
        std::ranges::fill(m_evaluatedStamp, std::uint64_t{});
        ++m_evaluatedEpoch;
    }
    m_directBatchValid = true;
}

void SceneInstance::Synchronize(const TransformWriter &writer) {
    std::span<const wz::core::graph::NodeHandle> changed;
    if (m_directBatchValid) {
        std::ranges::sort(m_evaluatedChanges, {}, [&](wz::core::graph::NodeHandle node) {
            return m_topologicalRank[node];
        });
        changed = m_evaluatedChanges;
    } else {
        changed = scene_polytree::changed_transform_nodes_since(
            m_runtime.topology(), m_runtime.state(), m_lastSynchronizedRevision, m_changedScratch);
    }
    std::ranges::for_each(changed, [&](wz::core::graph::NodeHandle node) {
        const EntityTarget &target = m_entityBindings[node];
        if (target.m_entity.IsValid()) {
            writer(target.m_entity, m_runtime.state().world(node).m_value * target.m_nodeToTarget);
            ++m_lastSynchronizedNodeCount;
        }
    });
    m_lastSynchronizedRevision = m_runtime.state().revision();
    ResetEvaluatedBatch();
}

RuntimePartition *SceneInstance::FindPartition(const SpawnerHandle &spawner) {
    const auto found =
        std::ranges::find(m_partitions, spawner.m_partition, &RuntimePartition::m_partition);
    return spawner.IsValid() && spawner.m_generation == 1 && found != m_partitions.end() ? &*found
                                                                                         : nullptr;
}

const RuntimePartition *SceneInstance::FindPartition(const SpawnerHandle &spawner) const {
    const auto found =
        std::ranges::find(m_partitions, spawner.m_partition, &RuntimePartition::m_partition);
    return spawner.IsValid() && spawner.m_generation == 1 && found != m_partitions.end() ? &*found
                                                                                         : nullptr;
}

RuntimeSlot *SceneInstance::FindSlot(const SlotHandle &slot) {
    RuntimePartition *partition = FindPartition(slot.m_spawner);
    if (partition == nullptr || slot.m_slot >= partition->m_slots.size()) {
        return nullptr;
    }
    RuntimeSlot &runtimeSlot = partition->m_slots[slot.m_slot];
    return runtimeSlot.m_reserved && runtimeSlot.m_generation == slot.m_generation ? &runtimeSlot
                                                                                   : nullptr;
}

const RuntimeSlot *SceneInstance::FindSlot(const SlotHandle &slot) const {
    const RuntimePartition *partition = FindPartition(slot.m_spawner);
    if (partition == nullptr || slot.m_slot >= partition->m_slots.size()) {
        return nullptr;
    }
    const RuntimeSlot &runtimeSlot = partition->m_slots[slot.m_slot];
    return runtimeSlot.m_reserved && runtimeSlot.m_generation == slot.m_generation ? &runtimeSlot
                                                                                   : nullptr;
}

void SceneInstance::ClearSlot(RuntimeSlot &slot) {
    std::ranges::for_each(slot.m_nodes, [&](const RuntimeNodeBinding &node) {
        m_entityBindings[node.m_node] = EntityTarget{};
        (void)m_activeMotion.deactivate(node.m_node);
        (void)m_runtime.set_local(node.m_node, AzTransformValue(node.m_initialLocal));
    });
    if (slot.m_tankIndex < m_readyTanks.size()) {
        m_readyTanks[slot.m_tankIndex] = false;
    }
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
