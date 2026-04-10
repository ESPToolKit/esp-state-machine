# ESPStateMachine

ESPStateMachine is a typed, flat finite-state machine helper for ESP32 firmware. It models runtime behavior after your modules are already initialized: device modes, connection flows, update phases, guarded recovery paths, and timeout-driven fault handling.

`ESPLifecycle` answers "what should initialize or deinitialize, and in what order?" ESPStateMachine answers "what runtime state is this feature in, and which event is allowed to move it next?"

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-state-machine/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-state-machine/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-state-machine?sort=semver)](https://github.com/ESPToolKit/esp-state-machine/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- Typed enum states and events via `ESPStateMachine<State, Event>`.
- Flat finite-state machine with one active state.
- Guarded transitions evaluated in registration order.
- Entry, exit, transition, and rejection callbacks.
- Caller-owned `void*` payload passthrough for event context.
- Snapshot API for diagnostics.
- Reentrant dispatch protection with deterministic `Busy` result.
- Header-only core with no mandatory ESPToolKit dependencies.
- Optional adapter headers for `ESPEventBus`, `ESPTimer`, and `ESPLogger`.

## Installation
- PlatformIO: add `https://github.com/ESPToolKit/esp-state-machine.git` to `lib_deps`.
- Arduino IDE: install as ZIP from this repository.

Optional adapters require their matching libraries:
- `esp_state_machine/adapters/eventbus_bridge.h` requires `ESPEventBus`.
- `esp_state_machine/adapters/timer_bridge.h` requires `ESPTimer`.
- `esp_state_machine/adapters/logger_observer.h` requires `ESPLogger`.

## Single Include
```cpp
#include <ESPStateMachine.h>
```

## Quick Start
```cpp
#include <ESPStateMachine.h>

enum class DeviceState : uint8_t {
    Idle,
    Connecting,
    Online,
    Fault,
};

enum class DeviceEvent : uint8_t {
    Start,
    Connected,
    Timeout,
    Reset,
};

ESPStateMachine<DeviceState, DeviceEvent> machine;

void setup() {
    Serial.begin(115200);

    machine.addTransition(DeviceState::Idle, DeviceEvent::Start, DeviceState::Connecting);
    machine.addTransition(DeviceState::Connecting, DeviceEvent::Connected, DeviceState::Online);
    machine.addTransition(DeviceState::Connecting, DeviceEvent::Timeout, DeviceState::Fault);
    machine.addTransition(DeviceState::Fault, DeviceEvent::Reset, DeviceState::Idle);

    machine.onTransition([](const TransitionContext<DeviceState, DeviceEvent>& ctx) {
        Serial.printf("transition seq=%lu\n", static_cast<unsigned long>(ctx.sequence));
    });

    machine.begin(DeviceState::Idle);
    machine.dispatch(DeviceEvent::Start);
}

void loop() {}
```

## Transition Selection
- Multiple transitions for the same `(state, event)` are allowed.
- Matching transitions are evaluated in registration order.
- The first transition with no guard or a passing guard is taken.
- If no transition matches the current state/event pair, `dispatch(...)` returns `NoTransition`.
- If transitions match but all guards return false, `dispatch(...)` returns `GuardRejected`.
- Dispatch from inside callbacks returns `Busy`; queue follow-up work through `ESPEventBus` or your own scheduler.

## Callback Order
For a successful dispatch:
1. Exit callbacks for the source state.
2. Internal current state updates to the target state.
3. Transition action, if configured.
4. Entry callbacks for the target state.
5. Machine-level transition observers.

`begin(initialState)` invokes entry callbacks for the initial state with `bootstrap=true`. `end()` invokes exit callbacks for the current state with `shutdown=true`.

## Optional Adapters
### EventBus Bridge
```cpp
#include <ESPEventBus.h>
#include <ESPStateMachine.h>
#include <esp_state_machine/adapters/eventbus_bridge.h>

ESPEventBus bus;
ESPStateMachine<DeviceState, DeviceEvent> machine;
ESPStateMachineEventBusBridge<DeviceState, DeviceEvent> bridge;

void setup() {
    bus.init();
    bridge.attach(bus, machine);
    bridge.bind(42, DeviceEvent::Connected);
}
```

### Timer Bridge
```cpp
#include <ESPTimer.h>
#include <ESPStateMachine.h>
#include <esp_state_machine/adapters/timer_bridge.h>

ESPTimer timer;
ESPStateMachineTimerBridge<DeviceState, DeviceEvent> timeouts;

void setup() {
    timer.init();
    timeouts.addTimeout(DeviceState::Connecting, 5000, DeviceEvent::Timeout);
    timeouts.attach(machine, timer);
}
```

### Logger Observer
```cpp
#include <ESPLogger.h>
#include <ESPStateMachine.h>
#include <esp_state_machine/adapters/logger_observer.h>

const char* stateName(DeviceState state);
const char* eventName(DeviceEvent event);

ESPLogger logger;
ESPStateMachineLoggerObserver<DeviceState, DeviceEvent> trace;

void setup() {
    logger.init();
    trace.attach(machine, logger, {
        "DEVICE_FSM",
        stateName,
        eventName,
        true,
    });
}
```

## Non-goals In v1
- No hierarchical or nested states.
- No parallel regions.
- No built-in persistence.
- No runtime string state/event IDs.
- No worker task inside the core.
- No payload ownership or serialization.

## Examples
- `examples/basic_toggle`
- `examples/guarded_branching`
- `examples/network_timeout`
- `examples/eventbus_bridge`
- `examples/logger_trace`

## API Summary
- `bool begin(State initialState)` / `void end()` / `bool isStarted() const`
- `bool addTransition(State from, Event event, State to, TransitionOptions<State, Event> options = {})`
- `StateMachineDispatchResult<State, Event> dispatch(Event event, void* payload = nullptr)`
- `State currentState() const` / `StateMachineSnapshot<State> snapshot() const`
- `bool hasTransition(Event event) const`
- `StateMachineCallbackId onEnter(State state, StateCallback callback)`
- `StateMachineCallbackId onExit(State state, StateCallback callback)`
- `StateMachineCallbackId onTransition(TransitionObserver callback)`
- `StateMachineCallbackId onRejected(RejectedObserver callback)`
- `bool offCallback(StateMachineCallbackId callbackId)`

## Standalone CMake
```cmake
include(FetchContent)

FetchContent_Declare(
  esp_state_machine
  GIT_REPOSITORY https://github.com/ESPToolKit/esp-state-machine.git
  GIT_TAG main
)

FetchContent_MakeAvailable(esp_state_machine)

target_link_libraries(your_target PRIVATE ESPStateMachine::esp_state_machine)
```

If the source is vendored locally, `add_subdirectory(path/to/esp-state-machine)` exposes the same `ESPStateMachine::esp_state_machine` target.

## Tests
- Host-side tests in `test/test_esp_state_machine` cover the core and adapter compile behavior with small stubs.
- CI also builds the Arduino examples through both PlatformIO and Arduino CLI on the standard ESP32 board matrix.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Formatting Baseline

This repository follows the firmware formatting baseline from `esptoolkit-template`:
- `.clang-format` is the source of truth for C/C++/INO layout.
- `.editorconfig` enforces tabs (`tab_width = 4`), LF endings, and final newline.
- Format all tracked firmware sources with `bash scripts/format_cpp.sh`.

## License
MIT - see [LICENSE.md](LICENSE.md).

## ESPToolKit
- Check out other libraries: <https://github.com/orgs/ESPToolKit/repositories>
- Hang out on Discord: <https://discord.gg/WG8sSqAy>
- Support the project: <https://ko-fi.com/esptoolkit>
- Visit the website: <https://www.esptoolkit.hu/>
