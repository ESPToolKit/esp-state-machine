#pragma once

#include <ESPLogger.h>

#include "esp_state_machine/state_machine.h"

template <typename State, typename Event> struct ESPStateMachineLoggerOptions {
	const char *tag = "ESPStateMachine";
	const char *(*stateToString)(State) = nullptr;
	const char *(*eventToString)(Event) = nullptr;
	bool logRejected = true;
};

template <typename State, typename Event> class ESPStateMachineLoggerObserver {
  public:
	ESPStateMachineLoggerObserver() = default;
	~ESPStateMachineLoggerObserver() {
		detach();
	}

	ESPStateMachineLoggerObserver(const ESPStateMachineLoggerObserver &) = delete;
	ESPStateMachineLoggerObserver &operator=(const ESPStateMachineLoggerObserver &) = delete;

	bool attach(
	    ESPStateMachine<State, Event> &machine,
	    ESPLogger &logger,
	    ESPStateMachineLoggerOptions<State, Event> options = {}
	) {
		if (machine_ != nullptr) {
			return false;
		}

		machine_ = &machine;
		logger_ = &logger;
		options_ = options;
		if (options_.tag == nullptr || options_.tag[0] == '\0') {
			options_.tag = "ESPStateMachine";
		}

		transitionCallbackId_ = machine_->onTransition(
		    [this](const TransitionContext<State, Event> &context) { logTransition(context); }
		);

		if (options_.logRejected) {
			rejectedCallbackId_ = machine_->onRejected(
			    [this](const RejectedEventContext<State, Event> &context) { logRejected(context); }
			);
		}

		if (transitionCallbackId_ == 0 || (options_.logRejected && rejectedCallbackId_ == 0)) {
			detach();
			return false;
		}

		return true;
	}

	void detach() {
		if (machine_ != nullptr) {
			if (transitionCallbackId_ != 0) {
				(void)machine_->offCallback(transitionCallbackId_);
			}
			if (rejectedCallbackId_ != 0) {
				(void)machine_->offCallback(rejectedCallbackId_);
			}
		}

		transitionCallbackId_ = 0;
		rejectedCallbackId_ = 0;
		machine_ = nullptr;
		logger_ = nullptr;
		options_ = ESPStateMachineLoggerOptions<State, Event>{};
	}

  private:
	const char *stateName(State state) const {
		if (options_.stateToString == nullptr) {
			return "?";
		}
		const char *name = options_.stateToString(state);
		return name != nullptr ? name : "?";
	}

	const char *eventName(Event event) const {
		if (options_.eventToString == nullptr) {
			return "?";
		}
		const char *name = options_.eventToString(event);
		return name != nullptr ? name : "?";
	}

	static const char *statusName(StateMachineDispatchStatus status) {
		switch (status) {
		case StateMachineDispatchStatus::Transitioned:
			return "transitioned";
		case StateMachineDispatchStatus::NoTransition:
			return "no_transition";
		case StateMachineDispatchStatus::GuardRejected:
			return "guard_rejected";
		case StateMachineDispatchStatus::NotStarted:
			return "not_started";
		case StateMachineDispatchStatus::Busy:
			return "busy";
		}
		return "unknown";
	}

	void logTransition(const TransitionContext<State, Event> &context) {
		if (logger_ == nullptr) {
			return;
		}
		logger_->info(
		    options_.tag,
		    "transition seq=%lu from=%s event=%s to=%s",
		    static_cast<unsigned long>(context.sequence),
		    stateName(context.from),
		    eventName(context.event),
		    stateName(context.to)
		);
	}

	void logRejected(const RejectedEventContext<State, Event> &context) {
		if (logger_ == nullptr) {
			return;
		}
		logger_->warn(
		    options_.tag,
		    "rejected seq=%lu state=%s event=%s status=%s",
		    static_cast<unsigned long>(context.sequence),
		    stateName(context.state),
		    eventName(context.event),
		    statusName(context.status)
		);
	}

	ESPStateMachine<State, Event> *machine_ = nullptr;
	ESPLogger *logger_ = nullptr;
	ESPStateMachineLoggerOptions<State, Event> options_{};
	StateMachineCallbackId transitionCallbackId_ = 0;
	StateMachineCallbackId rejectedCallbackId_ = 0;
};
