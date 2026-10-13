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

- Preserve the component-scoped ownership model documented in `doc/osal_architecture_en.md`.
- New generic OSAL operations must express component-level semantics rather than copy native RTOS vocabulary mechanically.
- Resource-backed primitives must follow the existing registry ownership model.
- Backend-only registry helpers belong in the protected methods table when they operate on generic OSAL-owned registry state.
- Backend ports must use the generic `vtable` contract and protected `ptable` helpers consistently with existing primitives.
- Do not expose backend-native handles or implementation details to component code beyond the established opaque-handle contract.

## Testing and Verification Rules

Testing is part of feature design, not a follow-up activity.

Every new feature, OSAL primitive, backend capability, generator extension or higher-level abstraction must be designed together with the tests that verify it. Before implementation is considered complete, determine:

- what behavior must be verified;
- which test level owns each verification;
- which positive, negative, lifecycle, timeout and concurrency scenarios are required;
- whether the existing test harness must be extended;
- whether CMake/CTest or CI integration must be updated;
- whether the change affects more than one backend or generated configuration.

A feature is not considered fully designed until its verification strategy is defined. When automated verification is not practical, document the reason explicitly instead of silently omitting tests.

### Test Levels

Keep generic unit tests and backend integration tests conceptually separate.

Generic OSAL unit tests verify the generic API contract in isolation through the mock-backed `vtable`/`ptable` harness. They should cover, as applicable:

- input validation;
- initialized and non-initialized states;
- generic lifecycle behavior;
- registry and ownership logic implemented by the generic layer;
- correct backend dispatch;
- argument forwarding;
- backend-status propagation;
- generic handle and resource-slot behavior.

Do not use generic unit tests to prove native backend behavior such as real blocking, wake-up, scheduling, timeout, timer expiration or thread interaction.

Backend integration tests verify observable behavior using the real backend implementation and its real native primitives. They should cover, as applicable:

- blocking and wake-up behavior;
- timeout behavior;
- producer/consumer or waiter/notifier interaction;
- ordering and data integrity;
- lifecycle behavior observable through the real backend;
- concurrent access;
- timer expiration and restart behavior;
- backend-specific supported and unsupported semantics.

Do not duplicate generic unit-test validation matrices in integration tests. Generic API calls may be asserted in integration tests as scenario assumptions or setup preconditions, but the purpose of an integration test must be real backend behavior.

For the FreeRTOS backend host integration suite, use the FreeRTOS POSIX port so the tests exercise the actual KIWI FreeRTOS backend together with the real FreeRTOS kernel. Do not replace that layer with mocked FreeRTOS API calls when the goal is backend integration verification.

Generator behavior must be tested at the generator/integration level when a change affects generated API, primitive selection, template filtering, backend emission or generated buildability.

### Test Executables and Naming

Keep unit and integration test environments in separate executables. Prefer one executable per test level/runtime environment rather than one executable per primitive.

The expected top-level split is:

```text
kiwi_osal_unit_test
kiwi_osal_posix_it_test
kiwi_osal_freertos_posix_it_test
```

Integration-test names must always identify both the port and the fact that the test is an integration test, even when the containing directory already implies the port.

Use file names following this form:

```text
osal_<port>_it_test_<primitive>_gtest.cpp
```

For example:

```text
osal_posix_it_test_queue_gtest.cpp
osal_posix_it_test_software_timer_gtest.cpp
osal_freertos_posix_it_test_queue_gtest.cpp
osal_freertos_posix_it_test_software_timer_gtest.cpp
```

Generic unit-test files should follow this form:

```text
osal_unit_test_<primitive>_gtest.cpp
```

Test fixtures and suites must also make the runtime environment explicit, for example `OsalPosixItTest` and `OsalFreertosPosixItTest`.

### Test Isolation and Fixtures

Tests must be independent of execution order and of state left by neighboring tests.

Use GoogleTest fixtures with `SetUp()` and `TearDown()` whenever practical. Initialization and deinitialization of the OSAL instance used by a test should normally be owned by the fixture.

Each test should receive fresh test state, including as applicable:

