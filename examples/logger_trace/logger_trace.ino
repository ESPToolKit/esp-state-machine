#include <Arduino.h>
#include <ESPLogger.h>
#include <ESPStateMachine.h>
#include <esp_state_machine/adapters/logger_observer.h>

enum class JobState : uint8_t {
	Idle,
	Running,
	Failed,
};

enum class JobEvent : uint8_t {
	Start,
	Fail,
	Reset,
};

const char *stateName(JobState state) {
	switch (state) {
	case JobState::Idle:
		return "Idle";
	case JobState::Running:
		return "Running";
	case JobState::Failed:
		return "Failed";
	}
	return "?";
}

const char *eventName(JobEvent event) {
	switch (event) {
	case JobEvent::Start:
		return "Start";
	case JobEvent::Fail:
		return "Fail";
	case JobEvent::Reset:
		return "Reset";
	}
	return "?";
}

ESPLogger logger;
ESPStateMachine<JobState, JobEvent> machine;
ESPStateMachineLoggerObserver<JobState, JobEvent> trace;

void setup() {
	Serial.begin(115200);
	logger.init();

	machine.addTransition(JobState::Idle, JobEvent::Start, JobState::Running);
	machine.addTransition(JobState::Running, JobEvent::Fail, JobState::Failed);
	machine.addTransition(JobState::Failed, JobEvent::Reset, JobState::Idle);

	trace.attach(machine, logger, {"JOB_FSM", stateName, eventName, true});
	machine.begin(JobState::Idle);
	machine.dispatch(JobEvent::Start);
	machine.dispatch(JobEvent::Fail);
}

void loop() {
	delay(1000);
}
