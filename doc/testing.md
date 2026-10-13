# KIWI Testing

KIWI now has two complementary verification paths:

- generated-code verification in GitHub Actions;
- OSAL unit/integration verification in the standalone `test/` harness and GitLab CI.

The two paths intentionally overlap at the generated-code boundary but serve different purposes. GitHub Actions checks that a full generated FreeRTOS/POSIX OSAL still builds, links and passes static analysis. The test harness verifies the generic OSAL contract and the observable behavior of the implemented backends.

## Test architecture

```text
                    Current generator templates
                              |
                              v
                      generated kiwi_test OSAL
                       /         |          \
                      /          |           \
                     v           v            v
          Generic unit       POSIX IT     FreeRTOS POSIX IT
          GoogleMock         native host   real FreeRTOS kernel
              |                  |                 |
              v                  v                 v
        API / dispatch      pthread/POSIX      GCC_POSIX port
```

The harness regenerates its full-set `kiwi_test` OSAL during CMake configure. Checked-in generated OSAL test sources are intentionally avoided.

### Generic unit tests

Target:

```text
kiwi_osal_unit_test
```

The generic layer is tested through an `OsalMock + OsalMockBinder + C bridge vtable` arrangement. This keeps the public C API under test while GoogleMock controls backend behavior.

The unit layer covers:

- invalid public arguments;
- initialized / uninitialized instance state;
- vtable dispatch;
- exact argument forwarding;
- backend-status propagation;
- registry handle access;
- missing backend operation handling;
- generic metadata and validity behavior.

These tests do not attempt to prove real queue blocking, semaphore wake-up, timer expiration or other native runtime behavior.

### Native POSIX integration tests

Target:

```text
kiwi_osal_posix_it_test
```

The POSIX integration suite exercises the generated generic OSAL together with the generated POSIX backend and the host's real pthread/POSIX facilities.

Behavioral coverage includes:

- queue FIFO/full/empty/reset, blocking receive and timed send/receive;
- recursive mutex ownership, try-lock contention, wake-up and timeout;
- counting semaphore initial/max count, wake-up, timeout and multiple waiters;
- event-flags set/get/clear, wait-any, wait-all, clear/no-clear and timeout;
- stream-buffer partial transfers, empty/full behavior, trigger-level wake-up, blocking producer/consumer, timeout, wrap-around and reset;
- thread creation, explicit yield and zero-delay no-op semantics;
- one-shot and periodic software timers, Stop, Reset, restart, independent timers and delete-before-expiration;
- monotonic time behavior;
- real memory allocation/free behavior;
- the expected runtime-unsupported result for deprecated system-level Critical Section operations.

### FreeRTOS backend integration tests

Target:

```text
kiwi_osal_freertos_posix_it_test
```

This suite uses the generated FreeRTOS backend with the **official FreeRTOS-Kernel `GCC_POSIX` port**. FreeRTOS APIs are not mocked.

```text
Generic OSAL
    |
    v
Generated FreeRTOS backend
    |
    v
FreeRTOS-Kernel
    |
    v
GCC_POSIX host port
```

The suite mirrors the portable behavioral scenarios from the native POSIX suite where the generic contract is the same. Backend-specific expectations remain explicit, for example the supported FreeRTOS Critical Section behavior versus the unsupported POSIX mapping.

### Generator integration test

CTest also registers a Python generator integration check that verifies:

- full primitive-set generation for FreeRTOS and POSIX;
- required new API symbols such as `ThreadYield`;
- native POSIX timer and yield implementation presence;
- FreeRTOS timer and yield implementation presence;
- removal of the complete THREAD group when that group is disabled;
- preservation of unrelated groups while THREAD is disabled;
- removal of generator `BEGIN/END` markers from emitted output.

## Test structure and naming

```text
test/
├── CMakeLists.txt
├── README.md
├── config/
├── generator/
├── mocks/
├── unit/
└── integration/
    ├── common/
    ├── posix/
    └── freertos_posix/
```

Integration filenames always identify both the port and integration-test role:

```text
osal_posix_it_test_queue_gtest.cpp
osal_posix_it_test_software_timer_gtest.cpp
osal_freertos_posix_it_test_queue_gtest.cpp
osal_freertos_posix_it_test_software_timer_gtest.cpp
```

Unit tests use the equivalent generic naming:

```text
osal_unit_test_queue_gtest.cpp
osal_unit_test_software_timer_gtest.cpp
```

## Scenario construction rules

Tests use Arrange / Act / Assert where practical.

Each `TEST_F` has a Doxygen scenario description containing:

- the verification goal;
- the way the scenario exercises that goal;
- the expected observable outcome.

Inside the body, comments describe meaningful milestones and state transitions. They are not numbered and do not merely repeat the next statement.

Test fixtures isolate state through `SetUp()` / `TearDown()` wherever the tested lifecycle allows it. Each ordinary scenario starts from a fresh OSAL instance and teardown deinitializes that instance. A scenario that explicitly deinitializes the fixture instance updates fixture state so teardown does not deinitialize it twice.

