# KIWI OSAL Code Generator

KIWI is a code generator for building **component-scoped Operating System Abstraction Layers (OSALs)** for embedded software. Instead of making every module depend on one large system-wide abstraction, KIWI generates a small OS contract tailored to the operating-system services that a particular component actually needs.

The goal is not only portability between operating systems. A component-scoped OSAL gives a component one explicit and unambiguous vocabulary for OS primitives, keeps native RTOS/POSIX details outside the component, and creates a natural test boundary: the same component can be built and exercised on another platform with a test OSAL implementation, without changing the component itself.

## Why OSAL and why component-scoped?

Real operating-system APIs differ not only in types and function signatures, but also in terminology, lifecycle rules, timeout representation, error conventions and execution-context restrictions. The same conceptual operation may be called `create`, `new`, `init`, `delete`, `destroy`, `give`, `release`, `send`, `post`, `receive`, `get` or `pend` depending on the OS and its history. KIWI places a stable component-facing contract in front of those differences and defines common semantics that every portable backend must preserve.

Keeping that contract component-scoped makes it intentionally small and easier to reason about, validate, trace, port and test. It also prevents a project-wide OSAL from growing into a universal wrapper around every primitive exposed by every supported RTOS.

For the architectural model, unified primitive semantics, ownership rules and testability rationale, see [`osal/README.md`](osal/README.md).

## Code generator

The generator turns a module prefix, target port, API selection and output-layout settings into a component-specific OSAL. The same generation core is used by the standalone CLI and GUI applications, and YAML profiles make generation reproducible across KIWI versions.

For CLI options, GUI controls, generation profiles, output layouts and executable builds, see [`generator/README.md`](generator/README.md).

## Supported ports

| Target | Language | Status | Notes |
| --- | --- | --- | --- |
| FreeRTOS | C | Implemented | Current working portable backend |
| POSIX | C | Planned | Host/portable backend scaffold exists |
| CMSIS-RTOS2 | C | Planned | Portable backend scaffold exists |
| C++ OSAL variant | C++ | Planned | C++ generation/port support is on the roadmap |

Additional ports are welcome as long as they preserve the component-scoped contract and common OSAL semantics.

## Testing

Testing is a first-class use case for the component-scoped model. A production backend can be replaced by a host/test backend so the same component can be tested in an isolated and deterministic environment, including on a different platform from the final target.

Automated project test infrastructure is still planned. The intended strategy and current manual checks are described in [`test/README.md`](test/README.md).

## Contributing

Contributions are welcome, especially new portable backends, test infrastructure, examples, generator improvements, documentation, bug fixes and carefully designed extensions to the OSAL API.

See [`CONTRIBUTE.md`](CONTRIBUTE.md) before preparing a contribution.

## License

KIWI is distributed under the MIT License. See [`LICENSE`](LICENSE).
