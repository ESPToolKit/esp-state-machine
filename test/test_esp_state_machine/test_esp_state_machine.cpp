#include <ESPStateMachine.h>
#include <esp_state_machine/adapters/eventbus_bridge.h>
#include <esp_state_machine/adapters/logger_observer.h>
#include <esp_state_machine/adapters/timer_bridge.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

enum class TestState : uint8_t {
	Idle,
	Connecting,
	Online,
	Fault,
};

enum class TestEvent : uint8_t {
	Start,
	Connected,
	Timeout,
	Reset,
};

[[noreturn]] void fail(const std::string &message) {
	throw std::runtime_error(message);
}

void expectTrue(bool condition, const std::string &message) {
	if (!condition) {
		fail(message);
	}
}

void expectFalse(bool condition, const std::string &message) {
	if (condition) {
		fail(message);
	}
}

template <typename T>
void expectEqual(const T &actual, const T &expected, const std::string &message) {
	if (!(actual == expected)) {
		fail(message);
	}
}

void testBeginEndLifecycleCallbacks() {
	ESPStateMachine<TestState, TestEvent> machine;
	bool bootstrapped = false;
	bool shutdown = false;

	machine.onEnter(TestState::Idle, [&bootstrapped](const StateCallbackContext<TestState, TestEvent> &ctx) {
		bootstrapped = ctx.bootstrap;
	});
	machine.onExit(TestState::Idle, [&shutdown](const StateCallbackContext<TestState, TestEvent> &ctx) {
		shutdown = ctx.shutdown;
	});

	expectTrue(machine.begin(TestState::Idle), "begin should succeed");
	expectTrue(bootstrapped, "begin should invoke bootstrap enter callback");
	expectTrue(machine.isStarted(), "machine should report started");

	machine.end();
	expectTrue(shutdown, "end should invoke shutdown exit callback");
	expectFalse(machine.isStarted(), "machine should report stopped");
}

void testSimpleTransitionAndSnapshot() {
	ESPStateMachine<TestState, TestEvent> machine;
	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.begin(TestState::Idle);

	auto result = machine.dispatch(TestEvent::Start);
	expectTrue(result.ok(), "start transition should succeed");
	expectEqual(machine.currentState(), TestState::Connecting, "current state should update");

	auto snap = machine.snapshot();
	expectEqual(snap.currentState, TestState::Connecting, "snapshot should capture current state");
	expectEqual(snap.previousState, TestState::Idle, "snapshot should capture previous state");
	expectEqual(
	    snap.lastStatus,
	    StateMachineDispatchStatus::Transitioned,
	    "snapshot should capture last status"
	);
}

void testNoTransitionAndGuardRejected() {
	ESPStateMachine<TestState, TestEvent> machine;

	TransitionOptions<TestState, TestEvent> rejecting;
	rejecting.guard = [](const TransitionContext<TestState, TestEvent> &) { return false; };

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting, rejecting);
	machine.begin(TestState::Idle);

	auto noTransition = machine.dispatch(TestEvent::Reset);
	expectEqual(
	    noTransition.status,
	    StateMachineDispatchStatus::NoTransition,
	    "unknown event should return NoTransition"
	);

	auto rejected = machine.dispatch(TestEvent::Start);
	expectEqual(
	    rejected.status,
	    StateMachineDispatchStatus::GuardRejected,
	    "matching transitions with rejecting guards should return GuardRejected"
	);
}

void testGuardedBranchingOrder() {
	ESPStateMachine<TestState, TestEvent> machine;

	TransitionOptions<TestState, TestEvent> first;
	first.guard = [](const TransitionContext<TestState, TestEvent> &) { return false; };

	TransitionOptions<TestState, TestEvent> second;
	second.guard = [](const TransitionContext<TestState, TestEvent> &) { return true; };

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Fault, first);
	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting, second);
	machine.begin(TestState::Idle);

	auto result = machine.dispatch(TestEvent::Start);
	expectTrue(result.ok(), "second guarded transition should be selected");
	expectEqual(result.to, TestState::Connecting, "first passing transition should win");
}

void testCallbackOrderAndReentrantBusy() {
	ESPStateMachine<TestState, TestEvent> machine;
	std::vector<std::string> order;
	StateMachineDispatchStatus nestedStatus = StateMachineDispatchStatus::Transitioned;

	TransitionOptions<TestState, TestEvent> options;
	options.action = [&machine, &nestedStatus, &order](const TransitionContext<TestState, TestEvent> &) {
		order.push_back("action");
		nestedStatus = machine.dispatch(TestEvent::Reset).status;
	};

	machine.addTransition(TestState::Connecting, TestEvent::Connected, TestState::Online, options);
	machine.onExit(TestState::Connecting, [&order](const StateCallbackContext<TestState, TestEvent> &) {
		order.push_back("exit");
	});
	machine.onEnter(TestState::Online, [&order](const StateCallbackContext<TestState, TestEvent> &) {
		order.push_back("enter");
	});
	machine.onTransition([&order](const TransitionContext<TestState, TestEvent> &) {
		order.push_back("observer");
	});

	machine.begin(TestState::Connecting);
	order.clear();

	auto result = machine.dispatch(TestEvent::Connected);
	expectTrue(result.ok(), "connected transition should succeed");
	expectEqual(nestedStatus, StateMachineDispatchStatus::Busy, "nested dispatch should return Busy");
	expectEqual(order.size(), static_cast<size_t>(4), "callback order should have four entries");
	expectEqual(order[0], std::string("exit"), "exit should run first");
	expectEqual(order[1], std::string("action"), "action should run second");
	expectEqual(order[2], std::string("enter"), "enter should run third");
	expectEqual(order[3], std::string("observer"), "observer should run fourth");
}

