# KIWI Development Rules

## Project Context

Before modifying the project, analyze the available README files at all relevant levels of the repository.

Read the documentation hierarchically from the repository root down to the affected module or subsystem and use it to understand:

- the overall project architecture;
- module responsibilities and boundaries;
- directory structure;
- supported and planned features;
- build and generation workflow;
- platform-specific limitations;
- extension points and abstraction layers;
- testing strategy;
- documentation hierarchy;
- project-specific terminology and conventions.

Do not start implementation based only on the target source file when relevant architectural documentation is available.

Treat the README hierarchy as architectural context and preserve the documented design unless the requested task explicitly requires changing it.

Before editing source files, inspect relevant `TODO`, `FIXME` and similar in-code notes. Treat them as explicit implementation/design context: resolve them when they are in scope, or preserve them deliberately when the requested change does not address them. Do not silently ignore or remove such notes.

## Code Style

When modifying or extending the project, preserve the existing code style.

New code must look as if it had originally been written together with the surrounding reference code.

Requirements:

- Follow the formatting, indentation, naming, spacing, line wrapping, declaration style and ordering already used in the surrounding code.
- Preserve the existing file structure and ordering of sections.
- Preserve the existing style of section separators and local comments.
- Public interfaces, structures, types, callbacks, macros and other documented entities must use Doxygen comments consistent with the surrounding code.
- Doxygen comments must include all applicable `\\brief`, `\\param`, `\\return`, `\\note`, `\\details` and other fields used by the surrounding code.
- Function implementations must preserve the existing internal commentary structure, including argument validation, invariant checks, execution-context checks, backend delegation, resource handling, tracing and return handling where applicable.
- Keep multiline function declarations, calls, conditions and initializers aligned consistently with the surrounding code.
- Keep exactly two empty lines between function definitions.
- Do not compact existing code merely to reduce line count.
- Do not remove comments, Doxygen documentation, assertions, tracing or formatting unless explicitly requested.
- Do not introduce a new formatting style into an existing file.
- Prefer consistency with the existing project code over personal style preferences or generic external style guides.

## Assertions and Invariants

Use assertions to verify programmer-controlled invariants and conditions that must already be guaranteed by the layer calling the current function.

Examples include:

- non-NULL internal pointers that are guaranteed by the generic API;
- initialized backend state before entering a private backend method;
- required `vtable` and `ptable` entries;
- registry indices known to be within configured bounds;
- internal configuration values already validated by the public/generic layer;
- impossible internal states and broken ownership assumptions.

Do not duplicate a generic-layer runtime validation as another backend runtime branch when the condition is an internal invariant. Assert it instead.

Keep runtime error handling for conditions that can legitimately occur during normal execution, including native OS failures, resource exhaustion, timeout/full/empty results, unsupported execution context when it may be reached through the public contract, and other recoverable operational failures.

When an invariant violation can still cause unsafe behavior in release builds after assertions are compiled out, retain the minimum defensive handling required to prevent undefined behavior.

## Tracing

Preserve the tracing model used by the surrounding code.

For functions where equivalent existing code is traced:

- trace function entry and relevant input arguments;
- trace every returned error status;
- trace successful completion or the final returned status;
- trace important invariant failures before leaving a defensive release-build path;
- keep trace formatting and casts consistent with nearby functions.

Do not add new code paths that silently bypass tracing when equivalent existing paths are traced.

## Function Exit Markers

Every explicit function exit must be annotated using the existing exit-comment convention.

Use one of the following forms:

```c
return status;  // Exit: Error: <details>
return status;  // Exit: Success: <details>
return;         // Exit: Error: <details>
return;         // Exit: Success: <details>
```

The comment must describe the actual reason for that exit point. Do not use vague comments such as `Exit`, `done` or `return status` when a more specific reason is known.

If a function currently returns a summary status that may represent either success or failure, prefer structuring the error path explicitly so that each exit can be classified as either `Error` or `Success`.

## OSAL Extension Rules

- Preserve the component-scoped ownership model documented in `osal/README.md`.
- New generic OSAL operations must express component-level semantics rather than copy native RTOS vocabulary mechanically.
- Resource-backed primitives must follow the existing registry ownership model.
- Backend-only registry helpers belong in the protected methods table when they operate on generic OSAL-owned registry state.
- Backend ports must use the generic `vtable` contract and protected `ptable` helpers consistently with existing primitives.
- Do not expose backend-native handles or implementation details to component code beyond the established opaque-handle contract.
