# KIWI Testing

Automated tests are not implemented yet. The `test/` directory is reserved for project test infrastructure, but testing is already a central part of the KIWI architecture rather than an optional layer to be added later.

## Why component-scoped OSAL is test-friendly

A component-scoped OSAL gives every component a small, explicit boundary around its operating-system dependencies. That boundary can be backed by FreeRTOS in production and by a host/test implementation during tests without changing the component itself.

```text
Production                         Test

Component                          Same Component
    |                                  |
    v                                  v
Component OSAL API                 Same OSAL API
    |                                  |
    v                                  v
Production backend                Test / host backend
    |                                  |
    v                                  v
Target RTOS                        x86 / Linux / Windows / CI
```

This allows component tests to run in an isolated environment on a platform that may be completely different from the final embedded target.

A test backend can also deliberately control conditions that are difficult to reproduce reliably on hardware, for example:

- queue full / empty conditions;
- queue receive timeouts;
- allocation failures;
- thread-creation failures;
- deterministic or accelerated time;
- controlled lock behavior;
- backend error injection;
- resource leak and lifecycle violations.

The important property is that the component still uses its normal OSAL API. The test does not need to introduce test-only branches into the component or emulate the entire native RTOS API.

For the architectural rationale, see [`../osal/README.md`](../osal/README.md#testability-is-part-of-the-architecture).

## Planned test layers

The test strategy should eventually include several complementary layers:

- code-generation core unit tests;
- configuration normalization and validation tests;
- YAML profile load/save round-trip tests;
- deterministic/golden-file generation tests;
- CLI argument and exit-code tests;
- output-layout tests for every supported combination;
- generated C syntax/build checks;
- backend-specific tests for FreeRTOS, POSIX and CMSIS-RTOS2;
- host/test OSAL backend tests for component isolation;
- resource lifecycle/registry tests;
- timeout and execution-context semantic tests;
- deterministic failure-injection tests;
- assertion, tracing and error-path tests;
- regression tests for reported bugs.

GUI logic should remain thin enough that most behavior can be tested through the shared `kiwi_codegen.py` core. GUI-specific tests can then focus on frontend state mapping and profile interaction rather than duplicating generator tests.

## Host/test backend direction

A future test backend should implement the same generic semantic contract as a production backend, but it may deliberately expose extra control to the **test harness**, not to the component.

For example, the harness may configure the next allocation to fail or advance synthetic time. The component must still observe only ordinary OSAL results.

```text
Test harness
     |
     | configure deterministic behavior
     v
Test OSAL backend <----- Same generic OSAL contract ----- Component
```

This separation is important: test controls belong outside the component-facing API. Otherwise testability would leak test-specific behavior back into production component code.

## Current manual smoke test

Until the automated suite exists, a basic development check can be performed from the repository root.

Check Python syntax:

```bash
python -m py_compile \
  generator/kiwi_codegen.py \
  generator/kiwi_codegen_cli_app.py \
  generator/kiwi_codegen_ui_app.py
```

Check CLI help/no-argument behavior:

```bash
python generator/kiwi_codegen_cli_app.py
python generator/kiwi_codegen_cli_app.py --help
```

Neither command should generate files.

Perform a sample generation into a temporary output directory:

```bash
python generator/kiwi_codegen_cli_app.py \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --use-thread-api \
  --use-queue-api \
  --output=./generated_smoke
```

Then verify that the expected module tree is produced and that disabled API groups are not present in generated sources.

Profile round-trip, deterministic output and host/test backend checks will be converted into automated tests as the test infrastructure is introduced.
