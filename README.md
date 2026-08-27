# KIWI OSAL Code Generator

KIWI is a code generator for creating component-scoped Operating System Abstraction Layers (OSALs) for embedded software.

The central idea is simple: the scope of an OSAL is defined by the software component that uses it, not by the whole system. A component may be a small peripheral driver, a middleware module, a protocol or network stack, a framework, or a group of tightly related modules.

KIWI generates a dedicated OSAL contract for that component and a portable implementation for the selected operating-system backend.

## Why a component-scoped OSAL

A single system-wide OSAL tends to grow into a universal interface for every possible use case. As the system evolves, unrelated components become dependent on the same increasingly large abstraction layer. The result is often a broad API, leaking implementation details, increased coupling, and a contract that exposes operations many clients never use.

KIWI follows the opposite model: each component depends on its own OS abstraction contract.

This approach is aligned with the principles of SOLID, especially the Interface Segregation Principle:

> A client should not depend on methods it does not use.

A component therefore sees only the operating-system services required by that component.

## Architectural properties

### Component-defined scope

The lifetime and scope of an OSAL instance are defined by its owning software component.

A component can be as small as a simple GPIO expander driver or as large as a network stack or complete framework. KIWI does not impose a fixed component size. The OSAL boundary follows the logical architecture of the software.

### Reduced coupling

Independent components do not have to depend on one global system abstraction interface.

A change to the OSAL contract of one component does not require unrelated components to adopt the same change. This keeps operating-system dependencies local and prevents a common OSAL from becoming an architectural coupling point for the whole firmware.

### Minimal and purpose-built API

Native RTOS APIs are often large, historically inconsistent, and easy to misuse. They may provide multiple variants of similar operations, special ISR versions, different timeout rules, implementation-specific handles, and platform-specific lifecycle semantics.

A component-scoped OSAL deliberately exposes only the subset required by the component. Operations that the component does not need are not part of its contract.

Reducing the effective API surface lowers cognitive load, simplifies code review, and reduces the number of ways in which operating-system services can be used incorrectly.

### Unified semantics inside the component

Different operating systems provide equivalent services through different names and conventions. FreeRTOS, CMSIS-RTOS2, POSIX, and other environments differ in areas such as:

- naming;
- timeout representation;
- error reporting;
- object lifecycle;
- thread and ISR context rules;
- ownership conventions;
- handle types;
- time units.

The OSAL defines one stable vocabulary and one semantic contract for the component. The portable backend translates that contract to the API and behavior of the selected operating system.

The component therefore does not need to know whether a mutex is implemented by `xSemaphoreTake`, `osMutexAcquire`, `pthread_mutex_lock`, or another native primitive.

### Encapsulation of native OS resources

Native RTOS handles are not scattered across the component's internal structures.

The component does not need to maintain structures filled with objects such as:

```c
TaskHandle_t rx_task;
TaskHandle_t tx_task;
SemaphoreHandle_t socket_mutex;
QueueHandle_t rx_queue;
TimerHandle_t retry_timer;
```

Instead, operating-system resources created through an OSAL instance are stored in the instance's internal registries. Native backend handles remain encapsulated inside the OSAL implementation.

This keeps component data structures focused on component state instead of turning them into containers for RTOS bookkeeping.

### Stable resource indices

The component accesses registered OS resources through getters and stable indices.

Resource indices are determined by the deterministic creation/allocation order of the corresponding resource type. The component therefore addresses a resource by its defined index rather than by storing the native operating-system handle itself.

Conceptually:

```text
Software Component
        |
        | resource index
        v
    OSAL getter
        |
        v
Internal resource registry
        |
        v
Native OS resource
```

The deterministic registration order is part of the component's OSAL contract and should remain stable for a given generated configuration.

### Centralized resource ownership

The ownership model is explicit:

```text
Software Component
        owns
         |
         v
    OSAL Instance
        owns
         |
         v
Operating-System Resources
```

The component owns the OSAL instance; the OSAL instance owns the operating-system resources created through it.

This provides one place for resource registration, lookup, validation, deinitialization, diagnostics, and leak detection.

### Controlled operating-system access boundary

All operating-system access operations performed by the component through the OSAL pass through a single controlled boundary.

This boundary is a natural location for:

- argument validation;
- resource validation;
- execution-context validation;
- contract checks;
- tracing;
- diagnostics;
- statistics;
- timing measurements;
- result validation.

The component remains independent from backend-specific instrumentation and native RTOS details.

### Runtime validation

Internal OSAL checks enforce the component's operating-system access contract at runtime by validating that each operation is permitted and correctly formed in the current execution context.

Depending on the operation and backend, these checks may validate conditions such as:

- a valid OSAL instance;
- valid arguments;
- a valid resource index or handle;
- resource existence in the internal registry;
- resource type and state;
- ownership by the current OSAL instance;
- whether the operation is allowed from thread context or ISR context;
- the result returned by the backend implementation.

These checks reduce the probability of subtle errors when working with low-level operating-system APIs.

