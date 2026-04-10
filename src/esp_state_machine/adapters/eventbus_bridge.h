#pragma once

#include <ESPEventBus.h>

#include <vector>

#include "esp_state_machine/state_machine.h"

template <typename State, typename Event> class ESPStateMachineEventBusBridge {
  public:
	ESPStateMachineEventBusBridge() = default;
	~ESPStateMachineEventBusBridge() {
		detach();
	}

	ESPStateMachineEventBusBridge(const ESPStateMachineEventBusBridge &) = delete;
	ESPStateMachineEventBusBridge &operator=(const ESPStateMachineEventBusBridge &) = delete;

	bool attach(ESPEventBus &bus, ESPStateMachine<State, Event> &machine) {
		if (bus_ != nullptr || machine_ != nullptr) {
			return false;
		}
		bus_ = &bus;
		machine_ = &machine;
		return true;
	}

	void detach() {
		if (bus_ != nullptr) {
			for (EventBusSub subId : subscriptions_) {
				if (subId != 0) {
					bus_->unsubscribe(subId);
				}
			}
		}
		subscriptions_.clear();
		bus_ = nullptr;
		machine_ = nullptr;
	}

	EventBusSub bind(EventBusId busId, Event event) {
		if (bus_ == nullptr || machine_ == nullptr) {
			return 0;
		}

		EventBusSub subId = bus_->subscribe(
		    busId,
		    [this, event](void *payload, void *) {
			    if (machine_ != nullptr) {
				    (void)machine_->dispatch(event, payload);
			    }
		    },
		    nullptr,
		    false
		);

		if (subId != 0) {
			subscriptions_.push_back(subId);
		}
		return subId;
	}

  private:
	ESPEventBus *bus_ = nullptr;
	ESPStateMachine<State, Event> *machine_ = nullptr;
	std::vector<EventBusSub> subscriptions_{};
};
