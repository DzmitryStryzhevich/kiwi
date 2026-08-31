# KIWI OSAL Architecture

This directory contains the generic OSAL templates and portable backend templates used by the KIWI code generator.

## What problem does an OSAL solve?

Embedded components often begin by calling a concrete operating-system API directly: FreeRTOS queues and semaphores, CMSIS-RTOS2 mutexes and threads, POSIX pthreads, native timers, heap functions, and so on. That is simple while the component has only one target, but the OS dependency quickly spreads through its types, state structures, control flow and error handling.

The portability problem is broader than replacing one function name with another. Real operating-system APIs differ in several dimensions at once:

- native handle and configuration types;
- object lifecycle and ownership rules;
- timeout units and representation of an infinite wait;
- error models and return-value conventions;
- thread versus ISR-call restrictions;
- scheduler and priority models;
- behavior that is implicit in one OS and explicit in another;
- historically accumulated terminology.

The last point matters more than it first appears. Similar concepts are described with different verbs across operating systems and libraries: an object may be `create`d, `new`ed or `init`ialized, then `delete`d, `destroy`ed or `deinit`ialized. A synchronization object may be `give`n, `release`d or `post`ed; a consumer may `take`, `acquire`, `get`, `receive`, `wait` or `pend`. Large RTOS APIs also expose many variants of the same basic operation for timeouts, ISR context, static allocation, dynamic allocation and specialized object forms.

If those differences leak into a component, the component effectively learns the vocabulary and behavioral details of the current OS. Porting then becomes a rewrite rather than a backend substitution.

An Operating System Abstraction Layer puts a stable contract between the component and the native operating system:

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
| Backend implementation  |
+------------+------------+
             |
             v
        native OS API
```

The portable backend is responsible for translating the KIWI contract into the terminology, types and rules of the selected operating system. The component should not need to know how that translation is implemented.

## One contract, one meaning

KIWI deliberately treats **semantic consistency** as part of the OSAL contract. A generic operation must have one meaning at the component boundary regardless of whether the backend is FreeRTOS, POSIX, CMSIS-RTOS2 or a future implementation.

This is different from building a thin collection of aliases for native APIs. A wrapper such as `osalQueueSend()` is not enough if one backend blocks, another polls, a third uses different timeout units, and each returns unrelated error conventions. The abstraction is only useful when the component can reason about the operation without knowing which backend is active.

The current API follows a constrained vocabulary. The important terms are intended to be read consistently throughout KIWI:

| Term | KIWI semantic meaning |
| --- | --- |
| `Create` | Create an OS resource, register it in the owning OSAL instance and establish its lifecycle ownership. |
| `Delete` | Explicitly release an OS resource and remove it from the owning OSAL registry. A backend may map this to native `delete`, `destroy`, `deinit` or an equivalent operation. |
| `Put` | Producer-side submission of data without waiting for capacity. If the target cannot accept the item immediately, the operation reports that condition rather than silently blocking. |
| `Pend` | Consumer-side wait for data/resource availability with an explicit timeout. `0` represents an immediate attempt; `TEMPLATE_OSAL_INFINITY_TOUT` represents an unbounded wait where the backend can support it. |
| `Get` | Query or retrieve already available state, metadata or a registered handle. `Get` does not imply creation, ownership transfer or an implicit wait. |
| `Post` | Reserved semantic for producer-side signaling/event publication in primitive groups that use it: signal an occurrence without waiting for a consumer. It is not a synonym for resource creation or lookup. |
| `Lock` / `Unlock` | Acquire and release component-visible mutual exclusion using the contract defined by the generic layer, not the native mutex vocabulary. |
| `Reset` | Return a reusable resource to its defined empty/initial operational state without deleting and recreating the resource. |
| `Suspend` / `Resume` | Stop and restore execution eligibility of an existing thread without changing its ownership. |
| `Delay` | Block the calling thread for the requested OSAL time interval. |
| `Exit` | Terminate the calling thread according to the OSAL lifecycle contract. |
| `Malloc` / `Free` | Allocate and release memory through the OSAL backend while keeping the allocation under OSAL ownership/bookkeeping. |

Not every term is used by every primitive group, and future APIs should not invent synonyms casually. When a new primitive is introduced, its generic vocabulary should be chosen by **component-level behavior**, not copied from whichever backend was implemented first.

For example, the current queue contract uses `Put` for an immediate producer operation and `Pend` for the consumer operation that may wait for an item. The FreeRTOS backend maps these semantics to the appropriate FreeRTOS calls; a POSIX or CMSIS backend must reproduce the same observable contract even though its native API and terminology differ.

This semantic normalization solves several practical problems:

- component code is readable without RTOS-specific knowledge;
- switching backends does not change the meaning of existing calls;
- timeout and error handling can be reviewed once at the generic boundary;
- tests can target one stable contract rather than vendor-specific APIs;
- new ports have a behavioral specification to implement, not just a list of function names;
- the generic API can remain smaller than the union of all features offered by all supported operating systems.

The OSAL therefore intentionally does **not** try to expose every native capability. If an operating system has five ways to create a queue, that does not imply that the component contract needs five queue-creation functions. Backend-specific complexity belongs behind the portable layer unless it represents a genuinely distinct component-level requirement.

## Why component-scoped instead of one global OSAL?

A system-wide OSAL tends to accumulate every primitive required by every subsystem. Over time it becomes a large universal interface: drivers, protocol stacks, services and application modules all depend on the same abstraction even though each client uses only a small subset.

That creates another form of coupling. The global abstraction starts resembling a second operating-system API, with a growing surface area, many optional behaviors and a large number of combinations that every port is expected to support.

KIWI follows the opposite rule: **the scope of the OSAL is defined by the software component that uses it**. A component may be a driver, middleware module, protocol stack, framework, or a group of tightly related modules.

A generated component OSAL contains only the primitive groups required by that component. This is closely aligned with the Interface Segregation Principle: a client should not depend on methods it does not use.

The result is a smaller and more explicit boundary:

```text
Large system-wide OSAL

