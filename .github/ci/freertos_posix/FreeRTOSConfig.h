/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <assert.h>
#include <stddef.h>

/* Scheduler */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (1000000UL)
#define configTICK_RATE_HZ                      (1000U)
#define configMAX_PRIORITIES                    (16U)
#define configMINIMAL_STACK_SIZE                (128U)
#define configMAX_TASK_NAME_LEN                 (24U)
#define configIDLE_SHOULD_YIELD                 1
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* Allocation */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   (1024U * 1024U)
#define configAPPLICATION_ALLOCATED_HEAP        0

/* Kernel objects used by the generated full-set OSAL. */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_EVENT_GROUPS                  1
#define configUSE_STREAM_BUFFERS                1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configQUEUE_REGISTRY_SIZE               16
#define configUSE_QUEUE_SETS                    0
#define configUSE_CO_ROUTINES                   0

/* Software timers */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1U)
#define configTIMER_QUEUE_LENGTH                16
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2U)

/* Optional diagnostics kept disabled for the CI build. */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_TRACE_FACILITY                0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configUSE_NEWLIB_REENTRANT              0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configENABLE_BACKWARD_COMPATIBILITY     1

/* APIs used by the generated FreeRTOS port. */
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetSchedulerState          1

#define configASSERT(condition)                 assert(condition)

/*
 * The official GCC POSIX FreeRTOS port simulates interrupt handlers with host
 * signals and does not expose xPortIsInsideInterrupt().  The generated OSAL
 * uses that predicate to select task/ISR APIs.  The CI executable itself runs
 * only in normal task/process context, so make that context explicit here.
 */
#ifndef xPortIsInsideInterrupt
    #define xPortIsInsideInterrupt()    (0)
#endif

#endif /* FREERTOS_CONFIG_H */
