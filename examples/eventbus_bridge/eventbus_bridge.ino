#include <Arduino.h>
#include <ESPEventBus.h>
#include <ESPStateMachine.h>
#include <esp_state_machine/adapters/eventbus_bridge.h>

enum class AppBusEvent : uint16_t {
	ButtonPressed = 1,
};

enum class LedState : uint8_t {
	Off,
	On,
};

enum class LedEvent : uint8_t {
	Toggle,
};

ESPEventBus eventBus;
ESPStateMachine<LedState, LedEvent> machine;
ESPStateMachineEventBusBridge<LedState, LedEvent> bridge;

void setup() {
	Serial.begin(115200);
	eventBus.init();

	machine.addTransition(LedState::Off, LedEvent::Toggle, LedState::On);
	machine.addTransition(LedState::On, LedEvent::Toggle, LedState::Off);

	bridge.attach(eventBus, machine);
	bridge.bind(static_cast<EventBusId>(AppBusEvent::ButtonPressed), LedEvent::Toggle);

	machine.begin(LedState::Off);
}

void loop() {
	eventBus.post(AppBusEvent::ButtonPressed, nullptr);
	delay(1000);
}
