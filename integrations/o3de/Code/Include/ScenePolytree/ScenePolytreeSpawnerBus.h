#pragma once

#include <ScenePolytree/ScenePolytreeControllerTypes.h>
#include <ScenePolytree/ScenePolytreeSpawnerTypes.h>

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/std/containers/vector.h>

namespace ScenePolytree {
class ScenePolytreeSpawnerRequests : public AZ::ComponentBus {
  public:
    virtual SpawnRequestId Spawn(const ScenePolytreeSpawnRequest &request) = 0;
    virtual DespawnRequestId Despawn(InstanceHandle instance) = 0;
    virtual bool CancelSpawn(SpawnRequestId request) = 0;
    virtual ScenePolytreeSpawnerLifecycle GetSpawnerLifecycle() const = 0;
};
using ScenePolytreeSpawnerRequestBus = AZ::EBus<ScenePolytreeSpawnerRequests>;

class ScenePolytreeSpawnerNotifications : public AZ::ComponentBus {
  public:
    virtual void OnScenePolytreeSpawnerReady(SpawnerHandle) {}
    virtual void OnScenePolytreeSpawnerFailed(const ScenePolytreeSpawnerFailure &) {}
    virtual void OnSpawnSucceeded(SpawnRequestId, InstanceHandle,
                                  const ScenePolytreeRequestContext &) {}
    virtual void OnSpawnFailed(SpawnRequestId, const ScenePolytreeSpawnFailure &,
                               const ScenePolytreeRequestContext &) {}
    virtual void OnDespawnSucceeded(DespawnRequestId, InstanceHandle) {}
    virtual void OnDespawnFailed(DespawnRequestId, InstanceHandle,
                                 const ScenePolytreeDespawnFailure &) {}
};
using ScenePolytreeSpawnerNotificationBus = AZ::EBus<ScenePolytreeSpawnerNotifications>;

namespace Internal {
class ScenePolytreeSpawnerAsyncNotifications : public AZ::ComponentBus {
  public:
    virtual void OnScenePolytreeSpawnCompleted(
        AZ::u32 spawnerGeneration, SpawnRequestId request, AZ::u32 ticketId, SpawnError error,
        AZStd::vector<ScenePolytreeEntityBinding> bindings,
        AZStd::vector<ScenePolytreeControllerDeclaration> declarations) = 0;
    virtual void OnScenePolytreeDespawnCompleted(AZ::u32 spawnerGeneration, SpawnRequestId request,
                                                 AZ::u32 ticketId) = 0;
};
using ScenePolytreeSpawnerAsyncNotificationBus = AZ::EBus<ScenePolytreeSpawnerAsyncNotifications>;
} // namespace Internal
} // namespace ScenePolytree