- a newly initialized OSAL instance;
- clean generic registries;
- clean backend state;
- reset callback contexts, counters, latches and synchronization objects;
- no worker thread, timer or resource leaked from a previous test.

Resources created by a scenario must be explicitly released by the scenario or reliably cleaned up by the fixture. `TearDown()` must remain safe after partial test failure.

When a test intentionally exercises initialization or deinitialization itself, track fixture state so cleanup does not blindly initialize or deinitialize the same instance twice.

Scenarios that require multiple OSAL instances must create and destroy those instances independently.

Thread and timer tests must ensure that worker activity and callbacks are quiescent before the test finishes so no asynchronous activity can leak into a later test.

### Test Scenario Structure

Use the Arrange-Act-Assert pattern whenever it naturally fits the scenario.

Scenario setup and required assumptions should normally use `ASSERT_*` so the test stops when the state required for the behavior under test cannot be established. Use `EXPECT_*` for independent observations of the behavior under test when continuing the scenario remains meaningful.

Do not turn setup checks into separate integration-test goals. For example, asserting successful primitive creation is normally a precondition for a blocking or timeout scenario, not a duplicate test of the generic Create API contract.

Each test scenario must have a Doxygen description that states, concisely but with enough detail:

- what behavior is being verified;
- how the scenario exercises that behavior;
- what observable result is expected.

Use `\brief` for the test goal and `\details` for the scenario and expected behavior, consistent with the surrounding test style.

Inside the test body, add concise comments for important scenario milestones and state transitions. Comments should describe intent, for example:

```c
/* Fill the buffer to force the producer to block */
/* Release capacity from the consumer side */
/* Verify that the blocked producer resumes */
```

Do not number test steps. Avoid comments that merely repeat the following statement, such as `Call function`, `Check result` or `Increment counter`.

### Behavioral Coverage

When a primitive or abstraction gains new behavior, extend its tests with scenarios that exercise that behavior rather than only adding API smoke tests.

Depending on the primitive, consider:

- normal operation;
- resource exhaustion or capacity limits;
- empty/full states;
- blocking and wake-up transitions;
- finite timeout expiration;
- multiple waiters or producers/consumers;
- ordering and data integrity;
- reset/restart behavior;
- independent simultaneous objects;
- ownership across OSAL instances;
- repeated lifecycle operations where allowed;
- backend-specific unsupported behavior.

Time-sensitive integration tests must verify semantic timing behavior with reasonable bounds or synchronization events. Do not require exact wall-clock equality that is sensitive to host scheduler jitter.

Shared integration helpers may be used for reusable infrastructure such as latches, atomic counters, bounded wait helpers, timing helpers, callback contexts and deterministic test data. Do not hide the actual behavioral scenario behind a complex generic framework when an explicit port-specific test remains clearer.

### Blocking and Concurrency Tests

Tests must not rely on unbounded waits.

Every blocking or asynchronous scenario must have a finite test-side escape path so a regression results in a failed test rather than a permanently hung test process. CTest must also provide an outer timeout as a final safety net for integration tests.

Prefer explicit synchronization between test threads over arbitrary long sleeps. Small bounded delays may be used only when they are part of the behavior being tested or when no stronger synchronization point exists.

### CMake, CTest and GitLab CI

The local and CI test paths must use the same CMake/CTest harness. Do not maintain a separate CI-only execution path for the same tests.

Register GoogleTest cases with CTest so individual scenarios remain visible and filterable. Apply useful labels such as:

```text
unit
integration
posix
freertos_posix
generator
```

Keep separate build/test environments where different runtime environments require it, for example generic unit tests, native POSIX integration tests and FreeRTOS-POSIX integration tests.

GitLab CI must expose test failures clearly. When test infrastructure is added or changed, update CI as needed to provide:

- separate jobs for logically distinct test environments when practical;
- CTest output on failure;
- JUnit test reports;
- useful failure logs as artifacts;
- bounded job and test timeouts;
- deterministic non-zero exit status on test failure.

Generated-code tests in CI must regenerate the required output from the current templates/configuration rather than accidentally relying on stale generated files.
