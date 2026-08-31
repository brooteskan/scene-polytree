#include <ScenePolytree/Tank/AiTankIntentAdapterComponent.h>

#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace ScenePolytree {
AZ_COMPONENT_IMPL(AiTankIntentAdapterComponent, "AiTankIntentAdapterComponent",
                  AiTankIntentAdapterComponentTypeId);

void AiTankIntentAdapterComponent::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<AiTankIntentAdapterComponent, AZ::Component>()->Version(1);
        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<AiTankIntentAdapterComponent>("Scene Polytree AI Tank Input",
                                                      "Converts AI goals into TankIntent values.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"));
        }
    }
}

void AiTankIntentAdapterComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeTankIntentAdapterService"));
}

void AiTankIntentAdapterComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeTankIntentAdapterService"));
}

void AiTankIntentAdapterComponent::Activate() {
    TankAdapterRequestBus::Handler::BusConnect(GetEntityId());
    AiTankGoalRequestBus::Handler::BusConnect(GetEntityId());
}

void AiTankIntentAdapterComponent::Deactivate() {
    AiTankGoalRequestBus::Handler::BusDisconnect();
    TankAdapterRequestBus::Handler::BusDisconnect();
    m_tank = {};
}

void AiTankIntentAdapterComponent::AssignTank(TankHandle tank) {
    m_tank = tank;
    SubmitCurrentGoal();
}

void AiTankIntentAdapterComponent::SubmitGoal(const AiTankGoal &goal) {
    m_goal = goal;
    SubmitCurrentGoal();
}

void AiTankIntentAdapterComponent::SubmitCurrentGoal() {
    if (m_tank.IsValid()) {
        if (auto *requests = AZ::Interface<ScenePolytreeRequests>::Get()) {
            (void)requests->SubmitTankIntent(m_tank, MakeAiTankIntent(m_goal));
        }
    }
}
} // namespace ScenePolytree
