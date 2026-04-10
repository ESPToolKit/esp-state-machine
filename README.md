# ESPStateMachine

ESPStateMachine is a typed, flat finite-state machine helper for ESP32 firmware. It models runtime behavior after your modules are already initialized: device modes, connection flows, update phases, guarded recovery paths, and timeout-driven fault handling.

`ESPLifecycle` answers "what should initialize or deinitialize, and in what order?" ESPStateMachine answers "what runtime state is this feature in, and which event is allowed to move it next?"

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

## Testing
Host-side CMake tests cover the core and adapter compile behavior with small stubs:
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## License
MIT - see [LICENSE.md](LICENSE.md).

## ESPToolKit
- Repositories: <https://github.com/orgs/ESPToolKit/repositories>
- Website: <https://www.esptoolkit.hu/>
