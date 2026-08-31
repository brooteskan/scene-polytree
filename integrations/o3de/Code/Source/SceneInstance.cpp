#include "SceneInstance.h"

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/Debug/Trace.h>

#include <algorithm>
#include <optional>
#include <ranges>

namespace ScenePolytree::Internal {
namespace {
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
            runtimePartition.m_slots.push_back(AZStd::move(runtimeSlot));
        });
        runtimePartitions.push_back(AZStd::move(runtimePartition));
    });

    return std::unique_ptr<SceneInstance>(new SceneInstance(
        std::move(runtime), AZStd::move(runtimePartitions),
        std::chrono::nanoseconds(descriptor.m_fixedStepNanoseconds), descriptor.m_maxCatchUpSteps));
}

SceneInstance::SceneInstance(RuntimeScene runtime, AZStd::vector<RuntimePartition> partitions,
                             std::chrono::nanoseconds fixedStep, AZ::u32 maxCatchUpSteps)
    : m_runtime(std::move(runtime)), m_partitions(AZStd::move(partitions)),
      m_activeMotion(m_runtime.topology()), m_entityBindings(m_runtime.state().size()),
      m_stepSequence(fixedStep), m_evaluatedStamp(m_runtime.state().size()),
      m_topologicalRank(m_runtime.state().size()), m_maxCatchUpSteps(maxCatchUpSteps) {
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

bool SceneInstance::SetActive(bool active) {
    m_active = active;
    if (!active) {
        m_activeMotion.clear();
        m_accumulator = {};
    }
    return true;
}

bool SceneInstance::CorrectLocal(const SceneNodeHandle &node, const AZ::Transform &local) {
    const RuntimeNodeBinding *binding = FindNode(node);
    return binding != nullptr && m_runtime.set_local(binding->m_node, AzTransformValue(local)) ==
                                     scene_polytree::transform_error::none;
}

bool SceneInstance::CorrectWorld(const SceneNodeHandle &node, const AZ::Transform &world) {
    const RuntimeNodeBinding *binding = FindNode(node);
    if (binding == nullptr) {
        return false;
    }
    if (HasDirtyTransforms()) {
        EvaluateDirty();
    }
    const auto parent = wz::core::graph::parent(m_runtime.topology(), binding->m_node);
    const AZ::Transform local = parent == wz::core::graph::INVALID_NODE
                                    ? world
                                    : m_runtime.state().world(parent).m_value.GetInverse() * world;
    return m_runtime.set_local(binding->m_node, AzTransformValue(local)) ==
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
        aznumeric_cast<AZ::u32>(m_runtime.state().size()),
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

RuntimeNodeBinding *SceneInstance::FindNode(const SceneNodeHandle &node) {
    RuntimeSlot *slot = FindSlot(node.m_slot);
    if (slot == nullptr || node.m_bindingId.IsEmpty()) {
        return nullptr;
    }
    const auto found =
        std::ranges::find(slot->m_nodes, node.m_bindingId, &RuntimeNodeBinding::m_bindingId);
    return found != slot->m_nodes.end() ? &*found : nullptr;
}

const RuntimeNodeBinding *SceneInstance::FindNode(const SceneNodeHandle &node) const {
    const RuntimeSlot *slot = FindSlot(node.m_slot);
    if (slot == nullptr || node.m_bindingId.IsEmpty()) {
        return nullptr;
    }
    const auto found =
        std::ranges::find(slot->m_nodes, node.m_bindingId, &RuntimeNodeBinding::m_bindingId);
    return found != slot->m_nodes.end() ? &*found : nullptr;
}

void SceneInstance::ClearSlot(RuntimeSlot &slot) {
    std::ranges::for_each(slot.m_nodes, [&](const RuntimeNodeBinding &node) {
        m_entityBindings[node.m_node] = EntityTarget{};
        (void)m_activeMotion.deactivate(node.m_node);
        (void)m_runtime.set_local(node.m_node, AzTransformValue(node.m_initialLocal));
    });
}
} // namespace ScenePolytree::Internal
