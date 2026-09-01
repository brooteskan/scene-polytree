#include "SceneInstance.h"

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/std/string/string.h>

#include <algorithm>
#include <optional>
#include <ranges>

namespace ScenePolytree {
namespace Internal {
class ControllerCommandSinkAccess final {
  public:
    static void Bind(ScenePolytreeControllerCommandSink &sink, SceneInstance &runtime,
                     AZ::u64 epoch) {
        sink.m_runtime = &runtime;
        sink.m_epoch = epoch;
    }

    static void Clear(ScenePolytreeControllerCommandSink &sink) {
        sink.m_runtime = nullptr;
        sink.m_epoch = 0;
    }

    [[nodiscard]] static ScenePolytreeControllerResultCode
    SetLocal(ScenePolytreeControllerCommandSink &sink, ScenePolytreeControllerTargetToken target,
             const AZ::Transform &local) {
        auto *runtime = static_cast<SceneInstance *>(sink.m_runtime);
        return runtime != nullptr ? runtime->SetControllerLocal(target, sink.m_epoch, local)
                                  : ScenePolytreeControllerResultCode::StaleHandle;
    }

    [[nodiscard]] static ScenePolytreeControllerResultCode
    SetVelocity(ScenePolytreeControllerCommandSink &sink, ScenePolytreeControllerTargetToken target,
                const AZ::Vector3 &linear, const AZ::Vector3 &angular) {
        auto *runtime = static_cast<SceneInstance *>(sink.m_runtime);
        return runtime != nullptr
                   ? runtime->SetControllerVelocity(target, sink.m_epoch, linear, angular)
                   : ScenePolytreeControllerResultCode::StaleHandle;
    }

    [[nodiscard]] static ScenePolytreeControllerResultCode
    GetLocal(const ScenePolytreeControllerCommandSink &sink,
             ScenePolytreeControllerTargetToken target, AZ::Transform &local) {
        const auto *runtime = static_cast<const SceneInstance *>(sink.m_runtime);
        return runtime != nullptr ? runtime->GetControllerLocal(target, sink.m_epoch, local)
                                  : ScenePolytreeControllerResultCode::StaleHandle;
    }
};
} // namespace Internal

ScenePolytreeControllerResultCode
ScenePolytreeControllerCommandSink::SetLocalTransform(ScenePolytreeControllerTargetToken target,
                                                      const AZ::Transform &local) {
    return Internal::ControllerCommandSinkAccess::SetLocal(*this, target, local);
}

ScenePolytreeControllerResultCode
ScenePolytreeControllerCommandSink::SetVelocity(ScenePolytreeControllerTargetToken target,
                                                const AZ::Vector3 &linear,
                                                const AZ::Vector3 &angular) {
    return Internal::ControllerCommandSinkAccess::SetVelocity(*this, target, linear, angular);
}

ScenePolytreeControllerResultCode
ScenePolytreeControllerCommandSink::StopMotion(ScenePolytreeControllerTargetToken target) {
    return Internal::ControllerCommandSinkAccess::SetVelocity(
        *this, target, AZ::Vector3::CreateZero(), AZ::Vector3::CreateZero());
}

ScenePolytreeControllerResultCode
ScenePolytreeControllerCommandSink::GetLocalTransform(ScenePolytreeControllerTargetToken target,
                                                      AZ::Transform &local) const {
    return Internal::ControllerCommandSinkAccess::GetLocal(*this, target, local);
}
} // namespace ScenePolytree

