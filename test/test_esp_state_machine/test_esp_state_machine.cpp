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

void expectOrder(
    const std::vector<std::string> &actual,
    const std::vector<std::string> &expected,
    const std::string &message
) {
	expectEqual(actual.size(), expected.size(), message + " size");
	for (size_t i = 0; i < expected.size(); ++i) {
		expectEqual(actual[i], expected[i], message + " entry " + std::to_string(i));
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

void testEndDuringActionDefersShutdown() {
	ESPStateMachine<TestState, TestEvent> machine;
	std::vector<std::string> order;

	TransitionOptions<TestState, TestEvent> options;
	options.action = [&machine, &order](const TransitionContext<TestState, TestEvent> &) {
		order.push_back("action");
		machine.end();
	};

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting, options);
	machine.onExit(TestState::Idle, [&order](const StateCallbackContext<TestState, TestEvent> &) {
		order.push_back("exit");
	});
	machine.onEnter(
	    TestState::Connecting,
	    [&order](const StateCallbackContext<TestState, TestEvent> &) { order.push_back("enter"); }
	);
	machine.onTransition([&order](const TransitionContext<TestState, TestEvent> &) {
		order.push_back("observer");
	});
	machine.onExit(TestState::Connecting, [&order](const StateCallbackContext<TestState, TestEvent> &ctx) {
		if (ctx.shutdown) {
			order.push_back("shutdown");
		}
	});

	machine.begin(TestState::Idle);
	order.clear();

	auto result = machine.dispatch(TestEvent::Start);
	expectTrue(result.ok(), "end from action should still complete transition");
	expectFalse(machine.isStarted(), "end from action should stop after dispatch completes");
	expectEqual(
	    machine.currentState(),
	    TestState::Connecting,
	    "deferred shutdown should keep target state"
	);
	expectOrder(order, {"exit", "action", "enter", "observer", "shutdown"}, "end from action order");
}

void testEndDuringEnterDefersShutdown() {
	ESPStateMachine<TestState, TestEvent> machine;
	std::vector<std::string> order;

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.onExit(TestState::Idle, [&order](const StateCallbackContext<TestState, TestEvent> &) {
		order.push_back("exit");
	});
	machine.onEnter(
	    TestState::Connecting,
	    [&machine, &order](const StateCallbackContext<TestState, TestEvent> &) {
		    order.push_back("enter");
		    machine.end();
	    }
	);
	machine.onTransition([&order](const TransitionContext<TestState, TestEvent> &) {
		order.push_back("observer");
	});
	machine.onExit(TestState::Connecting, [&order](const StateCallbackContext<TestState, TestEvent> &ctx) {
		if (ctx.shutdown) {
			order.push_back("shutdown");
		}
	});

	machine.begin(TestState::Idle);
	order.clear();

	auto result = machine.dispatch(TestEvent::Start);
	expectTrue(result.ok(), "end from enter should still complete transition");
	expectFalse(machine.isStarted(), "end from enter should stop after dispatch completes");
	expectOrder(order, {"exit", "enter", "observer", "shutdown"}, "end from enter order");
}

void testEndDuringExitDefersShutdown() {
	ESPStateMachine<TestState, TestEvent> machine;
	std::vector<std::string> order;

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.onExit(TestState::Idle, [&machine, &order](const StateCallbackContext<TestState, TestEvent> &) {
		order.push_back("exit");
		machine.end();
	});
	machine.onEnter(
	    TestState::Connecting,
	    [&order](const StateCallbackContext<TestState, TestEvent> &) { order.push_back("enter"); }
	);
	machine.onTransition([&order](const TransitionContext<TestState, TestEvent> &) {
		order.push_back("observer");
	});
	machine.onExit(TestState::Connecting, [&order](const StateCallbackContext<TestState, TestEvent> &ctx) {
		if (ctx.shutdown) {
			order.push_back("shutdown");
		}
	});

	machine.begin(TestState::Idle);
	order.clear();

	auto result = machine.dispatch(TestEvent::Start);
	expectTrue(result.ok(), "end from exit should still complete transition");
	expectFalse(machine.isStarted(), "end from exit should stop after dispatch completes");
	expectOrder(order, {"exit", "enter", "observer", "shutdown"}, "end from exit order");
}

void testEndDuringTransitionObserverDefersShutdown() {
	ESPStateMachine<TestState, TestEvent> machine;
	std::vector<std::string> order;

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.onExit(TestState::Idle, [&order](const StateCallbackContext<TestState, TestEvent> &) {
		order.push_back("exit");
	});
	machine.onEnter(
	    TestState::Connecting,
	    [&order](const StateCallbackContext<TestState, TestEvent> &) { order.push_back("enter"); }
	);
	machine.onTransition([&machine, &order](const TransitionContext<TestState, TestEvent> &) {
		order.push_back("observer");
		machine.end();
	});
	machine.onExit(TestState::Connecting, [&order](const StateCallbackContext<TestState, TestEvent> &ctx) {
		if (ctx.shutdown) {
			order.push_back("shutdown");
		}
	});

	machine.begin(TestState::Idle);
	order.clear();

	auto result = machine.dispatch(TestEvent::Start);
	expectTrue(result.ok(), "end from observer should still complete transition");
	expectFalse(machine.isStarted(), "end from observer should stop after dispatch completes");
	expectOrder(order, {"exit", "enter", "observer", "shutdown"}, "end from observer order");
}

void testEndDuringRejectedObserverDefersShutdown() {
	ESPStateMachine<TestState, TestEvent> machine;
	std::vector<std::string> order;

	machine.onRejected([&machine, &order](const RejectedEventContext<TestState, TestEvent> &) {
		order.push_back("rejected");
		machine.end();
	});
	machine.onExit(TestState::Idle, [&order](const StateCallbackContext<TestState, TestEvent> &ctx) {
		if (ctx.shutdown) {
			order.push_back("shutdown");
		}
	});

	machine.begin(TestState::Idle);
	auto result = machine.dispatch(TestEvent::Reset);

	expectEqual(
	    result.status,
	    StateMachineDispatchStatus::NoTransition,
	    "rejected observer dispatch should reject"
	);
	expectFalse(machine.isStarted(), "end from rejected observer should stop after observer completes");
	expectOrder(order, {"rejected", "shutdown"}, "end from rejected observer order");
}

void testBeginTwiceAndDispatchAfterEnd() {
	ESPStateMachine<TestState, TestEvent> machine;

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	expectTrue(machine.begin(TestState::Idle), "first begin should succeed");
	expectFalse(machine.begin(TestState::Online), "second begin should fail while started");

	machine.end();
	auto result = machine.dispatch(TestEvent::Start);
	expectEqual(
	    result.status,
	    StateMachineDispatchStatus::NotStarted,
	    "dispatch after end should be NotStarted"
	);
	expectFalse(result.transitioned, "dispatch after end should not transition");
}

void testInactiveCallbacksStayInert() {
	ESPStateMachine<TestState, TestEvent> machine;
	uint32_t enterCount = 0;
	uint32_t exitCount = 0;
	uint32_t transitionCount = 0;
	uint32_t rejectedCount = 0;

	const StateMachineCallbackId enterId = machine.onEnter(
	    TestState::Idle,
	    [&enterCount](const StateCallbackContext<TestState, TestEvent> &) { enterCount++; }
	);
	const StateMachineCallbackId exitId = machine.onExit(
	    TestState::Idle,
	    [&exitCount](const StateCallbackContext<TestState, TestEvent> &) { exitCount++; }
	);
	const StateMachineCallbackId transitionId = machine.onTransition(
	    [&transitionCount](const TransitionContext<TestState, TestEvent> &) { transitionCount++; }
	);
	const StateMachineCallbackId rejectedId = machine.onRejected(
	    [&rejectedCount](const RejectedEventContext<TestState, TestEvent> &) { rejectedCount++; }
	);

	expectTrue(machine.offCallback(enterId), "enter callback should deactivate");
	expectTrue(machine.offCallback(exitId), "exit callback should deactivate");
	expectTrue(machine.offCallback(transitionId), "transition observer should deactivate");
	expectTrue(machine.offCallback(rejectedId), "rejected observer should deactivate");

	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.begin(TestState::Idle);
	machine.dispatch(TestEvent::Start);
	machine.dispatch(TestEvent::Reset);

	expectEqual(enterCount, static_cast<uint32_t>(0), "inactive enter callback should stay inert");
	expectEqual(exitCount, static_cast<uint32_t>(0), "inactive exit callback should stay inert");
	expectEqual(transitionCount, static_cast<uint32_t>(0), "inactive transition observer should stay inert");
	expectEqual(rejectedCount, static_cast<uint32_t>(0), "inactive rejected observer should stay inert");
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

void testLoggerAttachFailureCanRetry() {
	ESPLogger logger;
	ESPStateMachine<TestState, TestEvent> machine;
	ESPStateMachineLoggerObserver<TestState, TestEvent> observer;

	logger.init();
	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);
	machine.begin(TestState::Idle);
	expectFalse(
	    observer.attach(machine, logger, {"TEST_FSM", stateName, eventName, true}),
	    "logger observer attach should fail while machine is started"
	);

	machine.end();
	expectTrue(
	    observer.attach(machine, logger, {"TEST_FSM", stateName, eventName, true}),
	    "logger observer should retry cleanly after failed attach"
	);

	machine.begin(TestState::Idle);
	machine.dispatch(TestEvent::Start);
	machine.dispatch(TestEvent::Reset);

	expectEqual(logger.infoCount, static_cast<uint32_t>(1), "retried logger should record transition");
	expectEqual(logger.warnCount, static_cast<uint32_t>(1), "retried logger should record rejection");
}

void testLoggerDetachAndReattach() {
	ESPLogger logger;
	ESPStateMachine<TestState, TestEvent> machine;
	ESPStateMachineLoggerObserver<TestState, TestEvent> observer;

	logger.init();
	machine.addTransition(TestState::Idle, TestEvent::Start, TestState::Connecting);

	expectTrue(
	    observer.attach(machine, logger, {"TEST_FSM", stateName, eventName, true}),
	    "logger observer should attach before detach"
	);
	observer.detach();
	expectTrue(
	    observer.attach(machine, logger, {"TEST_FSM", stateName, eventName, true}),
	    "logger observer should reattach after detach"
	);

	machine.begin(TestState::Idle);
	machine.dispatch(TestEvent::Start);
	observer.detach();
	machine.dispatch(TestEvent::Reset);

	expectEqual(logger.infoCount, static_cast<uint32_t>(1), "reattached logger should log transition once");
	expectEqual(logger.warnCount, static_cast<uint32_t>(0), "detached logger should not log rejection");
}

} // namespace

int main() {
	try {
		testBeginEndLifecycleCallbacks();
		testSimpleTransitionAndSnapshot();
		testNoTransitionAndGuardRejected();
		testGuardedBranchingOrder();
		testCallbackOrderAndReentrantBusy();
		testEndDuringActionDefersShutdown();
		testEndDuringEnterDefersShutdown();
		testEndDuringExitDefersShutdown();
		testEndDuringTransitionObserverDefersShutdown();
		testEndDuringRejectedObserverDefersShutdown();
		testBeginTwiceAndDispatchAfterEnd();
		testInactiveCallbacksStayInert();
		testEventBusBridgeDispatchesBoundEvent();
		testTimerBridgeTimeoutAndStaleCancel();
		testTimerBridgeFiresTimeout();
		testLoggerObserverLogsTransitionsAndRejections();
		testLoggerAttachFailureCanRetry();
		testLoggerDetachAndReattach();
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}

	std::cout << "ESPStateMachine tests passed\n";
	return 0;
}
