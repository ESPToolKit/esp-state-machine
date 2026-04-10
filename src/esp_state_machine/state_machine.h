#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

using StateMachineCallbackId = uint32_t;

enum class StateMachineDispatchStatus : uint8_t {
	Transitioned = 0,
	NoTransition,
	GuardRejected,
	NotStarted,
	Busy,
};

template <typename State, typename Event> struct TransitionContext {
	State from{};
	State to{};
	Event event{};
	void *payload = nullptr;
	uint32_t sequence = 0;
};

template <typename State, typename Event> struct StateCallbackContext {
	State state{};
	State otherState{};
	Event event{};
	void *payload = nullptr;
	uint32_t sequence = 0;
	bool bootstrap = false;
	bool shutdown = false;
};

template <typename State, typename Event> struct RejectedEventContext {
	State state{};
	Event event{};
	void *payload = nullptr;
	uint32_t sequence = 0;
	StateMachineDispatchStatus status = StateMachineDispatchStatus::NoTransition;
};

template <typename State, typename Event> struct StateMachineDispatchResult {
	StateMachineDispatchStatus status = StateMachineDispatchStatus::NotStarted;
	State from{};
	State to{};
	Event event{};
	void *payload = nullptr;
	uint32_t sequence = 0;
	bool transitioned = false;

	bool ok() const {
		return transitioned && status == StateMachineDispatchStatus::Transitioned;
	}
};

template <typename State, typename Event> struct StateMachineSnapshot {
	bool started = false;
	State currentState{};
	State previousState{};
	Event lastEvent{};
	StateMachineDispatchStatus lastStatus = StateMachineDispatchStatus::NotStarted;
	uint32_t lastSequence = 0;
};

template <typename State, typename Event> struct TransitionOptions {
	using Guard = std::function<bool(const TransitionContext<State, Event> &)>;
	using Action = std::function<void(const TransitionContext<State, Event> &)>;

	Guard guard;
	Action action;
};

