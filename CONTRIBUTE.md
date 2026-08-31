# Contributing to KIWI

Contributions are welcome. KIWI is still evolving, so changes should preserve the central architectural goals: component-scoped OSAL contracts, deterministic generation, a shared code-generation core, portable backend isolation, explicit resource ownership, and consistent validation/tracing behavior.

## Useful contribution areas

Good contribution candidates include:

- new portable OSAL backends;
- completion and maintenance of POSIX and CMSIS-RTOS2 ports;
- future C++ generation/port support;
- additional automated tests and CI coverage;
- generator-core improvements;
- CLI and GUI usability improvements;
- YAML profile compatibility/migration support;
- bug fixes;
- documentation and examples;
- carefully designed new OSAL primitive groups.

## Before changing the OSAL API

Avoid adding a primitive only because one backend exposes it. A new generic API group should have clear component-level semantics that can be represented consistently across the intended backends.

When introducing a new primitive group, update the whole vertical slice rather than only one file:

- generic OSAL types and API;
- resource registry/bookkeeping where required;
- method tables;
- portable backend implementation;
- lifecycle cleanup;
- tracing and assertions;
- execution-context validation;
- generator markers/configuration;
- YAML profile schema handling if required;
- CLI/UI selection;
- documentation;
- tests.

## Adding a new port

A new port should preserve the generic OSAL contract rather than leaking native backend behavior into the component API.

A backend is expected to provide:

- backend-specific OSAL instance state;
- generic base object as part of the backend instance model;
- backend validation/lifecycle state;
- mapping of generic timeout and priority semantics;
- internal resource-registry synchronization;
- deterministic cleanup of OSAL-owned resources where possible;
- consistent assertion and tracing integration;
- common source layout, naming and Doxygen style.

Backend-internal synchronization objects must remain separate from component-visible resources.

## Generator architecture

Generation logic belongs in:

```text
generator/kiwi_codegen.py
```

The two applications:

```text
generator/kiwi_codegen_cli_app.py
generator/kiwi_codegen_ui_app.py
```

should remain thin frontends to the same core. Do not implement a generation rule independently in the GUI and CLI.

When adding a generator option, keep CLI, GUI and YAML profiles aligned wherever the option is intended to be reproducible.

## Tests

New behavior should ideally include tests. The automated test suite is still planned, so contributions that establish or expand that infrastructure are especially useful.

See [`test/README.md`](test/README.md) for the planned coverage model.

For bug fixes, add a regression test when the relevant test infrastructure exists. For new ports, include at least build/syntax coverage and focused tests for lifecycle, resource handling and backend-specific semantics.

## Documentation

Update documentation when behavior, profile format, CLI options, GUI controls, supported ports or generated output layouts change.

Keep the root `README.md` concise. Detailed OSAL architecture belongs in [`osal/README.md`](osal/README.md), while generator usage belongs in [`generator/README.md`](generator/README.md).

## Change scope

Prefer focused changes. Avoid mixing a broad OSAL redesign, unrelated GUI work, formatting-only changes and a new backend into one contribution unless those changes are inseparable.

Generated-source behavior should remain deterministic for the same templates and normalized configuration.

## Code style

Follow the style already established in the surrounding source. Portable backends should look like parts of one codebase rather than unrelated vendor examples.

In particular:

- keep naming and file structure consistent across ports;
- keep Doxygen/comment style consistent;
- keep validation and error handling systematic;
- avoid backend-specific types in the generic component-facing contract;
- do not introduce hidden global state when state belongs to an OSAL instance;
- preserve configurable tracing/assertion hooks.

## Submitting changes

Before submitting a change:

1. run the available syntax/build checks relevant to the modified files;
2. verify CLI help if CLI options changed;
3. verify profile load/save if profile behavior changed;
4. verify affected generation layouts;
5. update the appropriate README/documentation;
6. keep the change focused and explain the architectural reason for it in the pull/merge request.

If a contribution changes generic OSAL semantics or the profile schema, describe compatibility impact explicitly.
