#pragma once

#include <ESPTimer.h>

#include <vector>

#include "esp_state_machine/state_machine.h"

template <typename State, typename Event> class ESPStateMachineTimerBridge {
  public:
	ESPStateMachineTimerBridge() = default;
	~ESPStateMachineTimerBridge() {
		detach();
	}

	ESPStateMachineTimerBridge(const ESPStateMachineTimerBridge &) = delete;
	ESPStateMachineTimerBridge &operator=(const ESPStateMachineTimerBridge &) = delete;

	bool addTimeout(State state, uint32_t timeoutMs, Event timeoutEvent) {
		if (timeoutMs == 0) {
			return false;
		}

		for (TimeoutBinding &binding : bindings_) {
			if (binding.state == state) {
				binding.timeoutMs = timeoutMs;
				binding.timeoutEvent = timeoutEvent;
				return true;
			}
		}

		TimeoutBinding binding{};
		binding.state = state;
		binding.timeoutMs = timeoutMs;
		binding.timeoutEvent = timeoutEvent;
		bindings_.push_back(binding);

		if (machine_ != nullptr && !registerBinding(bindings_.back())) {
			bindings_.pop_back();
			return false;
		}

		return true;
	}

	bool attach(ESPStateMachine<State, Event> &machine, ESPTimer &timer) {
		if (machine_ != nullptr || timer_ != nullptr) {
			return false;
		}

		machine_ = &machine;
		timer_ = &timer;

		for (TimeoutBinding &binding : bindings_) {
			if (!registerBinding(binding)) {
				detach();
				return false;
			}
		}
		return true;
	}

	void detach() {
		cancelActiveTimeout();

		if (machine_ != nullptr) {
			for (TimeoutBinding &binding : bindings_) {
				if (binding.enterCallbackId != 0) {
					(void)machine_->offCallback(binding.enterCallbackId);
					binding.enterCallbackId = 0;
				}
				if (binding.exitCallbackId != 0) {
					(void)machine_->offCallback(binding.exitCallbackId);
					binding.exitCallbackId = 0;
				}
			}
		}

		machine_ = nullptr;
		timer_ = nullptr;
	}

  private:
	struct TimeoutBinding {
		State state{};
		uint32_t timeoutMs = 0;
		Event timeoutEvent{};
		StateMachineCallbackId enterCallbackId = 0;
		StateMachineCallbackId exitCallbackId = 0;
	};

	bool registerBinding(TimeoutBinding &binding) {
		if (machine_ == nullptr) {
			return false;
		}

		if (binding.enterCallbackId == 0) {
			binding.enterCallbackId = machine_->onEnter(
			    binding.state,
			    [this](const StateCallbackContext<State, Event> &context) {
				    armTimeoutForState(context.state);
			    }
			);
		}

		if (binding.exitCallbackId == 0) {
			binding.exitCallbackId = machine_->onExit(
			    binding.state,
			    [this](const StateCallbackContext<State, Event> &context) {
				    (void)context;
				    cancelActiveTimeout();
			    }
			);
		}

		return binding.enterCallbackId != 0 && binding.exitCallbackId != 0;
	}

	TimeoutBinding *findBinding(State state) {
		for (TimeoutBinding &binding : bindings_) {
			if (binding.state == state) {
				return &binding;
			}
		}
		return nullptr;
	}

	void armTimeoutForState(State state) {
		if (machine_ == nullptr || timer_ == nullptr) {
			return;
		}

		TimeoutBinding *binding = findBinding(state);
		if (binding == nullptr) {
			return;
		}

		cancelActiveTimeout();
		activeTimeoutId_ = timer_->setTimeout(
		    [this, state, event = binding->timeoutEvent]() {
			    activeTimeoutId_ = 0;
			    if (machine_ != nullptr && machine_->isStarted() && machine_->currentState() == state) {
				    (void)machine_->dispatch(event, nullptr);
			    }
		    },
		    binding->timeoutMs
		);
	}

	void cancelActiveTimeout() {
		if (timer_ != nullptr && activeTimeoutId_ != 0) {
			(void)timer_->clearTimeout(activeTimeoutId_);
		}
		activeTimeoutId_ = 0;
	}

	ESPStateMachine<State, Event> *machine_ = nullptr;
	ESPTimer *timer_ = nullptr;
	std::vector<TimeoutBinding> bindings_{};
	uint32_t activeTimeoutId_ = 0;
};
