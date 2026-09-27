# KIWI Testing

KIWI now has a basic GitHub Actions verification pipeline. It intentionally starts with a small set of high-value checks: the generator itself must build and run, a full-set OSAL must be generated, the generated C sources must compile, and the generated sources must pass static analysis. More focused unit and integration tests remain planned.


## GitHub Actions pipeline

The repository contains a four-stage workflow in `.github/workflows/ci.yml`:

```text
generator-check
      |
      v
generate-full-set
      |
      v
build-generated
      |
      v
static-analysis
```

The `generate-full-set` stage enables every currently implemented OSAL primitive group:

- queue;
- stream buffer;
- mutex;
- semaphore;
- event flags;
- thread;
- critical section;
- software timer;
- time;
- memory.

The generated source tree is passed between jobs as a workflow artifact. The build stage checks out the official `FreeRTOS/FreeRTOS-Kernel` repository at the pinned `V11.3.1` tag and builds the generated full-set OSAL against the real `GCC_POSIX` FreeRTOS simulator port. A CI-only `FreeRTOSConfig.h` supplies the kernel configuration required by the generated API set, while the FreeRTOS kernel itself, its POSIX port, heap implementation, queue/event/stream-buffer/timer code and native headers all come from the upstream FreeRTOS release.

The build uses CMake and links a small host executable against both generated OSAL libraries and the real FreeRTOS kernel. This intentionally goes beyond syntax-only compilation: unresolved FreeRTOS symbols in the generated port become link failures. The generated targets are compiled with `-Wall -Wextra -Werror`.

The static-analysis stage checks the same generated full-set sources against the real FreeRTOS headers from the pinned POSIX port. It currently runs GCC `-fanalyzer` and `cppcheck`. MPU and SMP are not enabled in this host build because the FreeRTOS POSIX simulator is neither an MPU port nor an SMP target; their port-specific validation remains covered at source-contract level rather than being faked by CI-only FreeRTOS stubs.

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
- controlled mutex behavior;
- backend error injection;
- resource leak and lifecycle violations.

The important property is that the component still uses its normal OSAL API. The test does not need to introduce test-only branches into the component or emulate the entire native RTOS API.

For the architectural rationale, see [`osal_architecture_en.md`](osal_architecture_en.md#14-testability-as-part-of-the-architecture).

## Planned test layers

The test strategy should eventually include several complementary layers:

- code-generation core unit tests;
- configuration normalization and validation tests;
- YAML profile load/save round-trip tests;
- deterministic/golden-file generation tests;
- CLI argument and exit-code tests;
- output-layout tests for every supported combination;
- generated C syntax/build checks;
- backend-specific tests for FreeRTOS and POSIX;
- host/test OSAL backend tests for component isolation;
- resource lifecycle/registry tests;
- timeout and execution-context semantic tests;
- deterministic failure-injection tests;
- assertion, tracing and error-path tests;
- regression tests for reported bugs.

GUI logic should remain thin enough that most behavior can be tested through the shared `generator/core/` pipeline. GUI-specific tests can then focus on frontend state mapping and profile interaction rather than duplicating generator tests.

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

The same checks can also be run manually from the repository root during local development.

Check Python syntax:

```bash
python -m compileall -q \
  generator/core \
  generator/formatter \
  generator/cli \
  generator/gui
```

Check CLI help/no-argument behavior:

```bash
python generator/cli/main.py
python generator/cli/main.py --help
```

Neither command should generate files.

Perform a sample generation into a temporary output directory:

```bash
python generator/cli/main.py \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --use-thread-api \
  --use-queue-api \
  --output=./generated_smoke
```

Then verify that the expected module tree is produced and that disabled API groups are not present in generated sources.

Profile round-trip, deterministic output and host/test backend checks will be converted into automated tests as the test infrastructure is introduced.