namespace ScenePolytree::Internal {
namespace {
[[nodiscard]] ScenePolytreeResultCode MapControllerResult(ScenePolytreeControllerResultCode code) {
    switch (code) {
    case ScenePolytreeControllerResultCode::Success:
        return ScenePolytreeResultCode::Success;
    case ScenePolytreeControllerResultCode::FactoryNotFound:
        return ScenePolytreeResultCode::ControllerFactoryNotFound;
    case ScenePolytreeControllerResultCode::InvalidConfiguration:
        return ScenePolytreeResultCode::ControllerInvalidConfiguration;
    case ScenePolytreeControllerResultCode::InvalidTarget:
        return ScenePolytreeResultCode::ControllerTargetNotFound;
    case ScenePolytreeControllerResultCode::ConstructionFailed:
        return ScenePolytreeResultCode::ControllerConstructionFailed;
    default:
        return ScenePolytreeResultCode::ControllerConstructionFailed;
    }
}

[[nodiscard]] bool TypeIdLess(ScenePolytreeControllerTypeId left,
                              ScenePolytreeControllerTypeId right) {
    return left.ToString<AZStd::string>() < right.ToString<AZStd::string>();
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
                ? scene.insert_root(TransformNodePayload::Transform,
                                    AzTransformValue(node.m_initialLocal))
            : parent != slot.m_nodes.end()
                ? scene.insert_child(parent->m_node, TransformNodePayload::Transform,
                                     TransformEdgePayload::Parent,
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
      m_controllerWriteOwners(m_runtime.state().size()), m_stepSequence(fixedStep),
      m_evaluatedStamp(m_runtime.state().size()), m_topologicalRank(m_runtime.state().size()),
      m_maxCatchUpSteps(maxCatchUpSteps) {
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
    if (bindings.size() != runtimeSlot->m_nodes.size() ||
        std::ranges::any_of(bindings, [](const auto &binding) {
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
        m_entityToNode.emplace(binding.m_entity, node->m_node);
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
    if (HasControllers(slot)) {
        return ScenePolytreeResultCode::ControllersActive;
    }
    std::ranges::for_each(runtimeSlot->m_nodes, [&](const RuntimeNodeBinding &node) {
        m_entityToNode.erase(m_entityBindings[node.m_node].m_entity);
        m_entityBindings[node.m_node] = EntityTarget{};
    });
    return ScenePolytreeResultCode::Success;
}

ScenePolytreeResultCode SceneInstance::ResetSlot(SlotHandle slot) {
    RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    if (HasControllers(slot)) {
        return ScenePolytreeResultCode::ControllersActive;
    }
    ClearSlot(*runtimeSlot);
    return ScenePolytreeResultCode::Success;
}

ScenePolytreeResultCode SceneInstance::ReleaseSlot(SlotHandle slot) {
    RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    if (HasControllers(slot)) {
        return ScenePolytreeResultCode::ControllersActive;
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

ScenePolytreeResultCode
SceneInstance::AttachControllers(InstanceHandle instance,
                                 AZStd::vector<ScenePolytreeControllerDeclaration> declarations,
                                 const ControllerFactoryResolver &resolveFactory) {
    if (!instance.IsValid() || FindSlot(instance.m_slot) == nullptr) {
        return ScenePolytreeResultCode::StaleHandle;
    }
    if (std::ranges::any_of(m_controllers, [&](const ControllerRecord &record) {
            return record.m_handle.m_instance == instance;
        })) {
        return ScenePolytreeResultCode::ControllersActive;
    }
    if (declarations.empty()) {
        return ScenePolytreeResultCode::Success;
    }
    if (!resolveFactory) {
        return ScenePolytreeResultCode::ControllerFactoryNotFound;
    }

    std::ranges::sort(declarations, [](const auto &left, const auto &right) {
        if (left.m_executionOrder != right.m_executionOrder) {
            return left.m_executionOrder < right.m_executionOrder;
        }
        if (left.m_providerBindingId != right.m_providerBindingId) {
            return left.m_providerBindingId.GetStringView() <
                   right.m_providerBindingId.GetStringView();
        }
        if (left.m_declarationId != right.m_declarationId) {
            return left.m_declarationId.GetStringView() < right.m_declarationId.GetStringView();
        }
        return TypeIdLess(left.m_typeId, right.m_typeId);
    });

    struct PreparedTarget {
        ScenePolytreeControllerTargetReference m_reference;
        SceneNodeHandle m_node;
        wz::core::graph::NodeHandle m_runtimeNode{wz::core::graph::INVALID_NODE};
    };
    struct PreparedController {
        std::size_t m_declaration{};
        AZStd::shared_ptr<const ScenePolytreeControllerFactory> m_factory;
        AZStd::vector<PreparedTarget> m_targets;
    };

    AZStd::vector<PreparedController> prepared;
    prepared.reserve(declarations.size());
    AZStd::vector<AZ::u32> pendingWriteOwners(m_controllerWriteOwners.size());
    ScenePolytreeResultCode failure = ScenePolytreeResultCode::Success;
    std::ranges::for_each(
        std::views::iota(std::size_t{}, declarations.size()), [&](std::size_t declarationIndex) {
            if (failure != ScenePolytreeResultCode::Success) {
                return;
            }
            auto &declaration = declarations[declarationIndex];
            const bool duplicateDeclaration =
                std::ranges::count(declarations, declaration.m_declarationId,
                                   &ScenePolytreeControllerDeclaration::m_declarationId) != 1;
            if (declaration.m_declarationId.IsEmpty() || declaration.m_typeId.IsNull() ||
                declaration.m_configuration == nullptr || declaration.m_targets.empty() ||
                duplicateDeclaration) {
                failure = ScenePolytreeResultCode::ControllerInvalidConfiguration;
                return;
            }
            auto factory = resolveFactory(declaration.m_typeId);
            if (factory == nullptr || factory->GetControllerTypeId() != declaration.m_typeId) {
                failure = ScenePolytreeResultCode::ControllerFactoryNotFound;
                return;
            }

            PreparedController controller{declarationIndex, AZStd::move(factory), {}};
            controller.m_targets.reserve(declaration.m_targets.size());
            std::ranges::for_each(declaration.m_targets, [&](const auto &target) {
                if (failure != ScenePolytreeResultCode::Success) {
                    return;
                }
                const bool usesEntity = target.m_prefabEntity.IsValid();
                const bool usesBinding = !target.m_bindingId.IsEmpty();
                const bool duplicateSlot =
                    std::ranges::count(declaration.m_targets, target.m_slot,
                                       &ScenePolytreeControllerTargetReference::m_slot) != 1;
                if (target.m_slot.IsEmpty() || usesEntity == usesBinding || duplicateSlot ||
                    (target.m_access != ScenePolytreeControllerTargetAccess::ReadOnly &&
                     target.m_access != ScenePolytreeControllerTargetAccess::ReadWrite)) {
                    failure = ScenePolytreeResultCode::ControllerInvalidConfiguration;
                    return;
                }
                const NodeResult resolved =
                    usesEntity ? ResolveEntityNode(instance.m_slot, target.m_prefabEntity)
                               : ResolveNode(instance.m_slot, target.m_bindingId);
                if (!resolved.IsSuccess()) {
                    failure = resolved.m_code == ScenePolytreeResultCode::PartitionMismatch
                                  ? ScenePolytreeResultCode::ControllerTargetOutsideInstance
                                  : ScenePolytreeResultCode::ControllerTargetNotFound;
                    return;
                }
                const RuntimeNodeBinding *node = FindNode(resolved.m_handle);
                if (node == nullptr ||
                    std::ranges::any_of(controller.m_targets, [&](const auto &entry) {
                        return entry.m_runtimeNode == node->m_node;
                    })) {
                    failure = ScenePolytreeResultCode::ControllerInvalidConfiguration;
                    return;
                }
                if (target.m_access == ScenePolytreeControllerTargetAccess::ReadWrite) {
                    const bool owned = m_controllerWriteOwners[node->m_node] != 0 ||
                                       pendingWriteOwners[node->m_node] != 0;
                    if (owned) {
                        failure = ScenePolytreeResultCode::ControllerWriteConflict;
                        return;
                    }
                    pendingWriteOwners[node->m_node] =
                        aznumeric_cast<AZ::u32>(declarationIndex + 1);
                }
                controller.m_targets.push_back({target, resolved.m_handle, node->m_node});
            });
            if (failure == ScenePolytreeResultCode::Success) {
                prepared.push_back(AZStd::move(controller));
            }
        });
    if (failure != ScenePolytreeResultCode::Success) {
        return failure;
    }

    ScenePolytreeControllerCommandSink startCommands;
    BeginControllerCommands(startCommands);
    std::ranges::for_each(prepared, [&](PreparedController &controller) {
        if (failure != ScenePolytreeResultCode::Success) {
            return;
        }
        auto &declaration = declarations[controller.m_declaration];
        ControllerBatchEntry *batch = FindControllerBatch(declaration.m_typeId);
        if (batch == nullptr) {
            auto created = controller.m_factory->CreateBatch();
            if (created == nullptr) {
                failure = ScenePolytreeResultCode::ControllerConstructionFailed;
                return;
            }
            m_controllerBatches.push_back(
                {declaration.m_typeId, controller.m_factory, AZStd::move(created)});
            batch = &m_controllerBatches.back();
        }

        AZStd::vector<ScenePolytreeResolvedControllerTarget> resolvedTargets;
        resolvedTargets.reserve(controller.m_targets.size());
        AZStd::vector<ScenePolytreeControllerTargetToken> tokens;
        tokens.reserve(controller.m_targets.size());
        std::ranges::for_each(controller.m_targets, [&](const PreparedTarget &target) {
            const auto token = AllocateControllerTarget(target.m_node, target.m_runtimeNode,
                                                        target.m_reference.m_access);
            tokens.push_back(token);
            resolvedTargets.push_back(
                {target.m_reference.m_slot, target.m_node, token, target.m_reference.m_access});
        });

        const auto created = batch->m_batch->CreateController(instance, declaration.m_declarationId,
                                                              *declaration.m_configuration,
                                                              resolvedTargets, startCommands);
        if (!created.IsSuccess()) {
            std::ranges::for_each(tokens, [&](auto token) { ReleaseControllerTarget(token); });
            failure = created.m_code == ScenePolytreeControllerResultCode::Success
                          ? ScenePolytreeResultCode::ControllerConstructionFailed
                          : MapControllerResult(created.m_code);
            return;
        }
        const ScenePolytreeControllerStateHandle publicState{m_nextControllerHandle++,
                                                             m_controllerHandleGeneration};
        if (m_nextControllerHandle == 0) {
            m_nextControllerHandle = 1;
            if (++m_controllerHandleGeneration == 0) {
                ++m_controllerHandleGeneration;
            }
        }
        m_controllers.push_back({{instance, declaration.m_typeId, publicState},
                                 created.m_state,
                                 declaration.m_declarationId,
                                 AZStd::move(tokens)});
    });

    if (failure != ScenePolytreeResultCode::Success) {
        EndControllerCommands(startCommands, false);
        (void)DetachControllers(instance);
        return failure;
    }
    EndControllerCommands(startCommands, true);
    std::ranges::sort(m_controllerBatches, [](const auto &left, const auto &right) {
        return TypeIdLess(left.m_typeId, right.m_typeId);
    });
    return ScenePolytreeResultCode::Success;
}

ScenePolytreeControllerResultCode SceneInstance::CloseControllerInput(InstanceHandle instance) {
    if (!instance.IsValid()) {
        return ScenePolytreeControllerResultCode::InvalidHandle;
    }
    if (FindSlot(instance.m_slot) == nullptr) {
        return ScenePolytreeControllerResultCode::StaleHandle;
    }
    ScenePolytreeControllerResultCode result = ScenePolytreeControllerResultCode::Success;
    bool matched{};
    std::ranges::for_each(m_controllers | std::views::filter([&](const ControllerRecord &record) {
                              return record.m_handle.m_instance == instance;
                          }),
                          [&](ControllerRecord &record) {
                              matched = true;
                              if (record.m_inputClosed) {
                                  return;
                              }
                              ControllerBatchEntry *batch =
                                  FindControllerBatch(record.m_handle.m_typeId);
                              const auto closed =
                                  batch != nullptr ? batch->m_batch->CloseInput(record.m_batchState)
                                                   : ScenePolytreeControllerResultCode::StaleHandle;
                              if (result == ScenePolytreeControllerResultCode::Success &&
                                  closed != ScenePolytreeControllerResultCode::Success) {
                                  result = closed;
                              }
                              if (closed == ScenePolytreeControllerResultCode::Success) {
                                  record.m_inputClosed = true;
                              }
                          });
    return matched ? result : ScenePolytreeControllerResultCode::StaleHandle;
}

ScenePolytreeControllerResultCode SceneInstance::DetachControllers(InstanceHandle instance) {
    if (!instance.IsValid()) {
        return ScenePolytreeControllerResultCode::InvalidHandle;
    }
    ScenePolytreeControllerResultCode result = CloseControllerInput(instance);
    std::ranges::for_each(
        m_controllers | std::views::filter([&](const ControllerRecord &record) {
            return record.m_handle.m_instance == instance;
        }),
        [&](const ControllerRecord &record) {
            ControllerBatchEntry *batch = FindControllerBatch(record.m_handle.m_typeId);
            const auto destroyed = batch != nullptr
                                       ? batch->m_batch->DestroyController(record.m_batchState)
                                       : ScenePolytreeControllerResultCode::StaleHandle;
            if (result == ScenePolytreeControllerResultCode::Success &&
                destroyed != ScenePolytreeControllerResultCode::Success) {
                result = destroyed;
            }
            std::ranges::for_each(record.m_targets,
                                  [&](auto target) { ReleaseControllerTarget(target); });
        });
    AZStd::erase_if(m_controllers, [&](const ControllerRecord &record) {
        return record.m_handle.m_instance == instance;
    });
    AZStd::vector<ControllerBatchEntry> retainedBatches;
    retainedBatches.reserve(m_controllerBatches.size());
    std::ranges::for_each(m_controllerBatches, [&](ControllerBatchEntry &batch) {
        const bool remainsInUse =
            std::ranges::any_of(m_controllers, [&](const ControllerRecord &record) {
                return record.m_handle.m_typeId == batch.m_typeId;
            });
        if (remainsInUse) {
            retainedBatches.push_back(AZStd::move(batch));
        }
    });
    m_controllerBatches.swap(retainedBatches);
    return result;
}

ScenePolytreeControllerLookupResult
SceneInstance::FindController(InstanceHandle instance, const AZ::Name &declarationId) const {
    const auto found = std::ranges::find_if(m_controllers, [&](const ControllerRecord &record) {
        return record.m_handle.m_instance == instance && record.m_declarationId == declarationId;
    });
    return found != m_controllers.end()
               ? ScenePolytreeControllerLookupResult{ScenePolytreeControllerResultCode::Success,
                                                     found->m_handle}
               : ScenePolytreeControllerLookupResult{ScenePolytreeControllerResultCode::StaleHandle,
                                                     {}};
}

ScenePolytreeControllerResultCode
SceneInstance::SubmitControllerInput(ScenePolytreeControllerHandle controller,
                                     const ScenePolytreeControllerInput &input) {
    const auto found = std::ranges::find(m_controllers, controller, &ControllerRecord::m_handle);
    if (found == m_controllers.end()) {
        return ScenePolytreeControllerResultCode::StaleHandle;
    }
    ControllerBatchEntry *batch = FindControllerBatch(controller.m_typeId);
    if (batch == nullptr) {
        return ScenePolytreeControllerResultCode::StaleHandle;
    }
    ScenePolytreeControllerCommandSink commands;
    BeginControllerCommands(commands);
    const auto result = batch->m_batch->SubmitInput(found->m_batchState, input, commands);
    EndControllerCommands(commands, result == ScenePolytreeControllerResultCode::Success);
    return result;
}

bool SceneInstance::HasControllerType(ScenePolytreeControllerTypeId typeId) const {
    return std::ranges::any_of(m_controllers, [&](const ControllerRecord &record) {
        return record.m_handle.m_typeId == typeId;
    });
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
        const auto step = m_stepSequence.next_step();
        if (HasRunningControllers()) {
            RunControllers(step);
        }
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
    return m_active && (HasRunningControllers() || !m_activeMotion.empty() || HasDirtyTransforms());
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

NodeResult SceneInstance::ResolveEntityNode(SlotHandle slot, AZ::EntityId entity) const {
    const RuntimeSlot *runtimeSlot = FindSlot(slot);
    if (runtimeSlot == nullptr) {
        return {ScenePolytreeResultCode::StaleHandle, {}};
    }
    const auto found = m_entityToNode.find(entity);
    if (found == m_entityToNode.end()) {
        return {ScenePolytreeResultCode::InvalidBinding, {}};
    }
    const auto binding =
        std::ranges::find(runtimeSlot->m_nodes, found->second, &RuntimeNodeBinding::m_node);
    return binding != runtimeSlot->m_nodes.end()
               ? NodeResult{ScenePolytreeResultCode::Success,
                            SceneNodeHandle{slot, binding->m_bindingId}}
               : NodeResult{ScenePolytreeResultCode::PartitionMismatch, {}};
}

SceneInstance::ControllerBatchEntry *
SceneInstance::FindControllerBatch(ScenePolytreeControllerTypeId typeId) {
    const auto found =
        std::ranges::find(m_controllerBatches, typeId, &ControllerBatchEntry::m_typeId);
    return found != m_controllerBatches.end() ? &*found : nullptr;
}

const SceneInstance::ControllerBatchEntry *
SceneInstance::FindControllerBatch(ScenePolytreeControllerTypeId typeId) const {
    const auto found =
        std::ranges::find(m_controllerBatches, typeId, &ControllerBatchEntry::m_typeId);
    return found != m_controllerBatches.end() ? &*found : nullptr;
}

SceneInstance::ControllerTargetRecord *
SceneInstance::FindControllerTarget(ScenePolytreeControllerTargetToken target, AZ::u64 epoch) {
    if (epoch == 0 || epoch != m_controllerCommandEpoch || !target.IsValid() ||
        target.m_index >= m_controllerTargets.size()) {
        return nullptr;
    }
    ControllerTargetRecord &record = m_controllerTargets[target.m_index];
    if (!record.m_active || record.m_generation != target.m_generation) {
        return nullptr;
    }
    return &record;
}

const SceneInstance::ControllerTargetRecord *
SceneInstance::FindControllerTarget(ScenePolytreeControllerTargetToken target,
                                    AZ::u64 epoch) const {
    if (epoch == 0 || epoch != m_controllerCommandEpoch || !target.IsValid() ||
        target.m_index >= m_controllerTargets.size()) {
        return nullptr;
    }
    const ControllerTargetRecord &record = m_controllerTargets[target.m_index];
    return record.m_active && record.m_generation == target.m_generation ? &record : nullptr;
}

ScenePolytreeControllerTargetToken
SceneInstance::AllocateControllerTarget(const SceneNodeHandle &node,
                                        wz::core::graph::NodeHandle runtimeNode,
                                        ScenePolytreeControllerTargetAccess access) {
    AZ::u32 index{};
    if (m_freeControllerTargets.empty()) {
        index = aznumeric_cast<AZ::u32>(m_controllerTargets.size());
        m_controllerTargets.push_back({});
    } else {
        index = m_freeControllerTargets.back();
        m_freeControllerTargets.pop_back();
    }
    ControllerTargetRecord &record = m_controllerTargets[index];
    record.m_node = node;
    record.m_runtimeNode = runtimeNode;
    record.m_access = access;
    record.m_pendingLocal = AZ::Transform::CreateIdentity();
    record.m_pendingMotion = {};
    record.m_localEpoch = 0;
    record.m_motionEpoch = 0;
    record.m_active = true;
    if (access == ScenePolytreeControllerTargetAccess::ReadWrite) {
        m_controllerWriteOwners[runtimeNode] = index + 1;
    }
    m_localTouched.reserve(m_controllerTargets.size());
    m_motionTouched.reserve(m_controllerTargets.size());
    m_controllerMotionUpdates.reserve(m_controllerTargets.size());
    return {index, record.m_generation};
}

void SceneInstance::ReleaseControllerTarget(ScenePolytreeControllerTargetToken target) {
    if (!target.IsValid() || target.m_index >= m_controllerTargets.size()) {
        return;
    }
    ControllerTargetRecord &record = m_controllerTargets[target.m_index];
    if (!record.m_active || record.m_generation != target.m_generation) {
        return;
    }
    if (record.m_access == ScenePolytreeControllerTargetAccess::ReadWrite &&
        m_controllerWriteOwners[record.m_runtimeNode] == target.m_index + 1) {
        m_controllerWriteOwners[record.m_runtimeNode] = 0;
    }
    record.m_active = false;
    record.m_node = {};
    record.m_runtimeNode = wz::core::graph::INVALID_NODE;
    if (++record.m_generation == 0) {
        ++record.m_generation;
    }
    m_freeControllerTargets.push_back(target.m_index);
}

bool SceneInstance::HasControllers(SlotHandle slot) const {
    return std::ranges::any_of(m_controllers, [&](const ControllerRecord &record) {
        return record.m_handle.m_instance.m_slot == slot;
    });
}

void SceneInstance::BeginControllerCommands(ScenePolytreeControllerCommandSink &sink) {
    m_localTouched.clear();
    m_motionTouched.clear();
    m_controllerMotionUpdates.clear();
    if (++m_controllerCommandEpoch == 0) {
        ++m_controllerCommandEpoch;
    }
    ControllerCommandSinkAccess::Bind(sink, *this, m_controllerCommandEpoch);
}

void SceneInstance::EndControllerCommands(ScenePolytreeControllerCommandSink &sink, bool apply) {
    ControllerCommandSinkAccess::Clear(sink);
    if (apply) {
        std::ranges::for_each(m_localTouched, [&](AZ::u32 index) {
            const ControllerTargetRecord &target = m_controllerTargets[index];
            const auto applied =
                m_runtime.set_local(target.m_runtimeNode, AzTransformValue(target.m_pendingLocal));
            AZ_Error("ScenePolytree", applied == scene_polytree::transform_error::none,
                     "Failed to apply a behavior local-transform command.");
        });
        std::ranges::transform(
            m_motionTouched, std::back_inserter(m_controllerMotionUpdates), [&](AZ::u32 index) {
                const ControllerTargetRecord &target = m_controllerTargets[index];
                return ActiveSet::update_type{target.m_runtimeNode, target.m_pendingMotion};
            });
        const auto applied = m_activeMotion.apply_updates(m_controllerMotionUpdates, m_policy,
                                                          m_activeMotionUpdateWorkspace);
        AZ_Error("ScenePolytree", applied == scene_polytree::motion::motion_error::none,
                 "Failed to apply a behavior motion command batch.");
    }
    m_localTouched.clear();
    m_motionTouched.clear();
    m_controllerMotionUpdates.clear();
}

bool SceneInstance::HasRunningControllers() const {
    return std::ranges::any_of(m_controllerBatches, [](const ControllerBatchEntry &entry) {
        return entry.m_batch->HasRunningControllers();
    });
}

void SceneInstance::RunControllers(scene_polytree::motion::fixed_motion_step step) {
    ScenePolytreeControllerCommandSink sink;
    BeginControllerCommands(sink);
    std::ranges::for_each(m_controllerBatches, [&](ControllerBatchEntry &entry) {
        if (entry.m_batch->HasRunningControllers()) {
            entry.m_batch->FixedStepBatch({step.tick, step.delta}, sink);
        }
    });
    EndControllerCommands(sink, true);
}

ScenePolytreeControllerResultCode
SceneInstance::SetControllerLocal(ScenePolytreeControllerTargetToken target, AZ::u64 epoch,
                                  const AZ::Transform &local) {
    ControllerTargetRecord *record = FindControllerTarget(target, epoch);
    if (record == nullptr) {
        return ScenePolytreeControllerResultCode::InvalidTarget;
    }
    if (record->m_access != ScenePolytreeControllerTargetAccess::ReadWrite) {
        return ScenePolytreeControllerResultCode::ReadOnlyTarget;
    }
    if (!local.IsFinite()) {
        return ScenePolytreeControllerResultCode::InvalidInput;
    }
    record->m_pendingLocal = local;
    if (record->m_localEpoch != epoch) {
        record->m_localEpoch = epoch;
        m_localTouched.push_back(target.m_index);
    }
    return ScenePolytreeControllerResultCode::Success;
}

ScenePolytreeControllerResultCode
SceneInstance::SetControllerVelocity(ScenePolytreeControllerTargetToken target, AZ::u64 epoch,
                                     const AZ::Vector3 &linear, const AZ::Vector3 &angular) {
    ControllerTargetRecord *record = FindControllerTarget(target, epoch);
    if (record == nullptr) {
        return ScenePolytreeControllerResultCode::InvalidTarget;
    }
    if (record->m_access != ScenePolytreeControllerTargetAccess::ReadWrite) {
        return ScenePolytreeControllerResultCode::ReadOnlyTarget;
    }
    if (!linear.IsFinite() || !angular.IsFinite()) {
        return ScenePolytreeControllerResultCode::InvalidInput;
    }
    record->m_pendingMotion = {linear, angular};
    if (record->m_motionEpoch != epoch) {
        record->m_motionEpoch = epoch;
        m_motionTouched.push_back(target.m_index);
    }
    return ScenePolytreeControllerResultCode::Success;
}

ScenePolytreeControllerResultCode
SceneInstance::GetControllerLocal(ScenePolytreeControllerTargetToken target, AZ::u64 epoch,
                                  AZ::Transform &local) const {
    const ControllerTargetRecord *record = FindControllerTarget(target, epoch);
    if (record == nullptr) {
        return ScenePolytreeControllerResultCode::InvalidTarget;
    }
    local = record->m_localEpoch == epoch ? record->m_pendingLocal
                                          : m_runtime.state().local(record->m_runtimeNode).m_value;
    return ScenePolytreeControllerResultCode::Success;
}

void SceneInstance::ClearSlot(RuntimeSlot &slot) {
    std::ranges::for_each(slot.m_nodes, [&](const RuntimeNodeBinding &node) {
        m_entityToNode.erase(m_entityBindings[node.m_node].m_entity);
        m_entityBindings[node.m_node] = EntityTarget{};
        (void)m_activeMotion.deactivate(node.m_node);
        (void)m_runtime.set_local(node.m_node, AzTransformValue(node.m_initialLocal));
    });
}
} // namespace ScenePolytree::Internal
