# KIWI OSAL tests

The `test/` tree contains the automated verification harness for the generated KIWI OSAL contract and its implemented host-capable backends.

The harness intentionally generates a fresh full-set `kiwi_test` OSAL during CMake configure. Tests therefore exercise the current generator templates instead of checked-in generated sources that can become stale.

## Test layers

The verification model is split into three executable test environments plus generator integration checks:

| Layer | Target | Purpose |
| --- | --- | --- |
| Generic unit | `kiwi_osal_unit_test` | Validate generic API arguments, registry behavior, vtable dispatch and backend-status propagation through GoogleMock. |
| Native POSIX integration | `kiwi_osal_posix_it_test` | Validate observable behavior of the real POSIX backend and host synchronization primitives. |
| FreeRTOS POSIX integration | `kiwi_osal_freertos_posix_it_test` | Validate the real FreeRTOS backend against the official `GCC_POSIX` FreeRTOS kernel port. |
| Generator integration | CTest/Python | Validate profile filtering and required generated symbols for the current templates. |

Generic unit tests and backend integration tests deliberately do not duplicate the same responsibility. Generic tests validate API/dispatch contracts; integration tests validate real blocking, timeout, wake-up, ordering, timer and lifecycle behavior.

Backend integration suites also verify cross-instance ownership for registered resources: handles or allocations created by one OSAL instance are rejected by another instance while remaining valid for their owner.

## Layout

```text
test/
├── CMakeLists.txt
├── README.md
├── config/
│   └── FreeRTOSConfig.h
├── generator/
│   └── osal_generator_it_test.py
├── mocks/
│   ├── osal_mock.cpp
│   └── osal_mock.hpp
├── unit/
│   ├── osal_unit_test_fixture.hpp
│   └── osal_unit_test_*_gtest.cpp
└── integration/
    ├── common/
    │   └── osal_it_test_utils.hpp
    ├── posix/
    │   ├── osal_posix_it_test_fixture.hpp
    │   └── osal_posix_it_test_*_gtest.cpp
    └── freertos_posix/
        ├── osal_freertos_posix_it_test_fixture.hpp
        ├── osal_freertos_posix_it_test_main.cpp
        └── osal_freertos_posix_it_test_*_gtest.cpp
```

Integration-test filenames always include both the port and `it_test`, even though the directory already identifies the runtime. This keeps filenames, CTest output and CI logs self-describing.

## Scenario style

Each scenario follows Arrange / Act / Assert where practical.

Every `TEST_F` has a Doxygen description that states:

- the behavior under verification;
- how the scenario exercises that behavior;
- the expected observable result.

Inside the body, comments mark meaningful state transitions such as blocking a worker, publishing a token, reaching a stream trigger level or stopping a timer. Step numbering and comments that merely restate the following line are intentionally avoided.

Fixtures initialize an isolated OSAL instance in `SetUp()` and deinitialize it in `TearDown()` whenever the scenario permits that lifecycle. Tests that explicitly exercise deinitialization update fixture state so teardown does not perform a second deinit.

Blocking scenarios use bounded waits and CTest provides an outer process timeout. A regression should fail a test instead of hanging a CI runner indefinitely.

## Local configuration

The test project is standalone and is configured from the repository root with `-S test`.

Install the generator package in the Python environment used by CMake before configuring the harness:

```bash
python -m pip install -e generator
```

CMake invokes the current repository generator directly and therefore uses that Python environment for its runtime dependencies.

The generator suite does not require GoogleTest:

```bash
cmake -S test -B build/test-generator -G Ninja \
  -DKIWI_TEST_BUILD_GENERATOR=ON \
  -DKIWI_TEST_BUILD_UNIT=OFF \
  -DKIWI_TEST_BUILD_POSIX=OFF \
  -DKIWI_TEST_BUILD_FREERTOS_POSIX=OFF

ctest --test-dir build/test-generator -L generator --output-on-failure
```

### Generic unit tests

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

### Native POSIX integration tests

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

### FreeRTOS backend through the POSIX kernel port

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

By default CMake fetches pinned GoogleTest `v1.14.0` and FreeRTOS-Kernel `V11.3.1` when the corresponding suites need them. Offline development can point the harness at existing source trees:

```bash
-DKIWI_TEST_GTEST_SOURCE_DIR=/path/to/googletest
-DKIWI_TEST_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel
```

Fetching can be disabled explicitly with `KIWI_TEST_FETCH_GTEST=OFF` or `KIWI_TEST_FETCH_FREERTOS=OFF`.

## CTest discovery and filtering

GoogleTest cases are registered individually with `gtest_discover_tests()`. CTest therefore launches each discovered scenario as a separate test process in addition to the per-scenario fixture reset. For the FreeRTOS-POSIX executable this also gives every CTest scenario a fresh kernel scheduler process. Useful labels are:

```text
generator
unit
integration
posix
freertos_posix
```

List discovered tests without running them:

```bash
ctest --test-dir build/test-posix -N
```

Run only one primitive or scenario family by regular expression:

```bash
ctest --test-dir build/test-posix -R StreamBuffer --output-on-failure
ctest --test-dir build/test-freertos-posix -R SoftwareTimer --output-on-failure
```

## GitLab CI

Both CI systems use a five-stage verification flow:

```text
1 - Generator build check
2 - Generate full OSAL set
3 - Build generated code
4 - Static analysis
5 - Test suites
```

GitHub Actions exposes these as five sequential jobs. GitLab uses the same five explicit stages; the final `test` stage fans out into separate generator, generic unit, native POSIX integration and FreeRTOS-POSIX integration jobs. The test jobs use the same CMake/CTest entry points as local development.

Each job:

- configures its own build directory;
- lists discovered tests with `ctest -N`;
- runs the relevant CTest label with `--output-on-failure`;
- writes a JUnit report for GitLab;
- preserves the CTest `Testing/` directory as an artifact for failure diagnostics.

The FreeRTOS integration job uses the official kernel `GCC_POSIX` port and the test-specific `FreeRTOSConfig.h`; it does not mock the FreeRTOS API.

## Coverage expectations for OSAL changes

A new primitive, feature, backend capability or higher-level abstraction is not considered fully designed until its verification strategy is defined at the same time.

Use the lowest layer that proves the behavior:

- generic API/validation/dispatch behavior -> generic unit test;
- real backend synchronization or lifecycle behavior -> integration test for that port;
- generation/profile behavior -> generator integration test.

When the same observable backend contract applies to POSIX and FreeRTOS, keep equivalent scenario names in both integration suites so CI differences are immediately visible.
