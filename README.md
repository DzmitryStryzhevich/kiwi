# KIWI OSAL Code Generator

KIWI is a code generator for building component-scoped Operating System Abstraction Layers (OSALs) for embedded software. Instead of forcing every module in a firmware project to depend on one large system-wide abstraction, KIWI generates a small OSAL contract tailored to the operating-system services that a particular component actually needs.

The project is intended to make OS dependencies explicit, reproducible and portable while keeping native RTOS/POSIX handles and implementation details outside the component. KIWI provides one code-generation core with both CLI and GUI frontends, reusable YAML generation profiles, and portable backend templates.

## Why OSAL and why component-scoped?

An OSAL isolates application or middleware code from a specific operating-system API. This makes it possible to keep one component-facing contract while mapping it to FreeRTOS, CMSIS-RTOS2, POSIX or another backend.

A component-scoped OSAL keeps that contract intentionally small: a component depends only on the primitives it uses, owns its OSAL instance, and does not need to carry native OS handles through its internal data structures. This reduces coupling and keeps the abstraction boundary useful for validation, tracing, diagnostics and controlled resource ownership.

For the architectural model and design rationale, see [`osal/README.md`](osal/README.md).

## Code generator

The generator turns a module prefix, target port, API selection and output-layout settings into a component-specific OSAL. The same generation core is used by the standalone CLI and GUI applications. Generation profiles can be saved to YAML and reused later, including for regenerating an existing component with a newer KIWI version.

For CLI options, GUI controls, generation profiles, output layouts and Windows executable builds, see [`generator/README.md`](generator/README.md).

## Supported ports

| Target | Language | Status | Notes |
| --- | --- | --- | --- |
| FreeRTOS | C | Implemented | Current working portable backend |
| POSIX | C | Planned | Portable backend scaffold exists |
| CMSIS-RTOS2 | C | Planned | Portable backend scaffold exists |
| C++ OSAL variant | C++ | Planned | C++ generation/port support is on the roadmap |

Additional ports are welcome as long as they preserve the component-scoped contract and common OSAL semantics.

## Testing

Automated test infrastructure is still planned. The intended test strategy covers generator-core tests, profile round-trips, deterministic/golden-file generation, CLI smoke tests, backend-specific build checks and portable-layer tests.

Current status and the temporary manual smoke-test procedure are described in [`test/README.md`](test/README.md).

## Contributing

Contributions are welcome, especially new portable backends, tests, generator improvements, documentation, bug fixes and carefully designed extensions to the OSAL API.

See [`CONTRIBUTE.md`](CONTRIBUTE.md) before preparing a contribution.

## License

KIWI is distributed under the MIT License. See [`LICENSE`](LICENSE).
