# KIWI OSAL Code Generator

KIWI is a code generator for building **component-scoped Operating System Abstraction Layers (OSALs)** for embedded software. It generates a small OS-facing interface tailored to the needs of a particular component instead of forcing the whole project through one large system-wide abstraction.

The project is intended to make component code easier to port, test and maintain while keeping direct FreeRTOS, POSIX and other OS-specific dependencies behind generated OSAL boundaries.

## Why OSAL and why component-scoped?

A component-scoped OSAL gives a component a small, stable and unambiguous contract for the OS services it actually uses. It also provides a natural test boundary, allowing the same component to run against a production backend or an isolated host/test implementation.

For the architectural model, unified OS primitive semantics, ownership rules and testability rationale, see [`doc/osal_architecture_en.md`](doc/osal_architecture_en.md). A Russian version is available in [`doc/osal_architecture_ru.md`](doc/osal_architecture_ru.md).

## Code generator

KIWI provides both CLI and GUI frontends over the same code-generation core. YAML profiles can be used to save and reproduce generation settings.

For CLI options, GUI controls, profiles, output layouts and executable builds, see [`doc/kiwicgen.md`](doc/kiwicgen.md).

## Examples

Usage examples for the generated generic OSAL API are collected in [`doc/examples.md`](doc/examples.md). The examples are currently a placeholder and will be expanded as the API is stabilized.

## Supported ports

| Target | Language | Status | Notes |
| --- | --- | --- | --- |
| FreeRTOS | C | Implemented | Queues, stream buffers, mutexes, counting semaphores, event flags, threads including explicit `Yield`, deprecated system-level critical sections, software timers, time and memory |
| POSIX | C | Implemented | Queues, stream buffers, recursive mutexes, counting semaphores, event flags, threads including `Yield` and `DelayUntil`, native software timers, monotonic time and memory; thread suspend/resume and system-level critical sections are unsupported |
| C++ OSAL variant | C++ | Planned | C++ generation/port support is on the roadmap |

## Testing

KIWI has an automated OSAL verification harness based on GoogleTest/GoogleMock and CTest. Generic OSAL behavior is covered by isolated unit tests, the native POSIX backend is covered by behavioral integration tests, and the FreeRTOS backend is exercised against the official FreeRTOS `GCC_POSIX` host port. Generator profile checks are part of the same test entry point, and GitLab runs the suites as separate jobs with JUnit reports.

GitHub Actions additionally verifies full-set generation, real FreeRTOS/POSIX linking and static analysis. See [`test/README.md`](test/README.md) for local test commands and [`doc/testing.md`](doc/testing.md) for the complete verification model.

## Contributing

Contributions are welcome, including new ports, tests, examples, generator improvements, documentation and OSAL API extensions.

See [`CONTRIBUTE.md`](CONTRIBUTE.md) before preparing a contribution.

## License

KIWI is distributed under the MIT License. See [`LICENSE`](LICENSE).