Validation and diagnostic checks can be made configurable so that development builds provide strict runtime checking while selected checks can be reduced or disabled in release builds where required.

### Traceability and observability

The OSAL boundary allows the flow of operating-system access operations of an individual component to be observed independently from the rest of the system.

A trace can therefore be associated directly with the component that initiated the operation instead of producing an undifferentiated global stream of RTOS calls.

This makes it possible to inspect, for example:

- resource creation and deletion;
- mutex acquisition and release;
- queue operations;
- blocking and waiting;
- timing operations;
- memory operations;
- errors and failed contract checks.

The same boundary can later be used for component-specific profiling, latency analysis, contention analysis, and resource-usage statistics.

### Deliberate and small indirection cost

The OSAL uses a function-table based dispatch model to separate the component-facing contract from the operating-system-specific implementation.

The additional indirection is intentionally small. For typical RTOS operations, the cost of a function-table dispatch is minor compared with the cost of the underlying operation, which may involve synchronization, scheduler interaction, blocking, context switching, or memory management.

The wrapper is also the point where validation, tracing, diagnostics, and instrumentation can be performed before and after the backend call.

Architectural isolation should not be discarded merely to avoid a few instructions of dispatch overhead without measurement on the target platform.

### Compiler and linker optimization

Separate component-scoped OSALs may contain similar wrappers or backend implementations. Similar source code does not imply that all of it must remain duplicated in the final binary.

Modern toolchains can remove unused sections and may optimize equivalent code through inlining, link-time optimization, identical-code folding, and other compiler/linker transformations.

For that reason, OSAL contracts should primarily follow architectural component boundaries. Binary-size decisions should be based on the generated machine code and actual measurements rather than on source-level similarity alone.

## Deterministic code generation

KIWI uses code generation to make OSAL creation reproducible and to remove repetitive manual integration work.

For the same templates and configuration, the generator is intended to produce an equivalent source layout, API profile, symbol prefixing, and portable implementation structure.

Deterministic generation reduces the probability of mechanical integration errors such as:

- mismatched declarations and definitions;
- missing function-table entries;
- inconsistent symbol prefixes;
- forgotten resource configuration;
- accidental divergence between similar OSAL implementations;
- incomplete manual removal of unused API groups.

Code generation and runtime validation complement each other:

- code generation provides structural consistency and reproducibility;
- internal OSAL checks validate correct use of operating-system access operations at runtime.

## High-level model

```text
+---------------------------+
|    Software Component     |
+-------------+-------------+
              |
              | component-specific OS contract
              v
+---------------------------+
|       OSAL Instance       |
|                           |
|  - selected API groups    |
|  - resource registries    |
|  - stable resource IDs    |
|  - validation             |
|  - tracing boundary       |
+-------------+-------------+
              |
              | function-table dispatch
              v
+---------------------------+
|  Portable Implementation  |
+-------------+-------------+
              |
      +-------+-------+
      |       |       |
      v       v       v
  FreeRTOS  POSIX  CMSIS-RTOS2
```

## Repository structure

```text
kiwi/
├── generator/                 Python GUI and generator build files
├── osal/                      Base OSAL template
│   ├── CMakeLists.txt
│   └── portable/
│       ├── freertos/          FreeRTOS port template
│       ├── posix/             POSIX port scaffold
│       └── cmsis_rtos2/       CMSIS-RTOS2 port scaffold
├── test/                      Test infrastructure
├── doc/                       Project documentation and artwork
├── .github/                   GitHub CI/CD infrastructure
├── .gitlab/                   GitLab CI/CD infrastructure
└── .gitlab-ci.yml             GitLab CI entry point
```

## Current generator status

The current GUI provides the following port choices:

- `FreeRTOS` — implemented;
- `POSIX` — planned;
- `CMSIS RTOS v2` — planned.

The currently selectable API groups are:

- Queues;
- Locks;
- Threads;
- Time;
- Memory.

Additional primitives and portable backends can be introduced without changing the component-scoped architecture described above.

## Generated module naming

The generator accepts a module prefix and derives the required naming forms from it.

For example:

```text
foo_module
```

is used to generate component-specific file names, symbols, include guards, build targets, and the OSAL profile.

The default module prefix in the GUI is:

```text
foo_module
```

## Running from source

From the repository root:

```bash
python generator/kiwi_codegen_app.py
```

## Building the Windows executable

Run:

```text
generator/build_exe.bat
```

The build script installs the required Python packages and invokes PyInstaller.

The executable is created at:

```text
generator/dist/kiwi.exe
```

The PyInstaller bundle includes the OSAL template tree and KIWI application artwork so the generated executable can run without requiring the source repository beside it.

## Application artwork

The KIWI application artwork is stored in:

```text
doc/kiwi.png
doc/kiwi.ico
```

`kiwi.png` is used by the GUI at runtime and `kiwi.ico` is embedded into the Windows executable.

## License

KIWI is distributed under the MIT License. See [`LICENSE`](LICENSE).
