#include <Arduino.h>
#include <ESPStateMachine.h>

enum class DoorState : uint8_t {
	Locked,
	Unlocked,
	Alarm,
};

enum class DoorEvent : uint8_t {
	BadgePresented,
	Reset,
};

ESPStateMachine<DoorState, DoorEvent> machine;
bool badgeAllowed = false;

void setup() {
	Serial.begin(115200);

	TransitionOptions<DoorState, DoorEvent> allowOptions;
	allowOptions.guard = [](const TransitionContext<DoorState, DoorEvent> &) {
		return badgeAllowed;
	};
	allowOptions.action = [](const TransitionContext<DoorState, DoorEvent> &) {
		Serial.println("[door] badge accepted");
	};

	TransitionOptions<DoorState, DoorEvent> denyOptions;
	denyOptions.guard = [](const TransitionContext<DoorState, DoorEvent> &) {
		return !badgeAllowed;
	};
	denyOptions.action = [](const TransitionContext<DoorState, DoorEvent> &) {
		Serial.println("[door] badge rejected");
	};

	machine.addTransition(DoorState::Locked, DoorEvent::BadgePresented, DoorState::Unlocked, allowOptions);
	machine.addTransition(DoorState::Locked, DoorEvent::BadgePresented, DoorState::Alarm, denyOptions);
	machine.addTransition(DoorState::Alarm, DoorEvent::Reset, DoorState::Locked);
	machine.begin(DoorState::Locked);

	machine.dispatch(DoorEvent::BadgePresented);
	badgeAllowed = true;
	machine.dispatch(DoorEvent::Reset);
	machine.dispatch(DoorEvent::BadgePresented);
}

void loop() {
	delay(1000);
}