template <typename State, typename Event> class ESPStateMachine {
	static_assert(std::is_enum<State>::value, "ESPStateMachine State must be an enum type");
	static_assert(std::is_enum<Event>::value, "ESPStateMachine Event must be an enum type");

  public:
	using TransitionGuard = typename TransitionOptions<State, Event>::Guard;
	using TransitionAction = typename TransitionOptions<State, Event>::Action;
	using StateCallback = std::function<void(const StateCallbackContext<State, Event> &)>;
	using TransitionObserver = std::function<void(const TransitionContext<State, Event> &)>;
	using RejectedObserver = std::function<void(const RejectedEventContext<State, Event> &)>;

	ESPStateMachine() = default;
	~ESPStateMachine() {
		end();
	}

	ESPStateMachine(const ESPStateMachine &) = delete;
	ESPStateMachine &operator=(const ESPStateMachine &) = delete;

	bool addTransition(
	    State from,
	    Event event,
	    State to,
	    TransitionOptions<State, Event> options = TransitionOptions<State, Event>{}
	) {
		if (started_ || dispatching_) {
			return false;
		}

		TransitionEntry entry{};
		entry.from = from;
		entry.event = event;
		entry.to = to;
		entry.guard = std::move(options.guard);
		entry.action = std::move(options.action);
		transitions_.push_back(std::move(entry));
		return true;
	}

	StateMachineCallbackId onEnter(State state, StateCallback callback) {
		return addStateCallback(enterCallbacks_, state, std::move(callback));
	}

	StateMachineCallbackId onExit(State state, StateCallback callback) {
		return addStateCallback(exitCallbacks_, state, std::move(callback));
	}

	StateMachineCallbackId onTransition(TransitionObserver callback) {
		if (!callback || started_ || dispatching_) {
			return 0;
		}

		TransitionObserverEntry entry{};
		entry.id = nextCallbackId_++;
		entry.callback = std::move(callback);
		entry.active = true;
		transitionObservers_.push_back(std::move(entry));
		return transitionObservers_.back().id;
	}

	StateMachineCallbackId onRejected(RejectedObserver callback) {
		if (!callback || started_ || dispatching_) {
			return 0;
		}

		RejectedObserverEntry entry{};
		entry.id = nextCallbackId_++;
		entry.callback = std::move(callback);
		entry.active = true;
		rejectedObservers_.push_back(std::move(entry));
		return rejectedObservers_.back().id;
	}

	bool offCallback(StateMachineCallbackId id) {
		if (id == 0 || dispatching_) {
			return false;
		}

		bool removed = deactivateStateCallback(enterCallbacks_, id);
		removed = deactivateStateCallback(exitCallbacks_, id) || removed;
		removed = deactivateTransitionObserver(id) || removed;
		removed = deactivateRejectedObserver(id) || removed;
		return removed;
	}

	bool begin(State initialState) {
		if (started_ || dispatching_) {
			return false;
		}

		currentState_ = initialState;
		previousState_ = initialState;
		lastStatus_ = StateMachineDispatchStatus::NotStarted;
		sequence_ = 0;
		started_ = true;

		dispatching_ = true;
		StateCallbackContext<State, Event> context{};
		context.state = initialState;
		context.otherState = initialState;
		context.bootstrap = true;
		invokeStateCallbacks(enterCallbacks_, initialState, context);
		dispatching_ = false;

		return true;
	}

	void end() {
		if (!started_ || dispatching_) {
			started_ = false;
			return;
		}

		dispatching_ = true;
		StateCallbackContext<State, Event> context{};
		context.state = currentState_;
		context.otherState = currentState_;
		context.sequence = sequence_;
		context.shutdown = true;
		invokeStateCallbacks(exitCallbacks_, currentState_, context);
		dispatching_ = false;

		started_ = false;
	}

	bool isStarted() const {
		return started_;
	}

	State currentState() const {
		return currentState_;
	}

	bool hasTransition(Event event) const {
		if (!started_) {
			return false;
		}

		for (const TransitionEntry &transition : transitions_) {
			if (transition.from == currentState_ && transition.event == event) {
				return true;
			}
		}
		return false;
	}

	StateMachineSnapshot<State, Event> snapshot() const {
		StateMachineSnapshot<State, Event> snap{};
		snap.started = started_;
		snap.currentState = currentState_;
		snap.previousState = previousState_;
		snap.lastEvent = lastEvent_;
		snap.lastStatus = lastStatus_;
		snap.lastSequence = sequence_;
		return snap;
	}

	StateMachineDispatchResult<State, Event> dispatch(Event event, void *payload = nullptr) {
		StateMachineDispatchResult<State, Event> result{};
		result.event = event;
		result.payload = payload;
		result.from = currentState_;
		result.to = currentState_;

		if (!started_) {
			result.status = StateMachineDispatchStatus::NotStarted;
			return result;
		}

		if (dispatching_) {
			result.status = StateMachineDispatchStatus::Busy;
			result.sequence = ++sequence_;
			setLastDispatch(event, result.status);
			return result;
		}

		dispatching_ = true;

		const State from = currentState_;
		const uint32_t sequence = ++sequence_;
		bool matched = false;

		result.from = from;
		result.to = from;
		result.sequence = sequence;

		for (const TransitionEntry &transition : transitions_) {
			if (transition.from != from || transition.event != event) {
				continue;
			}

			matched = true;

			TransitionContext<State, Event> context{};
			context.from = transition.from;
			context.to = transition.to;
			context.event = event;
			context.payload = payload;
			context.sequence = sequence;

			if (transition.guard && !transition.guard(context)) {
				continue;
			}

			StateCallbackContext<State, Event> exitContext{};
			exitContext.state = from;
			exitContext.otherState = transition.to;
			exitContext.event = event;
			exitContext.payload = payload;
			exitContext.sequence = sequence;
			invokeStateCallbacks(exitCallbacks_, from, exitContext);

			previousState_ = from;
			currentState_ = transition.to;

			if (transition.action) {
				transition.action(context);
			}

			StateCallbackContext<State, Event> enterContext{};
			enterContext.state = transition.to;
			enterContext.otherState = from;
			enterContext.event = event;
			enterContext.payload = payload;
			enterContext.sequence = sequence;
			invokeStateCallbacks(enterCallbacks_, transition.to, enterContext);
			invokeTransitionObservers(context);

			result.to = transition.to;
			result.status = StateMachineDispatchStatus::Transitioned;
			result.transitioned = true;
			setLastDispatch(event, result.status);
			dispatching_ = false;
			return result;
		}

		result.status =
		    matched ? StateMachineDispatchStatus::GuardRejected : StateMachineDispatchStatus::NoTransition;
		setLastDispatch(event, result.status);

		RejectedEventContext<State, Event> rejected{};
		rejected.state = from;
		rejected.event = event;
		rejected.payload = payload;
		rejected.sequence = sequence;
		rejected.status = result.status;
		invokeRejectedObservers(rejected);

		dispatching_ = false;
		return result;
	}

  private:
	struct TransitionEntry {
		State from{};
		Event event{};
		State to{};
		TransitionGuard guard;
		TransitionAction action;
	};

	struct StateCallbackEntry {
		StateMachineCallbackId id = 0;
		State state{};
		StateCallback callback;
		bool active = false;
	};

	struct TransitionObserverEntry {
		StateMachineCallbackId id = 0;
		TransitionObserver callback;
		bool active = false;
	};

	struct RejectedObserverEntry {
		StateMachineCallbackId id = 0;
		RejectedObserver callback;
		bool active = false;
	};

	StateMachineCallbackId
	addStateCallback(std::vector<StateCallbackEntry> &entries, State state, StateCallback callback) {
		if (!callback || started_ || dispatching_) {
			return 0;
		}

		StateCallbackEntry entry{};
		entry.id = nextCallbackId_++;
		entry.state = state;
		entry.callback = std::move(callback);
		entry.active = true;
		entries.push_back(std::move(entry));
		return entries.back().id;
	}

	static bool deactivateStateCallback(std::vector<StateCallbackEntry> &entries, StateMachineCallbackId id) {
		for (StateCallbackEntry &entry : entries) {
			if (entry.id == id && entry.active) {
				entry.active = false;
				return true;
			}
		}
		return false;
	}

	bool deactivateTransitionObserver(StateMachineCallbackId id) {
		for (TransitionObserverEntry &entry : transitionObservers_) {
			if (entry.id == id && entry.active) {
				entry.active = false;
				return true;
			}
		}
		return false;
	}

	bool deactivateRejectedObserver(StateMachineCallbackId id) {
		for (RejectedObserverEntry &entry : rejectedObservers_) {
			if (entry.id == id && entry.active) {
				entry.active = false;
				return true;
			}
		}
		return false;
	}

	void invokeStateCallbacks(
	    const std::vector<StateCallbackEntry> &entries,
	    State state,
	    const StateCallbackContext<State, Event> &context
	) {
		for (const StateCallbackEntry &entry : entries) {
			if (entry.active && entry.state == state && entry.callback) {
				entry.callback(context);
			}
		}
	}

	void invokeTransitionObservers(const TransitionContext<State, Event> &context) {
		for (const TransitionObserverEntry &entry : transitionObservers_) {
			if (entry.active && entry.callback) {
				entry.callback(context);
			}
		}
	}

	void invokeRejectedObservers(const RejectedEventContext<State, Event> &context) {
		for (const RejectedObserverEntry &entry : rejectedObservers_) {
			if (entry.active && entry.callback) {
				entry.callback(context);
			}
		}
	}

	void setLastDispatch(Event event, StateMachineDispatchStatus status) {
		lastEvent_ = event;
		lastStatus_ = status;
	}

	std::vector<TransitionEntry> transitions_{};
	std::vector<StateCallbackEntry> enterCallbacks_{};
	std::vector<StateCallbackEntry> exitCallbacks_{};
	std::vector<TransitionObserverEntry> transitionObservers_{};
	std::vector<RejectedObserverEntry> rejectedObservers_{};

	State currentState_{};
	State previousState_{};
	Event lastEvent_{};
	StateMachineDispatchStatus lastStatus_ = StateMachineDispatchStatus::NotStarted;
	uint32_t sequence_ = 0;
	StateMachineCallbackId nextCallbackId_ = 1;
	bool started_ = false;
	bool dispatching_ = false;
};
