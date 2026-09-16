# KIWI OSAL Architecture

## Contents

1. [What is an OSAL?](#1-what-is-an-osal)
2. [What problem does an OSAL solve?](#2-what-problem-does-an-osal-solve)
3. [An OSAL is not free](#3-an-osal-is-not-free)
4. [One contract — one meaning](#4-one-contract--one-meaning)
5. [KIWI OSAL architectural model](#5-kiwi-osal-architectural-model)
6. [Global and component-scoped OSAL](#6-global-and-component-scoped-osal)
7. [Relationship to SOLID](#7-relationship-to-solid)
8. [Multi-instance model](#8-multi-instance-model)
9. [Resource ownership model](#9-resource-ownership-model)
10. [Resource registries and stable indices](#10-resource-registries-and-stable-indices)
11. [Runtime validation and assertions](#11-runtime-validation-and-assertions)
12. [Tracing and diagnostics](#12-tracing-and-diagnostics)
13. [Internal resource synchronization](#13-internal-resource-synchronization)
14. [Testability as part of the architecture](#14-testability-as-part-of-the-architecture)
15. [OS-specific implementation requirements](#15-os-specific-implementation-requirements)
16. [Why another abstraction layer?](#16-why-another-abstraction-layer)
17. [What KIWI OSAL deliberately does not do](#17-what-kiwi-osal-deliberately-does-not-do)
18. [When an OSAL may be unnecessary](#18-when-an-osal-may-be-unnecessary)
19. [Common objections](#19-common-objections)
20. [Current KIWI OSAL primitive groups](#20-current-kiwi-osal-primitive-groups)
21. [References and further reading](#21-references-and-further-reading)

---

## 1. What is an OSAL?

**OSAL means Operating System Abstraction Layer.**

Its primary purpose is to separate a software component from a specific operating system and its API by giving the component a stable contract for operating-system services such as threads, queues, synchronization primitives, timers, time, memory, and other OS resources.

At first glance an OSAL can look like a collection of wrappers that merely rename native calls:

| Native OS call | Example wrapper |
| --- | --- |
| `xQueueSend()` | `osalQueuePut()` |
| `xSemaphoreTake()` | `osalMutexLock()` |
| `vTaskDelay()` | `osalThreadDelay()` |

Renaming functions, however, is not a meaningful abstraction by itself.

Operating systems differ in more than function names. They differ in:

- object models;
- API signatures;
- native object identifier types;
- creation, ownership, and destruction rules;
- timeout units and infinite-wait representation;
- error models and return-value conventions;
- thread-context and ISR-context restrictions;
- scheduler and priority models;
- behavior that is explicit in one OS but implicit in another;
- historically accumulated terminology.

The central KIWI OSAL principle is therefore:

> **An OSAL abstracts semantics, not function names.**

The component should know **what** operation it needs, while the OS-specific implementation should decide **how** the current operating system performs that operation.

<p align="center"><img src="images/en/osal_architecture.png" alt="KIWI OSAL architectural model" width="50%"></p>

---

## 2. What problem does an OSAL solve?

Direct use of an operating-system API is natural while a component has only one target. A small program can keep `QueueHandle_t` values, call `xQueueSend()`, and directly implement the relevant FreeRTOS rules.

The problem appears as the component grows. The OS dependency starts spreading into state structures, error handling, timeout rules, initialization and teardown, and branches that depend on execution context.

The component gradually becomes a mixture of application logic, OS-specific logic, resource management, execution-context checks, time conversion, and error translation.

For protocol stacks, drivers, communications services, and middleware, implementation mechanics can eventually obscure the application-domain problem.

Instead of reasoning only about protocol or device state, the developer must repeatedly remember:

- whether an operation is legal from an ISR;
- whether a dedicated ISR API variant exists;
- which units a timeout uses;
- who owns an object;
- how partial initialization must be rolled back;
- how the current OS encodes failure or timeout.

KIWI OSAL moves these concerns to an explicit architectural boundary. The component depends on its own OS-facing contract instead of directly depending on FreeRTOS, POSIX, or another OS.

Portability is important, but it is not the only result. The same boundary also enables:

- isolated testing;
- uniform precondition validation;
- centralized resource bookkeeping;
- deterministic cleanup;
- tracing;
- lifecycle diagnostics;
- reduced OS-specific noise in component code.

---

## 3. An OSAL is not free

Every additional abstraction has a cost, and KIWI OSAL does not pretend otherwise.

Depending on configuration, the implementation may add:

- code size;
- per-instance state;
- a method table and indirect calls;
- argument and invariant checks;
- resource registries;
- tracing;
- another API that must be maintained.

In systems with strict memory, latency, or determinism constraints, these costs should be measured. Some diagnostic checks may reasonably be configurable.

A fair comparison must also include the code that would still exist without an OSAL. Error checks, timeout conversion, ISR dispatch, cleanup, and partial-initialization handling do not disappear. Without a shared boundary they are often duplicated throughout application code.

The useful engineering question is therefore not “does an OSAL have overhead?” but:

> **What is the measured cost of the abstraction, and which architectural properties does the system gain in return?**

For KIWI those properties include portability, testability, semantic consistency, explicit resource ownership, diagnostics, and per-instance OS policy.

---

## 4. One contract — one meaning

KIWI treats **semantic consistency** as part of the public API contract.

The same generic operation must have the same observable meaning regardless of the selected OS-specific implementation. This is what separates an abstraction layer from a collection of native API aliases.

A function name is not useful if one implementation blocks, another polls, a third uses different timeout units, and all of them expose unrelated error semantics.

### 4.1 Consistent vocabulary

KIWI uses a deliberately small vocabulary and avoids blindly copying the terminology of the first implemented OS.

| Term | Semantic meaning |
| --- | --- |
| `Create` | Create an OS resource, register it in the owning OSAL instance, and establish ownership. |
| `Delete` | Release a resource and remove it from the internal registry. |
| `Put` | Immediate producer-side operation; do not wait for capacity. |
| `Post` | Producer-side operation with an explicit maximum wait. |
| `Get` | Immediate retrieval of already available data or state. |
| `Wait` | A waiting operation; the exact signature depends on the primitive. |
| `Pend` | Retrieve or acquire with an explicit timeout. |
| `TryLock` | Attempt a mutex lock without waiting. |
| `Lock` | Acquire a mutex with an unbounded wait. |
| `PendLock` | Acquire a mutex with an explicit timeout. |
| `Unlock` | Release a previously acquired mutex. |
| `Set` / `Clear` | Set or clear state bits. |
| `Start` / `Stop` | Start or stop an existing timer. |
| `Reset` | Return an object to its defined operational state or restart a timer period. |

Consistency does not mean forcing every primitive into an identical function shape. It means preserving one meaning whenever a term is used.

### 4.2 Core semantic matrix

| Primitive group | Immediate | Timed wait | Infinite wait | Other operations |
| --- | --- | --- | --- | --- |
| Queue, producer | `Put` | `Post(timeout)` | `Post(TEMPLATE_OSAL_INFINITY_TOUT)` | `Reset` |
| Queue, consumer | `Get` | `Pend` | `Wait` | `Reset` |
| Stream Buffer, producer | `Put` | `Post(timeout)` | `Post(TEMPLATE_OSAL_INFINITY_TOUT)` | `Reset` |
| Stream Buffer, consumer | `Get` | `Pend` | `Wait` | `Reset` |
| Counting Semaphore | `Pend(0)` | `Pend(timeout)` | `Wait` | `Post`, `CountGet` |
| Mutex | `TryLock` | `PendLock` | `Lock` | `Unlock` |
| Event Flags | `Get` | `Wait(..., timeout)` | `Wait(..., TEMPLATE_OSAL_INFINITY_TOUT)` | `Set`, `Clear` |
| Software Timer | — | — | — | `Start`, `Stop`, `Reset` |

A notable example is Event Flags: `Wait` naturally combines a flag mask, `WAIT_ANY/WAIT_ALL`, clear behavior, and a timeout. KIWI therefore treats its vocabulary as semantic conventions, not as a rigid grammar that every primitive must obey identically.

---

## 5. KIWI OSAL architectural model

KIWI OSAL is conceptually split into two parts.

### 5.1 Generic layer

The generic layer defines:

- the component-facing API;
- common types;
- operation semantics;
- lifecycle rules;
- resource ownership;
- internal registries;
- common validation;
- dispatch to the selected OS implementation.

### 5.2 OS-specific implementation

The OS-specific implementation maps the generic contract onto the current operating system:

| OSAL side | Operating-system side |
| --- | --- |
| OSAL timeout | native timeout representation |
| OSAL priority level | scheduler priority |
| OSAL object | native OS object |
| native operation result | OSAL error |
| generic operation | correct native API call |

FreeRTOS, POSIX, and a future implementation may use completely different mechanisms internally. They are required to preserve the same observable generic behavior.

Conceptually this is an interface plus interchangeable implementations. In C it is expressed through composition and an explicit method table rather than language-level inheritance.

The dependency direction matters: application logic depends on the generic OSAL contract, while the OS-specific implementation adapts to that contract. The component should not adapt itself to quirks of the selected operating system. The architecture figure above shows this dependency explicitly.

---

## 6. Global and component-scoped OSAL

One of the defining KIWI principles is that OSAL scope is owned by the software component that uses it.

That does not make a system-wide OSAL wrong. Global and component-scoped designs are different points on a scope continuum ranging from an individual component through a subsystem and application to an entire software platform.

<p align="center"><img src="images/en/global_vs_component_osal.png" alt="Global and component-scoped OSAL" width="50%"></p>

### 6.1 Global OSAL

A single shared OSAL works well when a project has a mature architecture and one stable system contract.

Typical advantages:

- one configuration point;
- less repeated per-instance state;
- one system interface for the whole application;
- simple binding model.

As the project grows, however:

- the interface accumulates requirements from many subsystems;
- contract changes become more expensive;
- coupling increases;
- every new OS implementation must support a larger feature surface;
- local policy changes become harder.

### 6.2 Component-scoped OSAL

A component-scoped OSAL contains only the service groups needed by that component. This model is especially suitable for:

- drivers;
- libraries;
- protocol stacks;
- middleware;
- communications services;
- frameworks;
- multiply instantiated components.

Advantages include:

- smaller API surface;
- explicit ownership boundaries;
- easier standalone portability;
- simpler host/test implementations;
- the ability to use strict validation locally;
- per-instance OS policy.

The model also has costs:

- multiple instances require state;
- some infrastructure may be repeated;
- poor component boundaries can make the design unnecessarily fragmented.

KIWI's goal is not to reject global abstractions, but to make abstraction scope an explicit architectural choice.

---

## 7. Relationship to SOLID

KIWI OSAL maps most clearly to three SOLID principles.

### 7.1 Interface Segregation Principle

A component should not depend on methods it does not use. The generator therefore includes only the selected primitive groups.

### 7.2 Dependency Inversion Principle

High-level component logic depends on its generic OSAL contract. FreeRTOS or POSIX implementations sit below that contract and adapt to it.

### 7.3 Liskov Substitution Principle

Backends are meaningfully substitutable only when they preserve observable semantics. If FreeRTOS and POSIX interpret the same generic operation differently, substitution is not correct. This is why “one contract — one meaning” is an architectural requirement rather than naming style.

It is more useful to state these concrete relationships than to claim that OSAL mechanically embodies every SOLID principle.

---

## 8. Multi-instance model

Component-scoped OSAL naturally supports multiple instances of the same component.

Two communications-stack instances can share one FreeRTOS implementation while having:

- separate registries;
- different resource sets;
- different priority policies;
- different SMP affinity policies;
- independent lifecycles.

Multiple OSAL instances may still use the same OS-specific implementation: their state, registries, and parameters are separate while the implementation code remains shared. The figure in section 6 illustrates this model.

This is stronger than a global set of wrapper functions because the OS-facing policy becomes part of the instance rather than only a property of the whole program.

---

## 9. Resource ownership model

The central ownership invariant is:

> **The component owns the OSAL instance. The OSAL instance owns the operating-system resources created through it.**

<p align="center"><img src="images/en/ownership_model.png" alt="Resource ownership model" width="50%"></p>

This makes resource lifetime visible and enforceable.

Queue, mutex, semaphore, thread, and timer identifiers exposed to the component are generic OSAL identifiers. A FreeRTOS-native object type should not become part of portable component state.

For example, `QueueHandle_t` is a FreeRTOS-specific detail, while `Template_osalQueueHandle_t` is the generic opaque queue identifier exposed at the OSAL boundary.

Memory is deliberately different. An allocated memory block is an addressable region, so KIWI uses `memPtr`, not an artificial `MemHandle`.

Terminology reflects architecture:

- a **handle / object identifier** is an opaque resource identity;
- a **pointer** is an address that the component actually uses as memory.

---

## 10. Resource registries and stable indices

Resources created through an OSAL instance are tracked in fixed-capacity internal registries.

<p align="center"><img src="images/en/resource_registry.png" alt="OSAL resource registry" width="50%"></p>

This provides several properties.

### 10.1 Verifiable ownership

The OSAL can verify that a resource belongs to the current instance. Cross-instance misuse can therefore be detected at the boundary instead of turning into later OS corruption.

### 10.2 Deterministic cleanup

At teardown, the instance knows which resources were created through it and can perform controlled cleanup.

### 10.3 Bounded resource model

Registry capacity is configured explicitly:

```c
TEMPLATE_OSAL_QUEUE_SLOTS_NUM
TEMPLATE_OSAL_THREAD_SLOTS_NUM
TEMPLATE_OSAL_MEM_SLOTS_NUM
```

The maximum tracked resource count is therefore known from configuration, which is useful in embedded systems.

### 10.4 Diagnostics

The registry boundary can detect:

- unknown resource identifiers;
- foreign-instance resources;
- double delete;
- registry exhaustion;
- lifecycle violations;
- leftover resources during teardown;
- registration failures.

### 10.5 A registry is not a security sandbox

Code running in the same address space with the same privileges may still bypass OSAL and call native OS APIs directly.

The registry is therefore primarily a controlled resource domain and an early misuse-detection mechanism. Strong isolation requires additional facilities such as MPU/MMU protection, privilege separation, or process isolation.

---

## 11. Runtime validation and assertions

The OSAL boundary is a natural place to enforce the component's operating-system contract, but not all checks belong to the same category.

### 11.1 Programming invariant violations

Examples:

- a pointer is NULL where the contract says it cannot be;
- an initialized instance has no method table;
- an internal index is out of range;
- an impossible internal state is observed.

These are programming errors and are natural assertion candidates.

### 11.2 Recoverable runtime conditions

Examples:

- queue full;
- queue empty;
- timeout;
- allocation failure;
- registry exhaustion.

These are operational outcomes and should be represented by `Template_osalErr_e` rather than assertions.

### 11.3 Checks that are part of the operation itself

Some checks directly determine the correct native call.

For example, a FreeRTOS operation may need to choose between a normal API and a `FromISR` variant. Execution-context detection is then not optional diagnostics; it participates in correct behavior.

The general rule is:

> **Not every OSAL check is optional validation. Some checks are part of the backend operation semantics.**

---

## 12. Tracing and diagnostics

Most OS interaction performed by the component passes through one controlled boundary. That makes OSAL a natural observation point.

Useful trace points include:

- resource creation and deletion;
- queue and stream-buffer operations;
- mutex operations;
- semaphore operations;
- event-flag operations;
- thread and timer lifecycle;
- memory allocation and release;
- timeouts;
- backend failures;
- invalid execution context;
- teardown and cleanup.

This is especially useful for timing-sensitive failures. A debugger breakpoint changes scheduling and event timing and can hide races that occur in normal execution. Tracing provides a way to observe the OS-facing flow without necessarily stopping the system.

The trace hook should be configurable and replaceable: a project may route it to `printf`, a project logger, binary trace, or another diagnostic transport.

The tracing implementation must also respect execution context. An OSAL call being legal from an ISR does not imply that an arbitrary `printf()` is safe inside the trace hook.

---

## 13. Internal resource synchronization

Resource registries are shared state and must be synchronized independently from component-visible mutexes.

Each OS-specific implementation therefore owns an internal resource-management mutex.

It:

- protects free-slot search;
- protects registration and release;
- protects bookkeeping;
- participates in teardown;
- is not stored in the component-visible mutex registry;
- is created directly by the OS-specific implementation.

The last point avoids a recursive dependency: creating the internal registry mutex through the public mutex API would require the registry to already be operational.

This synchronization belongs to the individual OSAL instance rather than to global mutable state shared by all instances.

---

## 14. Testability as part of the architecture

Portability and testability are two consequences of the same boundary.

If a component depends only on its OSAL contract, the production OS implementation can be replaced without changing component source code.

<p align="center"><img src="images/en/testability.png" alt="KIWI OSAL testability" width="50%"></p>

Two different test-oriented implementations are useful.

### 14.1 Host implementation

A future POSIX implementation can run the same component on a workstation. Threads are real threads, queues remain real communication primitives, and mutexes provide real mutual exclusion.

This is useful for:

- integration tests;
- system tests;
- simulation;
- long-running tests;
- CI execution;
- multi-component interaction tests.

### 14.2 Deterministic test implementation

A deterministic test implementation has a different purpose: let the test harness deliberately control OS outcomes.

| Controlled operation | Forced result |
| --- | --- |
| Queue: `Put` | success |
| Queue: `Put` | queue full |
| Queue: `Pend` | timeout |
| Memory: `Malloc` | allocation failure |
| Thread: `Create` | creation failure |
| Time: `TimeMsGet` | synthetic time |

This makes rare and difficult failure paths reproducible.

Test-control facilities belong to the test harness, not to the component-facing production API. The component should continue to observe only normal OSAL calls and normal OSAL results.

---

## 15. OS-specific implementation requirements

Every OS-specific implementation must preserve generic semantics even when native APIs differ.

Typical responsibilities include:

- timeout conversion;
- native infinite-wait representation;
- priority mapping;
- thread-context versus ISR-context dispatch;
- native resource creation and deletion;
- native-failure to OSAL-error translation;
- backend lifecycle state;
- internal registry synchronization;
- OS-specific parameter validation;
- instance-parameter normalization.

### 15.1 Defaults and per-instance parameters

An OS implementation may provide compile-time default policy while allowing an instance to override selected parts.

The contract should be explicit:

| `param` state | Behavior |
| --- | --- |
| `param == NULL` | use the default OS-specific behavior |
| `param != NULL` | validate the structure and apply explicitly selected parameter groups |

An invalid explicit configuration must not silently fall back to defaults. “Not provided” and “provided but invalid” are different states.

For FreeRTOS, priority mapping from `LOW/NORMAL/HIGH/CRITICAL` to scheduler priorities is one such policy. A custom mapping is accepted only when `LOW` is above `tskIDLE_PRIORITY`, every mapped value is below `configMAX_PRIORITIES`, and the sequence is strictly increasing: `LOW < NORMAL < HIGH < CRITICAL`. In SMP builds, thread-slot affinity may be another per-instance policy.

When MPU support is enabled, the FreeRTOS instance may provide one shared `MemoryRegion_t[portNUM_CONFIGURABLE_REGIONS]` policy. Region validation rejects zero sizes, address-range overflow, and pairwise overlap. For known MPU models detected from capabilities exposed by the selected FreeRTOS headers, the port also validates architecture-specific minimum size, granularity, and alignment rules. An otherwise unknown MPU model requires an explicit platform validator instead of being silently accepted.

### 15.2 Infinite wait

The generic layer uses `TEMPLATE_OSAL_INFINITY_TOUT`. A FreeRTOS implementation maps that value to FreeRTOS's own `portMAX_DELAY`.

The generic OSAL should not invent a native representation for the operating system.

---

## 16. Why another abstraction layer?

Embedded systems already contain HALs, BSPs, CMSIS interfaces, RTOS wrappers, vendor SDKs, and middleware frameworks. Asking “why another layer?” is reasonable.

The key question is **scope**.

| Abstraction | Primary scope |
| --- | --- |
| Vendor HAL | Hardware blocks and families inside a vendor ecosystem |
| BSP | A particular board and its hardware configuration |
| CMSIS-RTOS2 | Standardized common RTOS services for Arm Cortex systems |
| NASA OSAL | System-level OS abstraction for the cFS software platform |
| KIWI OSAL | OS-facing contract of an individual software component |

CMSIS-RTOS2 is a mature generic RTOS interface designed to reduce dependence on a specific RTOS implementation. KIWI does not deny that value. Its scope is different: KIWI generates a component-owned contract containing only the primitive groups that the component requires, together with ownership, registries, per-instance configuration, and project-specific semantics.

NASA OSAL is a strong example of a system-wide OS abstraction. NASA describes it as a library that isolates embedded software from the underlying RTOS and provides a generic interface to OS services. That demonstrates the architectural value of OS abstraction, but NASA OSAL's scope is much broader than KIWI's component-local contract.

The useful question is therefore not “does another abstraction already exist?” but:

> **Does that abstraction's contract boundary match the portability boundary of this component?**

If it does, another layer may be unnecessary. If it does not, the architectural problem remains.

---

## 17. What KIWI OSAL deliberately does not do

KIWI does not try to expose the union of all features from every supported operating system.

If FreeRTOS offers five variants of an operation, the generic API does not automatically need five methods.

Otherwise the abstraction would gradually become the union of FreeRTOS, POSIX, CMSIS, and every future OS feature, turning into another large system API.

The generic contract should grow only when a genuinely new **component-level semantic requirement** appears.

KIWI OSAL is also not:

- a complete security sandbox;
- a replacement for MPU/MMU protection;
- a universal scheduler;
- a hardware-driver abstraction;
- a promise that every OS-specific feature can be made portable without trade-offs.

---

## 18. When an OSAL may be unnecessary

An OSAL is not mandatory for every embedded module.

A separate abstraction layer may be unjustified when:

- the code is intentionally and permanently bound to one RTOS;
- a unique native feature is itself part of the component's public contract;
- the component is so small that a separate OS boundary adds no practical value;
- measured memory or latency limits make the chosen abstraction implementation unacceptable;
- the generic interface would inevitably mirror the native OS API one-to-one.

The last case is especially important. If the layer merely reproduces the FreeRTOS API under another prefix, it is not a meaningful abstraction; it is renaming.

---

## 19. Common objections

### “Why not just use FreeRTOS directly?”

That can be a perfectly reasonable choice for code intentionally tied to one FreeRTOS application with no need for independent host testing or reuse. KIWI is useful when the OS dependency itself needs an explicit boundary.

### “Doesn't the method table add overhead?”

It does add a measurable indirect-call cost. That cost should be measured where it matters. The alternative must also be measured honestly: duplicated validation, native types in component state, harder testing, and more expensive porting have costs too.

### “Why not use CMSIS-RTOS2?”

Sometimes CMSIS-RTOS2 is the best choice. It standardizes a useful common RTOS API for Arm Cortex systems. KIWI solves a different problem: a generated component-owned contract, selected API groups, resource ownership, registries, and per-instance policy. The domains overlap but are not identical.

### “Why keep a registry if the component can store handles?”

It can. The registry exists when verifiable ownership, bounded resources, deterministic teardown, diagnostics, and centralized tracing are valuable.

### “Why not one global OSAL?”

Sometimes one global OSAL is exactly right. The component-scoped model is useful when a component must be independently portable, independently testable, or differently configured per instance.

### “Why not just mock native RTOS functions?”

That is possible. A dedicated OSAL boundary makes the seam explicit and supports both a realistic host implementation and a deterministic fault-injection implementation without inserting test-only behavior into component source code.

---

## 20. Current KIWI OSAL primitive groups

The current generator and FreeRTOS implementation support:

| Group | Main operations |
| --- | --- |
| Queue | `Create`, `Delete`, `Put`, `Post`, `Get`, `Wait`, `Pend`, `Reset`, `HandleGet` |
| Stream Buffer | `Create`, `Delete`, `Put`, `Post`, `Get`, `Wait`, `Pend`, `Reset`, `HandleGet` |
| Mutex | `Create`, `Delete`, `TryLock`, `Lock`, `PendLock`, `Unlock`, `HandleGet` |
| Counting Semaphore | `Create`, `Delete`, `Wait`, `Pend`, `Post`, `CountGet`, `HandleGet` |
| Event Flags | `Create`, `Delete`, `Set`, `Clear`, `Get`, `Wait`, `HandleGet` |
| Thread | `Create`, `Delete`, `Suspend`, `Resume`, `Delay`, `DelayUntil`, `Exit`, `HandleGet` |
| Critical Section | `Enter`, `Exit` |
| Software Timer | `Create`, `Delete`, `Start`, `Stop`, `Reset`, `HandleGet` |
| Time | `TimeMsGet` |
| Memory | `Malloc`, `Free`, `MemPtrGet` |

The generic thread priority model uses four fixed levels: `LOW` → `NORMAL` → `HIGH` → `CRITICAL`.

The enum values are explicit and stable; each OS implementation maps them onto its scheduler model.

OSAL error codes likewise use explicit sequential numeric values. Existing assignments must not be changed or reused so stored logs, diagnostics, and lookup tables remain backward compatible.

---

## 21. References and further reading

KIWI is its own project architecture, but it builds on well-established ideas around dependency inversion, interface segregation, OS abstraction, and test seams.

1. **NASA Core Flight System OSAL** — a production example of an operating-system abstraction layer:
   <https://github.com/nasa/osal>

2. **NASA Software Catalog — Operating System Abstraction Layer** — overview of OS isolation and multiple OS implementations, including Linux/POSIX for development and testing:
   <https://software.nasa.gov/software/GSC-18370-1>

3. **Arm CMSIS-RTOS2** — a standardized generic RTOS API for Arm Cortex systems:
   <https://arm-software.github.io/CMSIS_6/main/RTOS2/index.html>

4. **FreeRTOS documentation** — operating-system primitives, SMP, and core-affinity facilities:
   <https://www.freertos.org/Documentation/>

5. James W. Grenning, **Test-Driven Development for Embedded C**, Pragmatic Bookshelf — test doubles, dependency breaking, and embedded testing away from target hardware.

6. Robert C. Martin's work on **SOLID**, Dependency Inversion, and Interface Segregation — architectural background for depending on contracts rather than concrete implementations.

---

## Summary

KIWI OSAL is not an attempt to hide FreeRTOS behind another set of names. Its purpose is to establish a controlled semantic boundary between a software component and its operating environment.

That boundary:

- separates application intent from system-call mechanics;
- makes resource ownership explicit;
- centralizes validation and tracing;
- supports multi-instance configuration;
- simplifies migration to another OS;
- provides a natural test seam;
- avoids exporting the full native OS API into component code.

The architecture can be summarized in one sentence:

> **The component states what it requires from the operating environment. The OSAL implementation decides how the selected operating system fulfills that requirement.**