Component A ----\
Component B -----+----> one growing universal OS abstraction
Component C ----/


Component-scoped OSAL

Component A ---> A OSAL contract ---> backend
Component B ---> B OSAL contract ---> backend
Component C ---> C OSAL contract ---> backend
```

The portable implementation can still share common architectural rules and generated patterns. What changes is the dependency direction: each component owns the abstraction it needs instead of depending on a project-wide catalog of operating-system services.

## Testability is part of the architecture

Component-scoped OSAL and component testing naturally complement each other. The same boundary that isolates the component from the production operating system also provides a clean **test seam**.

The component depends only on its own small OSAL contract. Therefore a production backend can be replaced by a host or test implementation while the component source remains unchanged:

```text
Production                               Test environment

Component                                Same component
    |                                         |
    v                                         v
Component OSAL API                       Same OSAL API
    |                                         |
    v                                         v
FreeRTOS / CMSIS / POSIX                 Test / host OSAL
                                              |
                                              v
                                      x86 Linux / Windows /
                                      isolated CI environment
```

This allows a component intended for an embedded target to be compiled and exercised on a completely different platform. The target MCU, scheduler and even the production RTOS do not have to be present for a large class of component tests.

A test backend can also make normally difficult OS conditions deterministic and controllable. For example, a test can deliberately force:

```text
Queue Put       -> success
Queue Put       -> queue full
Queue Pend      -> item available
Queue Pend      -> timeout / empty queue
Memory Malloc   -> allocation failure
Thread Create   -> creation failure
Time Get        -> deterministic synthetic time
Lock            -> controlled acquisition behavior
```

This is especially valuable for error paths that are difficult, slow or unsafe to reproduce on real hardware. Instead of manipulating the whole target system until a rare failure occurs, the test controls the OSAL boundary directly.

A component-specific interface makes these test doubles smaller as well. A queue-only component does not need a mock implementation of every timer, semaphore, scheduler and memory feature available in the RTOS. Its test environment implements only the contract that the component can actually use.

The goal is therefore not merely “the code can be ported.” The stronger property is:

> The same component can execute against different operating environments, including a deterministic test environment, without changing the component's OS-facing contract.

Future KIWI test infrastructure should build on this directly with host/test backends, deterministic fake services and regression tests for both normal and failure paths. See [`../test/README.md`](../test/README.md).

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

**The component owns the OSAL instance. The OSAL instance owns the operating-system resources. The component depends on its own OS abstraction contract, not on the operating-system API.**

Native handles should therefore remain inside OSAL resource registries instead of being scattered through component state structures. This keeps ownership and cleanup rules visible at one boundary and prevents native backend types from leaking into otherwise portable component code.

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

The registry is an implementation mechanism, not an invitation for the component to manipulate native resources directly. The generic API remains the component-facing contract.

## Generic layer and portable layer

The generated OSAL is split conceptually into two layers.

The **generic layer** defines the component-facing API, common types, lifecycle, validation, resource bookkeeping and semantic contract.

The **portable layer** maps that contract to a particular operating system. It is responsible for translating native handles, timeouts, priorities, context restrictions and return values without changing the meaning visible to the component.

The portable implementation is selected through a method table rather than by exposing native OS calls to the component. This makes the generic API the single semantic source of truth while allowing backend implementations to differ internally.

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

Because the same checks live at the OSAL boundary, they apply consistently to all operations accessing the operating system instead of being reimplemented ad hoc throughout the component.

## Tracing and diagnostics

All operating-system access performed through the OSAL passes through one controlled boundary. This makes it a natural place to trace the **flow of operations accessing the operating system**.

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

The same instrumentation also benefits tests: a host/test backend can validate call order, parameters and resource lifecycle without instrumenting the component itself.

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

All backends must preserve the generic semantics even when their native APIs differ. Backend-specific code is responsible for details such as:

- timeout conversion and infinite-wait representation;
- thread-priority mapping;
- ISR-safe versus thread-context operations;
- native resource creation/deletion;
- backend-specific lifecycle state;
- internal resource synchronization;
- backend-specific validation;
- translation of native failures into OSAL error semantics.

A port should not copy its native terminology into the generic API simply because that terminology is familiar to users of that OS. The generic contract is defined once; each backend adapts to it.

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

FreeRTOS is the current implemented backend. POSIX and CMSIS-RTOS2 remain planned ports and their directories currently provide project structure rather than complete implementations.

## Current generated primitive groups

The code generator currently exposes the following implemented API groups:

- queues;
- locks;
- threads;
- time;
- memory.

The primitive set is intentionally kept small while the architecture and generator contract are stabilized. Future primitive groups should be added only after their generic behavior and naming can be defined consistently across the intended backends.

That semantic definition is part of the feature, not documentation added after the implementation.
