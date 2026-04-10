#pragma once

#include <cstdint>
#include <functional>
#include <vector>

using EventBusId = uint16_t;
using EventBusSub = uint32_t;

class ESPEventBus {
  public:
	using EventCallback = std::function<void(void *, void *)>;

	EventBusSub
	subscribe(EventBusId id, EventCallback cb, void *userArg = nullptr, bool oneshot = false) {
		Subscription sub{};
		sub.subId = ++nextSubId_;
		sub.id = id;
		sub.cb = std::move(cb);
		sub.userArg = userArg;
		sub.oneshot = oneshot;
		sub.active = true;
		subs_.push_back(std::move(sub));
		return subs_.back().subId;
	}

	void unsubscribe(EventBusSub subId) {
		for (Subscription &sub : subs_) {
			if (sub.subId == subId) {
				sub.active = false;
			}
		}
	}

	void emit(EventBusId id, void *payload = nullptr) {
		for (Subscription &sub : subs_) {
			if (sub.active && sub.id == id && sub.cb) {
				sub.cb(payload, sub.userArg);
				if (sub.oneshot) {
					sub.active = false;
				}
			}
		}
	}

  private:
	struct Subscription {
		EventBusSub subId = 0;
		EventBusId id = 0;
		EventCallback cb;
		void *userArg = nullptr;
		bool oneshot = false;
		bool active = false;
	};

	EventBusSub nextSubId_ = 0;
	std::vector<Subscription> subs_{};
};
