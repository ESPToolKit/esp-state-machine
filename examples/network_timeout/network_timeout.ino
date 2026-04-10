#include <Arduino.h>
#include <ESPTimer.h>
#include <ESPStateMachine.h>
#include <esp_state_machine/adapters/timer_bridge.h>

enum class NetworkState : uint8_t {
	Idle,
	Connecting,
	Online,
	Fault,
};

enum class NetworkEvent : uint8_t {
	Start,
	GotIp,
	Timeout,
	Reset,
};

ESPTimer timer;
ESPStateMachine<NetworkState, NetworkEvent> machine;
ESPStateMachineTimerBridge<NetworkState, NetworkEvent> timeouts;

void setup() {
	Serial.begin(115200);
	timer.init();

	machine.addTransition(NetworkState::Idle, NetworkEvent::Start, NetworkState::Connecting);
	machine.addTransition(NetworkState::Connecting, NetworkEvent::GotIp, NetworkState::Online);
	machine.addTransition(NetworkState::Connecting, NetworkEvent::Timeout, NetworkState::Fault);
	machine.addTransition(NetworkState::Fault, NetworkEvent::Reset, NetworkState::Idle);

	timeouts.addTimeout(NetworkState::Connecting, 5000, NetworkEvent::Timeout);
	timeouts.attach(machine, timer);

	machine.begin(NetworkState::Idle);
	machine.dispatch(NetworkEvent::Start);
}

void loop() {
	delay(1000);
}
