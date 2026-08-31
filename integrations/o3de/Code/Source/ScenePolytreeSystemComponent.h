#pragma once

#include "SceneInstance.h"

#include <ScenePolytree/ScenePolytreeBus.h>
#include <ScenePolytree/ScenePolytreeTypeIds.h>

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/parallel/mutex.h>

#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ScenePolytree {
class ScenePolytreeSystemComponent final : public AZ::Component,
                                           public AZ::TickBus::Handler,
                                           public ScenePolytreeRequestBus::Handler {
  public:
    AZ_COMPONENT(ScenePolytreeSystemComponent, ScenePolytreeSystemComponentTypeId);

    static void Reflect(AZ::ReflectContext *context);
    static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType &provided);
    static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType &incompatible);

    void Activate() override;
    void Deactivate() override;

    SceneHandle CreateTankScene(const TankSceneDescriptor &descriptor) override;
    void DestroyScene(SceneHandle scene) override;
    bool BindTankEntities(TankHandle tank, const TankEntityBindings &bindings) override;
    bool RemoveTankEntities(TankHandle tank) override;
    bool MarkTankReady(TankHandle tank) override;
    bool SetSceneActive(SceneHandle scene, bool active) override;
    bool SubmitTankIntent(TankHandle tank, const TankIntent &intent) override;
    bool RequestCorrection(const SceneCorrection &correction) override;
    SceneStatistics GetSceneStatistics(SceneHandle scene) const override;
    bool IsSceneAlive(SceneHandle scene) const override;

    void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
    int GetTickOrder() override;

  private:
    struct CreateCommand {
        SceneHandle m_scene;
        TankSceneDescriptor m_descriptor;
    };
    struct DestroyCommand {
        SceneHandle m_scene;
    };
    struct BindCommand {
        TankHandle m_tank;
        TankEntityBindings m_bindings;
    };
    struct UnbindCommand {
        TankHandle m_tank;
    };
    struct ReadyCommand {
        TankHandle m_tank;
    };
    struct ActiveCommand {
        SceneHandle m_scene;
        bool m_active{};
    };
    struct IntentCommand {
        TankHandle m_tank;
        TankIntent m_intent;
    };
    struct CorrectionCommand {
        SceneCorrection m_correction;
    };
    using Command = std::variant<CreateCommand, DestroyCommand, BindCommand, UnbindCommand,
                                 ReadyCommand, ActiveCommand, IntentCommand, CorrectionCommand>;

    enum class SceneLife : AZ::u8 { Pending, Alive, Destroying };
    struct SceneEntry {
        std::unique_ptr<Internal::SceneInstance> m_instance;
        SceneLife m_life{SceneLife::Pending};
    };

    [[nodiscard]] Internal::SceneInstance *FindScene(SceneHandle scene);
    [[nodiscard]] const Internal::SceneInstance *FindScene(SceneHandle scene) const;
    [[nodiscard]] bool AcceptsCommands(SceneHandle scene) const;
    void Enqueue(Command command);
    void DrainCommands();
    void Process(const CreateCommand &command);
    void Process(const DestroyCommand &command);
    void Process(const BindCommand &command);
    void Process(const UnbindCommand &command);
    void Process(const ReadyCommand &command);
    void Process(const ActiveCommand &command);
    void Process(const IntentCommand &command);
    void Process(const CorrectionCommand &command);
    void RefreshTickConnection();

    mutable AZStd::recursive_mutex m_mutex;
    std::unordered_map<AZ::u64, SceneEntry> m_scenes;
    std::vector<Command> m_commands;
    AZ::u64 m_nextSceneId{1};
};
} // namespace ScenePolytree
