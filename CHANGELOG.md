# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [0.1.0] - 2026-04-10

### Added

- Initial ESPStateMachine release.
- Typed enum-based finite state machine core with guarded transitions, transition actions, entry/exit callbacks, transition/rejection observers, snapshots, and reentrant dispatch protection.
- Optional adapter headers for ESPEventBus, ESPTimer, and ESPLogger integration.
- Example sketches for basic toggles, guarded branching, timeout-driven network flow, EventBus bridging, and Logger tracing.
- Host-side tests with adapter stubs plus CMake, Arduino, and PlatformIO package metadata.
