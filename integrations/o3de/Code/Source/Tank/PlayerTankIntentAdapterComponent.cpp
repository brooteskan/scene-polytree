#include <ScenePolytree/Tank/PlayerTankIntentAdapterComponent.h>

#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>

namespace ScenePolytree {
AZ_COMPONENT_IMPL(PlayerTankIntentAdapterComponent, "PlayerTankIntentAdapterComponent",
                  PlayerTankIntentAdapterComponentTypeId);

void PlayerTankIntentAdapterComponent::Reflect(AZ::ReflectContext *context) {
    if (auto *serializeContext = azrtti_cast<AZ::SerializeContext *>(context)) {
        serializeContext->Class<PlayerTankIntentAdapterComponent, AZ::Component>()->Version(1);
        if (AZ::EditContext *editContext = serializeContext->GetEditContext()) {
            editContext
                ->Class<PlayerTankIntentAdapterComponent>(
                    "Scene Polytree Player Tank Input",
                    "Converts keyboard input into TankIntent values.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"));
        }
    }
}

void PlayerTankIntentAdapterComponent::GetProvidedServices(
    AZ::ComponentDescriptor::DependencyArrayType &provided) {
    provided.push_back(AZ_CRC_CE("ScenePolytreeTankIntentAdapterService"));
}

void PlayerTankIntentAdapterComponent::GetIncompatibleServices(
    AZ::ComponentDescriptor::DependencyArrayType &incompatible) {
    incompatible.push_back(AZ_CRC_CE("ScenePolytreeTankIntentAdapterService"));
}

void PlayerTankIntentAdapterComponent::Activate() {
    TankAdapterRequestBus::Handler::BusConnect(GetEntityId());
    AzFramework::InputChannelEventListener::Connect();
}

void PlayerTankIntentAdapterComponent::Deactivate() {
    AzFramework::InputChannelEventListener::Disconnect();
    TankAdapterRequestBus::Handler::BusDisconnect();
    m_tank = {};
}

void PlayerTankIntentAdapterComponent::AssignTank(TankHandle tank) {
    m_tank = tank;
    SubmitCurrentIntent();
}

bool PlayerTankIntentAdapterComponent::OnInputChannelEventFiltered(
    const AzFramework::InputChannel &inputChannel) {
    const auto &id = inputChannel.GetInputChannelId();
    const float value = inputChannel.GetState() == AzFramework::InputChannel::State::Ended
                            ? 0.0f
                            : inputChannel.GetValue();
    bool recognized = true;
    if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericW)
        m_forwardKey = value;
    else if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericS)
        m_backwardKey = value;
    else if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericA)
        m_leftKey = value;
    else if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericD)
        m_rightKey = value;
    else if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericJ)
        m_turretLeftKey = value;
    else if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericL)
        m_turretRightKey = value;
    else if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericI)
        m_gunUpKey = value;
    else if (id == AzFramework::InputDeviceKeyboard::Key::AlphanumericK)
        m_gunDownKey = value;
    else
        recognized = false;

    if (recognized) {
        m_input = {
            m_forwardKey - m_backwardKey,
            m_leftKey - m_rightKey,
            m_turretLeftKey - m_turretRightKey,
            m_gunUpKey - m_gunDownKey,
        };
        SubmitCurrentIntent();
    }
    return false;
}

void PlayerTankIntentAdapterComponent::SubmitCurrentIntent() {
    if (m_tank.IsValid()) {
        if (auto *requests = AZ::Interface<ScenePolytreeRequests>::Get()) {
            (void)requests->SubmitTankIntent(m_tank, MakePlayerTankIntent(m_input, m_tuning));
        }
    }
}
} // namespace ScenePolytree