Integration tests use successful setup calls as assertions/preconditions rather than duplicating generic API unit tests. Their purpose is to verify real runtime behavior.

Blocking and asynchronous tests always have bounded waits. CTest adds an outer timeout so a regression cannot hang a CI runner indefinitely.

For detailed local commands and test-tree conventions, see [`../test/README.md`](../test/README.md).

## Local execution

Before configuring the standalone test harness, install the generator package in the Python environment used by CMake:

```bash
python -m pip install -e generator
```

### Generator integration

```bash
cmake -S test -B build/test-generator -G Ninja \
  -DKIWI_TEST_BUILD_GENERATOR=ON \
  -DKIWI_TEST_BUILD_UNIT=OFF \
  -DKIWI_TEST_BUILD_POSIX=OFF \
  -DKIWI_TEST_BUILD_FREERTOS_POSIX=OFF

ctest --test-dir build/test-generator -L generator --output-on-failure
```

### Generic unit

```bash
cmake -S test -B build/test-unit -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKIWI_TEST_BUILD_GENERATOR=OFF \
  -DKIWI_TEST_BUILD_UNIT=ON \
  -DKIWI_TEST_BUILD_POSIX=OFF \
  -DKIWI_TEST_BUILD_FREERTOS_POSIX=OFF

cmake --build build/test-unit --parallel
ctest --test-dir build/test-unit -L unit --output-on-failure
```

### Native POSIX integration

```bash
cmake -S test -B build/test-posix -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKIWI_TEST_BUILD_GENERATOR=OFF \
  -DKIWI_TEST_BUILD_UNIT=OFF \
  -DKIWI_TEST_BUILD_POSIX=ON \
  -DKIWI_TEST_BUILD_FREERTOS_POSIX=OFF

cmake --build build/test-posix --parallel
ctest --test-dir build/test-posix -L '^posix$' --output-on-failure
```

### FreeRTOS through GCC_POSIX

```bash
cmake -S test -B build/test-freertos-posix -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKIWI_TEST_BUILD_GENERATOR=OFF \
  -DKIWI_TEST_BUILD_UNIT=OFF \
  -DKIWI_TEST_BUILD_POSIX=OFF \
  -DKIWI_TEST_BUILD_FREERTOS_POSIX=ON

cmake --build build/test-freertos-posix --parallel
ctest --test-dir build/test-freertos-posix -L '^freertos_posix$' --output-on-failure
```

The harness pins GoogleTest `v1.14.0` and FreeRTOS-Kernel `V11.3.1`. Existing source trees may be supplied with `KIWI_TEST_GTEST_SOURCE_DIR` and `KIWI_TEST_FREERTOS_KERNEL_DIR` for offline/reproducible development.

## CTest labels

The harness assigns these labels:

```text
generator
unit
integration
posix
freertos_posix
```

Examples:

```bash
ctest --test-dir build/test-posix -N
ctest --test-dir build/test-posix -R StreamBuffer --output-on-failure
ctest --test-dir build/test-freertos-posix -R SoftwareTimer --output-on-failure
```

## GitLab CI

`.gitlab-ci.yml` runs separate jobs for the independent test environments:

```text
osal-generator-test
osal-unit-test
osal-posix-it-test
osal-freertos-posix-it-test
```

Each job uses a separate build directory, lists discovered tests, runs the same CTest label used locally, emits JUnit and publishes the CTest `Testing/` directory for diagnostics.

```text
build/test-generator
build/test-unit
build/test-posix
build/test-freertos-posix
```

This separation keeps a FreeRTOS scheduler/runtime failure from obscuring native POSIX or generic unit results.

## GitHub generated-code verification

The existing GitHub Actions path remains focused on generated-code quality:

```text
generator-build
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

The full set enables queue, stream buffer, mutex, semaphore, event flags, thread, deprecated Critical Section, software timer, time and memory groups for both implemented ports.

The build stage links the FreeRTOS backend against FreeRTOS-Kernel `V11.3.1` and the `GCC_POSIX` simulator port, and links the native POSIX backend against host pthread/POSIX facilities. The native POSIX smoke executable also checks initialization/deinitialization, explicit thread yield and the expected unsupported Critical Section result.

The static-analysis stage runs GCC `-fanalyzer` and `cppcheck` over the generated generic, FreeRTOS and POSIX sources.

## Test-by-design rule

Testability is part of OSAL design rather than a follow-up task.

Every new primitive, primitive feature, backend capability or generator-level extension must define its verification approach while the feature is being designed. Depending on the change this means extending generic unit tests, one or more backend integration suites, generator tests, the common harness or CI.

A feature is not considered complete merely because its production implementation compiles; its intended behavior must also have an executable verification path at the appropriate layer.

## CI pipeline stages

GitHub Actions and GitLab CI follow the same five-stage verification sequence:

```text
1 - Generator build check
2 - Generate full OSAL set
3 - Build generated code
4 - Static analysis
5 - Test suites
```

The final GitLab `test` stage runs generator, generic unit, native POSIX integration and FreeRTOS-POSIX integration jobs independently. GitHub Actions runs the complete test harness in the fifth sequential job so the workflow graph keeps the same five-stage shape.
