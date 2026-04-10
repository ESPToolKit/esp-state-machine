#pragma once

#include <cstdint>
#include <functional>
#include <vector>

class ESPTimer {
  public:
	uint32_t setTimeout(std::function<void()> cb, uint32_t delayMs) {
		Timeout timeout{};
		timeout.id = ++nextId_;
		timeout.cb = std::move(cb);
		timeout.delayMs = delayMs;
		timeout.active = true;
		timeouts_.push_back(std::move(timeout));
		lastTimeoutId = timeouts_.back().id;
		return timeouts_.back().id;
	}

	bool clearTimeout(uint32_t id) {
		for (Timeout &timeout : timeouts_) {
			if (timeout.id == id && timeout.active) {
				timeout.active = false;
				clearCount++;
				return true;
			}
		}
		return false;
	}

	bool fire(uint32_t id) {
		for (Timeout &timeout : timeouts_) {
			if (timeout.id == id && timeout.active && timeout.cb) {
				timeout.active = false;
				timeout.cb();
				return true;
			}
		}
		return false;
	}

	uint32_t lastTimeoutId = 0;
	uint32_t clearCount = 0;

  private:
	struct Timeout {
		uint32_t id = 0;
		std::function<void()> cb;
		uint32_t delayMs = 0;
		bool active = false;
	};

	uint32_t nextId_ = 0;
	std::vector<Timeout> timeouts_{};
};
