#include <Arduino.h>
#include <ESPStateMachine.h>

enum class ToggleState : uint8_t {
	Off,
	On,
};

enum class ToggleEvent : uint8_t {
	Press,
};

ESPStateMachine<ToggleState, ToggleEvent> machine;

void setup() {
	Serial.begin(115200);

	machine.addTransition(ToggleState::Off, ToggleEvent::Press, ToggleState::On);
	machine.addTransition(ToggleState::On, ToggleEvent::Press, ToggleState::Off);
	machine.onTransition([](const TransitionContext<ToggleState, ToggleEvent> &ctx) {
		Serial.printf("[toggle] seq=%lu\n", static_cast<unsigned long>(ctx.sequence));
	});

	machine.begin(ToggleState::Off);
}

void loop() {
	machine.dispatch(ToggleEvent::Press);
	delay(1000);
}
