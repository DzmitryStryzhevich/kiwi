# KIWI OSAL Architecture

This directory contains the generic OSAL templates and portable backend templates used by the KIWI code generator.

## What problem does an OSAL solve?

Embedded components often depend directly on a concrete operating-system API: FreeRTOS queues and semaphores, CMSIS-RTOS2 mutexes and threads, POSIX pthreads, native timers, allocator calls, and so on. Direct use is simple initially, but it couples the component to one environment and spreads OS-specific types, handles, error conventions and lifecycle rules throughout the component.

An Operating System Abstraction Layer defines a stable component-facing contract and translates that contract into the selected backend API. The component uses its own OSAL interface while the portable implementation deals with the native OS.

Conceptually:

```text
Software Component
        |
        | component-specific OS contract
        v
+-------------------------+
|      OSAL Instance      |
|                         |
|  selected API groups    |
|  resource registries    |
|  validation / tracing   |
+------------+------------+
             |
             | portable dispatch
             v
+-------------------------+
|   Backend implementation|
+------------+------------+
             |
      native OS API
```

## Why component-scoped instead of one global OSAL?

A system-wide OSAL tends to accumulate every primitive required by every subsystem. Over time it becomes a large universal interface: drivers, protocol stacks, services and application modules all depend on the same abstraction even though each client uses only a small subset.

KIWI follows the opposite rule: **the scope of the OSAL is defined by the software component that uses it**. A component may be a driver, middleware module, protocol stack, framework, or a group of tightly related modules.

This is closely aligned with the Interface Segregation Principle: a client should not depend on methods it does not use. Code generation makes this practical by removing API groups that are not selected for a particular component.

## Ownership model

The intended ownership model is explicit:

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

The component owns the OSAL instance. The OSAL instance owns the OS resources created through it. The component depends on its own OS abstraction contract, not on the operating-system API.

Native handles should therefore remain inside OSAL resource registries instead of being scattered through component state structures.

## Resource registries and stable indices

OS resources created through an OSAL instance are stored in fixed-capacity internal registries. This gives the abstraction one place to perform:

- resource allocation and registration;
- lookup and validation;
- ownership checks;
- deterministic cleanup;
- diagnostics and tracing;
- leak and lifecycle checks.

A component can refer to its resources through stable indices/getters rather than retaining native backend handles. For a deterministic initialization sequence, resource indices remain stable for the same generated configuration.

```text
Component resource index
          |
          v
      OSAL getter
          |
          v
  internal registry
          |
          v
 native OS resource
```

## Generic layer and portable layer

The generated OSAL is split conceptually into two layers.

The **generic layer** defines the component-facing API, common types, lifecycle, validation and resource bookkeeping. The **portable layer** maps that contract to a particular operating system.

The portable implementation is selected through a method table rather than by exposing native OS calls to the component. This gives the component one stable vocabulary while allowing backends to differ internally in handle types, timeout representation, scheduling semantics, ISR rules and error handling.

## Runtime validation and assertions

The OSAL boundary is a useful place to enforce the component's operating-system access contract. Depending on the operation, checks can include:

- valid OSAL instance and lifecycle state;
- valid arguments;
- valid method-table entry;
- valid resource index/handle;
- resource ownership by the current OSAL instance;
- resource type/state;
- thread-context versus ISR-context restrictions;
- backend return-value validation;
- internal registry invariants.

Programming-contract violations and impossible internal states are candidates for assertions. Recoverable runtime failures should remain represented by OSAL error codes where appropriate. Assertion behavior must remain configurable for different build modes.

## Tracing and diagnostics

All operating-system access performed through the OSAL passes through one controlled boundary. This makes tracing useful at both the generic and backend layers.

Typical trace points include:

- resource creation/deletion;
- queue operations;
- lock acquisition/release;
- thread lifecycle;
- time and memory operations;
- backend failures;
- invalid execution context;
- failed contract checks;
- deinitialization and cleanup.

Tracing and assertions are expected to be applied consistently across the generic OSAL and all portable backends, while remaining configurable so they can be reduced or disabled where required by a release configuration.

## Internal resource synchronization

Resource-registry manipulation must be synchronized independently from client-visible lock objects. Each portable backend is expected to provide an internal resource-management mutex owned by the OSAL backend instance.

That internal mutex:

- protects free-slot search and registry updates;
- protects registration and release bookkeeping;
- protects registry lookups where synchronization is required;
- protects best-effort cleanup during deinitialization;
- is not placed in the component-visible lock registry;
- is created directly by the backend, not through the public OSAL lock API.

This avoids recursive dependency on the same registry that the internal mutex is intended to protect.

## Portable backend expectations

All backends should expose the same generic semantics even when the native API differs. Backend-specific code should handle details such as:

- timeout conversion and infinite-wait representation;
- thread-priority mapping;
- ISR-safe versus thread-context operations;
- native resource creation/deletion;
- backend-specific lifecycle state;
- internal resource synchronization;
- backend-specific validation.

The source organization, naming, Doxygen style and lifecycle model should remain uniform across FreeRTOS, POSIX, CMSIS-RTOS2 and future ports.

## Current template tree

```text
osal/
├── CMakeLists.txt
├── template_osal.c
├── template_osal.h
└── portable/
    ├── freertos/
    │   ├── CMakeLists.txt
    │   ├── template_osal_freertos.c
    │   └── template_osal_freertos.h
    ├── posix/
    │   └── CMakeLists.txt
    └── cmsis_rtos2/
        └── CMakeLists.txt
```

FreeRTOS is the current implemented port. POSIX and CMSIS-RTOS2 remain planned.

## API selection

The current generator exposes these implemented selectable API groups:

- Queues;
- Locks;
- Threads;
- Time;
- Memory.

The generator is intentionally designed so additional primitive groups can be introduced later without turning the generic OSAL into one mandatory monolithic interface. A generated component should contain only the groups selected for that component.

For generator usage and selection controls, see [`../generator/README.md`](../generator/README.md).
