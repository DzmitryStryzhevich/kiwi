# KIWI OSAL Code Generator

KIWI is a code generator for building **component-scoped Operating System Abstraction Layers (OSALs)** for embedded software. It generates a small OS-facing interface tailored to the needs of a particular component instead of forcing the whole project through one large system-wide abstraction.

The project is intended to make component code easier to port, test and maintain while keeping direct FreeRTOS, POSIX, CMSIS-RTOS2 and other OS-specific dependencies behind generated OSAL boundaries.

## Why OSAL and why component-scoped?

A component-scoped OSAL gives a component a small, stable and unambiguous contract for the OS services it actually uses. It also provides a natural test boundary, allowing the same component to run against a production backend or an isolated host/test implementation.

For the architectural model, unified OS primitive semantics, ownership rules and testability rationale, see [`osal/README.md`](osal/README.md).

## Code generator

KIWI provides both CLI and GUI frontends over the same code-generation core. YAML profiles can be used to save and reproduce generation settings.

For CLI options, GUI controls, profiles, output layouts and executable builds, see [`generator/README.md`](generator/README.md).

## Examples

Usage examples for the generated generic OSAL API are collected in [`examples/`](examples/). The examples are currently a placeholder and will be expanded as the API is stabilized.

## Supported ports

| Target | Language | Status | Notes |
| --- | --- | --- | --- |
| FreeRTOS | C | Implemented | Queues, stream buffers, locks, counting semaphores, threads, critical sections, software timers, time and memory |
| POSIX | C | Planned | Host/portable backend scaffold exists |
| CMSIS-RTOS2 | C | Planned | Portable backend scaffold exists |
| C++ OSAL variant | C++ | Planned | C++ generation/port support is on the roadmap |

## Testing

Automated test infrastructure is still planned. See [`test/README.md`](test/README.md) for the intended testing model and current manual checks.

## Contributing

Contributions are welcome, including new ports, tests, examples, generator improvements, documentation and OSAL API extensions.

See [`CONTRIBUTE.md`](CONTRIBUTE.md) before preparing a contribution.

## License

KIWI is distributed under the MIT License. See [`LICENSE`](LICENSE).