void testEventBusBridgeDispatchesBoundEvent() {
	ESPEventBus bus;
	ESPStateMachine<TestState, TestEvent> machine;
	ESPStateMachineEventBusBridge<TestState, TestEvent> bridge;

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	expectTrue(bridge.attach(bus, machine), "event bus bridge should attach");
	expectTrue(bridge.bind(7, TestEvent::Start) != 0, "event bus bridge should bind event");

	machine.begin(TestState::Idle);
	bus.emit(7);
	expectEqual(machine.currentState(), TestState::Connecting, "bus event should dispatch FSM event");
}

void testTimerBridgeTimeoutAndStaleCancel() {
	ESPTimer timer;
	ESPStateMachine<TestState, TestEvent> machine;
	ESPStateMachineTimerBridge<TestState, TestEvent> bridge;

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.addTransition(TestState::Connecting, TestEvent::Connected, TestState::Online);
	machine.addTransition(TestState::Connecting, TestEvent::Timeout, TestState::Fault);

	expectTrue(bridge.addTimeout(TestState::Connecting, 100, TestEvent::Timeout), "timeout should add");
	expectTrue(bridge.attach(machine, timer), "timer bridge should attach");

	machine.begin(TestState::Idle);
	machine.dispatch(TestEvent::Start);
	const uint32_t staleTimer = timer.lastTimeoutId;
	expectTrue(staleTimer != 0, "entering connecting should arm timeout");

	machine.dispatch(TestEvent::Connected);
	expectFalse(timer.fire(staleTimer), "leaving state should cancel stale timeout");
	expectEqual(machine.currentState(), TestState::Online, "stale timeout should not alter state");
}

void testTimerBridgeFiresTimeout() {
	ESPTimer timer;
	ESPStateMachine<TestState, TestEvent> machine;
	ESPStateMachineTimerBridge<TestState, TestEvent> bridge;

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.addTransition(TestState::Connecting, TestEvent::Timeout, TestState::Fault);

	bridge.addTimeout(TestState::Connecting, 100, TestEvent::Timeout);
	bridge.attach(machine, timer);

	machine.begin(TestState::Idle);
	machine.dispatch(TestEvent::Start);

	expectTrue(timer.fire(timer.lastTimeoutId), "active timeout should fire");
	expectEqual(machine.currentState(), TestState::Fault, "timeout event should transition to fault");
}

const char *stateName(TestState state) {
	switch (state) {
	case TestState::Idle:
		return "Idle";
	case TestState::Connecting:
		return "Connecting";
	case TestState::Online:
		return "Online";
	case TestState::Fault:
		return "Fault";
	}
	return "?";
}

const char *eventName(TestEvent event) {
	switch (event) {
	case TestEvent::Start:
		return "Start";
	case TestEvent::Connected:
		return "Connected";
	case TestEvent::Timeout:
		return "Timeout";
	case TestEvent::Reset:
		return "Reset";
	}
	return "?";
}

void testLoggerObserverLogsTransitionsAndRejections() {
	ESPLogger logger;
	ESPStateMachine<TestState, TestEvent> machine;
	ESPStateMachineLoggerObserver<TestState, TestEvent> observer;

	logger.init();
	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	expectTrue(
	    observer.attach(machine, logger, {"TEST_FSM", stateName, eventName, true}),
	    "logger observer should attach"
	);

	machine.begin(TestState::Idle);
	machine.dispatch(TestEvent::Start);
	machine.dispatch(TestEvent::Reset);

	expectEqual(logger.infoCount, static_cast<uint32_t>(1), "logger should record one transition");
	expectEqual(logger.warnCount, static_cast<uint32_t>(1), "logger should record one rejection");
}

} // namespace

int main() {
	try {
		testBeginEndLifecycleCallbacks();
		testSimpleTransitionAndSnapshot();
		testNoTransitionAndGuardRejected();
		testGuardedBranchingOrder();
		testCallbackOrderAndReentrantBusy();
		testEventBusBridgeDispatchesBoundEvent();
		testTimerBridgeTimeoutAndStaleCancel();
		testTimerBridgeFiresTimeout();
		testLoggerObserverLogsTransitionsAndRejections();
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}

	std::cout << "ESPStateMachine tests passed\n";
	return 0;
}
