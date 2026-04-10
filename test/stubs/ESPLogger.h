#pragma once

#include <cstdarg>
#include <cstdint>

class ESPLogger {
  public:
	bool init() {
		initialized_ = true;
		return true;
	}

	bool isInitialized() const {
		return initialized_;
	}

	void info(const char *, const char *, ...) {
		infoCount++;
	}

	void warn(const char *, const char *, ...) {
		warnCount++;
	}

	uint32_t infoCount = 0;
	uint32_t warnCount = 0;

  private:
	bool initialized_ = false;
};
