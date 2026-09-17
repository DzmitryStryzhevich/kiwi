/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

/**
 * \file     template_osal_freertos.c
 * \brief    FreeRTOS OSAL port for Template.
 * \details  FreeRTOS implementation of the component-scoped Template OSAL contract.
 */

//===============================================================================[ INCLUDE ]========================================================================================

#include "template_osal_freertos.h"
#include "template_osal.h"

#include "FreeRTOS.h"
#include "task.h"
// BEGIN QUEUE
#include "queue.h"
// END QUEUE
#include "semphr.h"
// BEGIN STREAM_BUFFER
#include "stream_buffer.h"
// END STREAM_BUFFER
// BEGIN EVENT_FLAGS
#include "event_groups.h"
// END EVENT_FLAGS
// BEGIN SOFTWARE_TIMER
#include "timers.h"
// END SOFTWARE_TIMER

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

//=====================================================================[ INTERNAL MACRO DEFINITIONS ]===============================================================================

/**
 * \def   TEMPLATE_OSAL_FREERTOS_ASSERT
 * \brief Assertion macro for the FreeRTOS OSAL backend.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_ASSERT
    #if defined(TEMPLATE_OSAL_ASSERT)
        #define TEMPLATE_OSAL_FREERTOS_ASSERT(cond)    TEMPLATE_OSAL_ASSERT(cond)
    #elif defined(TEMPLATE_ASSERT)
        #define TEMPLATE_OSAL_FREERTOS_ASSERT(cond)    TEMPLATE_ASSERT(cond)
    #else
        #include <assert.h>
        #define TEMPLATE_OSAL_FREERTOS_ASSERT(cond)    assert(cond)
    #endif
#endif

/**
 * \def   TEMPLATE_OSAL_FREERTOS_TRACE
 * \brief Tracing macro for the FreeRTOS OSAL backend.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_TRACE
    #if defined(TEMPLATE_OSAL_TRACE)
        #define TEMPLATE_OSAL_FREERTOS_TRACE(...)    TEMPLATE_OSAL_TRACE(__VA_ARGS__)
    #elif defined(TEMPLATE_TRACE)
        #define TEMPLATE_OSAL_FREERTOS_TRACE(...)    TEMPLATE_TRACE(__VA_ARGS__)
    #else
        #define TEMPLATE_OSAL_FREERTOS_TRACE(...)    ((void)0)
    #endif
#endif

// BEGIN THREAD
#ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
    /*
     * Detect known MPU models from the selected FreeRTOS port headers.
     * Detection is based on the MPU capabilities exported by the selected
     * FreeRTOS port rather than MCU-vendor macros.
     */
    #if defined(portARMV8M_MINOR_VERSION)
        #define TEMPLATE_OSAL_FREERTOS_MPU_MODEL_ARMV8M
        #define TEMPLATE_OSAL_FREERTOS_MPU_ALIGNMENT    (32u)
        #define TEMPLATE_OSAL_FREERTOS_MPU_MIN_SIZE     (32u)
    #elif defined(portMPU_REGION_SIZE_256B) && \
    !defined(portMPU_REGION_SIZE_32B)
        #define TEMPLATE_OSAL_FREERTOS_MPU_MODEL_ARMV6M
        #define TEMPLATE_OSAL_FREERTOS_MPU_MIN_SIZE     (256u)
    #elif defined(portMPU_RASR_TEX_S_C_B_LOCATION) || \
    defined(portMPU_REGION_SIZE_32B)
        #define TEMPLATE_OSAL_FREERTOS_MPU_MODEL_CLASSIC_RASR
        #define TEMPLATE_OSAL_FREERTOS_MPU_MIN_SIZE     (32u)
    #elif !defined(TEMPLATE_OSAL_FREERTOS_MPU_REGION_PLATFORM_VALIDATE)
        #error "Unsupported FreeRTOS MPU model: provide TEMPLATE_OSAL_FREERTOS_MPU_REGION_PLATFORM_VALIDATE(region)"
    #endif /* if defined(portARMV8M_MINOR_VERSION) */
#endif /* ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU */

#ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP
    #if !defined(configNUMBER_OF_CORES) ||\
    (configNUMBER_OF_CORES <= 1)
        #error "TEMPLATE_OSAL_FREERTOS_USE_SMP requires FreeRTOS SMP with configNUMBER_OF_CORES > 1"
    #endif
    #if !defined(configUSE_CORE_AFFINITY) ||\
    (configUSE_CORE_AFFINITY != 1)
        #error "TEMPLATE_OSAL_FREERTOS_USE_SMP requires configUSE_CORE_AFFINITY == 1"
    #endif
#endif
// END THREAD

//====================================================================[ INTERNAL DATA TYPES DEFINITIONS ]===========================================================================

/* None */

//===============================================================[ INTERNAL FUNCTIONS AND OBJECTS DECLARATION ]=====================================================================

/**
 * \brief Initialize FreeRTOS-specific instance parameters with the default port policy.
 */
static void template_osalFreertosParamDefaultSet(Template_osalFreertosParam_s *const param);

/**
 * \brief Validate explicitly supplied FreeRTOS-specific instance parameters.
 */
static bool template_osalFreertosParamValidate(const Template_osalFreertosParam_s *const param);

/**
 * \brief Validate and apply FreeRTOS-specific instance parameters over the default policy.
 */
static bool template_osalFreertosParamApply(Template_osalFreertosParam_s *const dst,
                                            const Template_osalFreertosParam_s *const src);

// BEGIN THREAD
#ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU

/**
 * \brief Validate one FreeRTOS MPU memory region.
 */
    static bool template_osalFreertosMpuRegionValidate(const MemoryRegion_t *const region);
#endif
// END THREAD

// BEGIN QUEUE
/*-------------------------------- Queues ---------------------------------*/

/**
 * \brief Create a FreeRTOS queue.
 */
static Template_osalErr_e template_osalFreertosQueueCreate(void *const osal,
                                                           const size_t queueItemSize,
                                                           const size_t queueDepth,
                                                           Template_osalQueueHandle_t *const queueHandle);

/**
 * \brief Delete a FreeRTOS queue.
 */
static Template_osalErr_e template_osalFreertosQueueDelete(void *const osal,
                                                           const Template_osalQueueHandle_t queueHandle);

/**
 * \brief Put an item into a FreeRTOS queue without waiting for capacity.
 */
static Template_osalErr_e template_osalFreertosQueueItemPut(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            const void *const queueItemPtr);

/**
 * \brief Post an item to a registered queue with the requested timeout.
 */
static Template_osalErr_e template_osalFreertosQueueItemPost(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             const void *const queueItemPtr,
                                                             const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Retrieve an already available item from a registered queue without waiting.
 */
static Template_osalErr_e template_osalFreertosQueueItemGet(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            void *const queueItemPtr);

/**
 * \brief Wait indefinitely for an item from a registered queue.
 */
static Template_osalErr_e template_osalFreertosQueueItemWait(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             void *const queueItemPtr);

/**
 * \brief Pend an item from a FreeRTOS queue.
 */
static Template_osalErr_e template_osalFreertosQueueItemPend(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             void *const queueItemPtr,
                                                             const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Reset a FreeRTOS queue.
 */
static Template_osalErr_e template_osalFreertosQueueReset(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle);

// END QUEUE

// BEGIN STREAM_BUFFER
/*----------------------------- Stream buffers -----------------------------*/

/**
 * \brief Create a FreeRTOS stream buffer and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalFreertosStreamBufferCreate(void *const osal,
                                                                  const size_t bufferSizeBytes,
                                                                  const size_t triggerLevelBytes,
                                                                  Template_osalStreamBufferHandle_t *const streamBufferHandle);

/**
 * \brief Delete a registered FreeRTOS stream buffer.
 */
static Template_osalErr_e template_osalFreertosStreamBufferDelete(void *const osal,
                                                                  const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Put bytes into a registered stream buffer without waiting for free capacity.
 */
static Template_osalErr_e template_osalFreertosStreamBufferPut(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                               const void *const data,
                                                               const size_t dataLengthBytes,
                                                               size_t *const bytesPut);

/**
 * \brief Put bytes into a registered stream buffer using the requested timeout.
 */
static Template_osalErr_e template_osalFreertosStreamBufferPost(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                const void *const data,
                                                                const size_t dataLengthBytes,
                                                                const Template_osalTimeMs_t timeoutMs,
                                                                size_t *const bytesPut);

/**
 * \brief Get already available bytes from a registered stream buffer without waiting.
 */
static Template_osalErr_e template_osalFreertosStreamBufferGet(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                               void *const data,
                                                               const size_t dataLengthBytes,
                                                               size_t *const bytesGet);

/**
 * \brief Wait indefinitely for bytes from a registered stream buffer.
 */
static Template_osalErr_e template_osalFreertosStreamBufferWait(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                void *const data,
                                                                const size_t dataLengthBytes,
                                                                size_t *const bytesGet);

/**
 * \brief Get bytes from a registered stream buffer using the requested timeout.
 */
static Template_osalErr_e template_osalFreertosStreamBufferPend(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                void *const data,
                                                                const size_t dataLengthBytes,
                                                                const Template_osalTimeMs_t timeoutMs,
                                                                size_t *const bytesGet);

/**
 * \brief Reset a registered FreeRTOS stream buffer to the empty state.
 */
static Template_osalErr_e template_osalFreertosStreamBufferReset(void *const osal,
                                                                 const Template_osalStreamBufferHandle_t streamBufferHandle);
// END STREAM_BUFFER

// BEGIN MUTEX
/*-------------------------------- Mutexes ----------------------------------*/

/**
 * \brief Create a recursive FreeRTOS mutex.
 */
static Template_osalErr_e template_osalFreertosMutexCreate(void *const osal,
                                                           Template_osalMutexHandle_t *const mutexHandle);

/**
 * \brief Delete a recursive FreeRTOS mutex.
 */
static Template_osalErr_e template_osalFreertosMutexDelete(void *const osal,
                                                           const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Lock a recursive FreeRTOS mutex and wait indefinitely.
 */
static Template_osalErr_e template_osalFreertosMutexLock(void *const osal,
                                                         const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Try to lock a recursive FreeRTOS mutex without waiting.
 */
static Template_osalErr_e template_osalFreertosMutexTryLock(void *const osal,
                                                            const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Lock a recursive FreeRTOS mutex using the requested timeout.
 */
static Template_osalErr_e template_osalFreertosMutexPendLock(void *const osal,
                                                             const Template_osalMutexHandle_t mutexHandle,
                                                             const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Unlock a recursive FreeRTOS mutex.
 */
static Template_osalErr_e template_osalFreertosMutexUnlock(void *const osal,
                                                           const Template_osalMutexHandle_t mutexHandle);
// END MUTEX

// BEGIN SEMAPHORE
/*--------------------------- Counting semaphores --------------------------*/

/**
 * \brief Create a FreeRTOS counting semaphore and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalFreertosSemaphoreCreate(void *const osal,
                                                               const Template_osalSemaphoreCount_t maxCount,
                                                               const Template_osalSemaphoreCount_t initialCount,
                                                               Template_osalSemaphoreHandle_t *const semaphoreHandle);

/**
 * \brief Delete a registered FreeRTOS counting semaphore.
 */
static Template_osalErr_e template_osalFreertosSemaphoreDelete(void *const osal,
                                                               const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Wait indefinitely for one count from a registered counting semaphore.
 */
static Template_osalErr_e template_osalFreertosSemaphoreWait(void *const osal,
                                                             const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Pend for one count from a registered counting semaphore using the requested timeout.
 */
static Template_osalErr_e template_osalFreertosSemaphorePend(void *const osal,
                                                             const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                             const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Post one count to a registered counting semaphore.
 */
static Template_osalErr_e template_osalFreertosSemaphorePost(void *const osal,
                                                             const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Read the current count of a registered FreeRTOS counting semaphore.
 */
static Template_osalErr_e template_osalFreertosSemaphoreCountGet(void *const osal,
                                                                 const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                                 Template_osalSemaphoreCount_t *const semaphoreCount);
// END SEMAPHORE

// BEGIN EVENT_FLAGS
/*-------------------------------- Event flags ------------------------------*/

/**
 * \brief Create a FreeRTOS event group and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalFreertosEventFlagsCreate(void *const osal,
                                                                Template_osalEventFlagsHandle_t *const eventFlagsHandle);

/**
 * \brief Delete a registered FreeRTOS event group.
 */
static Template_osalErr_e template_osalFreertosEventFlagsDelete(void *const osal,
                                                                const Template_osalEventFlagsHandle_t eventFlagsHandle);

/**
 * \brief Set bits in a registered FreeRTOS event group.
 */
static Template_osalErr_e template_osalFreertosEventFlagsSet(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                             const uint32_t flags);

/**
 * \brief Clear bits in a registered FreeRTOS event group.
 */
static Template_osalErr_e template_osalFreertosEventFlagsClear(void *const osal,
                                                               const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                               const uint32_t flags);

/**
 * \brief Read currently set bits from a registered FreeRTOS event group.
 */
static Template_osalErr_e template_osalFreertosEventFlagsGet(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                             uint32_t *const flags);

/**
 * \brief Wait for any or all requested bits in a registered FreeRTOS event group.
 */
static Template_osalErr_e template_osalFreertosEventFlagsWait(void *const osal,
                                                              const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                              const uint32_t flags,
                                                              const Template_osalEventFlagsOptions_e options,
                                                              const Template_osalTimeMs_t timeoutMs,
                                                              uint32_t *const actualFlags);

// END EVENT_FLAGS

// BEGIN THREAD
/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a FreeRTOS task.
 */
static Template_osalErr_e template_osalFreertosThreadCreate(void *const osal,
                                                            Template_osalThreadHandle_t *const threadHandle,
                                                            Template_osalThreadAttr_s threadAttr);

/**
 * \brief Delete a FreeRTOS task.
 */
static Template_osalErr_e template_osalFreertosThreadDelete(void *const osal,
                                                            const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Suspend a FreeRTOS task.
 */
static Template_osalErr_e template_osalFreertosThreadSuspend(void *const osal,
                                                             const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Resume a FreeRTOS task.
 */
static Template_osalErr_e template_osalFreertosThreadResume(void *const osal,
                                                            const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Delay the calling FreeRTOS task.
 */
static Template_osalErr_e template_osalFreertosThreadDelay(void *const osal,
                                                           const Template_osalTimeMs_t delayMs);

/**
 * \brief Delay the calling FreeRTOS task until the next periodic wake-up point.
 */
static Template_osalErr_e template_osalFreertosThreadDelayUntil(void *const osal,
                                                                Template_osalTimeMs_t *const previousWakeTimeMs,
                                                                const Template_osalTimeMs_t periodMs);

/**
 * \brief Terminate the calling FreeRTOS task.
 */
static void template_osalFreertosThreadExit(void *const osal);

/**
 * \brief Validate FreeRTOS thread attributes.
 */
static bool template_osalFreertosThreadAttrValidate(const Template_osalThreadAttr_s *const threadAttr);

// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ------------------------*/

/**
 * \brief Enter a FreeRTOS task-context critical section.
 */
static Template_osalErr_e template_osalFreertosCriticalSectionEnter(void *const osal);

/**
 * \brief Exit a previously entered FreeRTOS task-context critical section.
 */
static Template_osalErr_e template_osalFreertosCriticalSectionExit(void *const osal);

// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------------- Software timers -------------------------*/

/**
 * \brief Create a FreeRTOS software timer and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerCreate(void *const osal,
                                                                   Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                   Template_osalSoftwareTimerAttr_s timerAttr);

/**
 * \brief Delete a registered FreeRTOS software timer.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerDelete(void *const osal,
                                                                   const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Start a registered FreeRTOS software timer.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStart(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Stop a registered FreeRTOS software timer.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStop(void *const osal,
                                                                 const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Reset and restart the period of a registered FreeRTOS software timer.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerReset(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Dispatch a native FreeRTOS timer callback to the component callback.
 */
static void template_osalFreertosSoftwareTimerCallback(TimerHandle_t timerHandle);

// END SOFTWARE_TIMER

/*--------------------------------- Time ----------------------------------*/

// BEGIN TIME

/**
 * \brief Retrieve the current FreeRTOS system time in milliseconds.
 */
static Template_osalErr_e template_osalFreertosTimeMsGet(void *const osal,
                                                         Template_osalTimeMs_t *const osTimeMs);
// END TIME

/**
 * \brief Convert milliseconds to FreeRTOS ticks for timeout-aware OSAL operations.
 */
static inline TickType_t template_osalFreertosTimeMsToTicksConvert(const Template_osalTimeMs_t timeMs);

// BEGIN MEMORY
/*-------------------------------- Memory ---------------------------------*/

/**
 * \brief Allocate memory from the FreeRTOS heap.
 */
static Template_osalErr_e template_osalFreertosMemAlloc(void *const osal,
                                                        const size_t size,
                                                        void **const memPtr);

/**
 * \brief Free memory allocated from the FreeRTOS heap.
 */
static Template_osalErr_e template_osalFreertosMemFree(void *const osal,
                                                       void *const memPtr);

// END MEMORY

/*------------------------------- Predicate -------------------------------*/

/**
 * \brief Validate the FreeRTOS OSAL backend.
 */
static bool template_osalFreertosIsValid(const void *const osal);

/*-------------------------- Resource synchronization ---------------------*/

/**
 * \brief Acquire the internal resource mutex.
 */
static inline Template_osalErr_e template_osalFreertosResourceLock(Template_osalFreertos_s *const osalFreertos);

/**
 * \brief Release the internal resource mutex.
 */
static inline Template_osalErr_e template_osalFreertosResourceUnlock(Template_osalFreertos_s *const osalFreertos);

/*------------------------------ Look-up tables ---------------------------*/

// BEGIN THREAD
/**
 * \brief Default FreeRTOS thread priority mapping.
 * \details Used when no instance-specific priority policy is supplied.
 */
static const UBaseType_t template_osalFreertosThreadPriority
[TEMPLATE_OSAL_THREAD_PRIO_MAX_COUNT] =
{
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_LOW,
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_NORMAL,
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_HIGH,
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_CRITICAL
};

// END THREAD

/**
 * \brief FreeRTOS OSAL backend vtable.
 */
static const Template_osalVtable_s template_osalFreertosVtable =
{
// BEGIN QUEUE
    /*-------------------------------- Queues ---------------------------------*/
    .queueCreate   = template_osalFreertosQueueCreate,
    .queueDelete   = template_osalFreertosQueueDelete,
    .queueItemPut  = template_osalFreertosQueueItemPut,
    .queueItemPost = template_osalFreertosQueueItemPost,
    .queueItemGet  = template_osalFreertosQueueItemGet,
    .queueItemWait = template_osalFreertosQueueItemWait,
    .queueItemPend = template_osalFreertosQueueItemPend,
    .queueReset    = template_osalFreertosQueueReset,

// END QUEUE

// BEGIN STREAM_BUFFER
    /*----------------------------- Stream buffers ----------------------------*/
    .streamBufferCreate = template_osalFreertosStreamBufferCreate,
    .streamBufferDelete = template_osalFreertosStreamBufferDelete,
    .streamBufferPut    = template_osalFreertosStreamBufferPut,
    .streamBufferPost   = template_osalFreertosStreamBufferPost,
    .streamBufferGet    = template_osalFreertosStreamBufferGet,
    .streamBufferWait   = template_osalFreertosStreamBufferWait,
    .streamBufferPend   = template_osalFreertosStreamBufferPend,
    .streamBufferReset  = template_osalFreertosStreamBufferReset,
// END STREAM_BUFFER

// BEGIN MUTEX
    /*-------------------------------- Mutexes ----------------------------------*/
    .mutexCreate   = template_osalFreertosMutexCreate,
    .mutexDelete   = template_osalFreertosMutexDelete,
    .mutexLock     = template_osalFreertosMutexLock,
    .mutexTryLock  = template_osalFreertosMutexTryLock,
    .mutexPendLock = template_osalFreertosMutexPendLock,
    .mutexUnlock   = template_osalFreertosMutexUnlock,
// END MUTEX

// BEGIN SEMAPHORE
    /*--------------------------- Counting semaphores --------------------------*/
    .semaphoreCreate   = template_osalFreertosSemaphoreCreate,
    .semaphoreDelete   = template_osalFreertosSemaphoreDelete,
    .semaphoreWait     = template_osalFreertosSemaphoreWait,
    .semaphorePend     = template_osalFreertosSemaphorePend,
    .semaphorePost     = template_osalFreertosSemaphorePost,
    .semaphoreCountGet = template_osalFreertosSemaphoreCountGet,
// END SEMAPHORE

// BEGIN EVENT_FLAGS
    /*-------------------------------- Event flags ------------------------------*/
    .eventFlagsCreate = template_osalFreertosEventFlagsCreate,
    .eventFlagsDelete = template_osalFreertosEventFlagsDelete,
    .eventFlagsSet    = template_osalFreertosEventFlagsSet,
    .eventFlagsClear  = template_osalFreertosEventFlagsClear,
    .eventFlagsGet    = template_osalFreertosEventFlagsGet,
    .eventFlagsWait   = template_osalFreertosEventFlagsWait,

// END EVENT_FLAGS

// BEGIN THREAD
    /*-------------------------------- Threads --------------------------------*/

    .threadCreate     = template_osalFreertosThreadCreate,
    .threadDelete     = template_osalFreertosThreadDelete,
    .threadSuspend    = template_osalFreertosThreadSuspend,
    .threadResume     = template_osalFreertosThreadResume,
    .threadDelay      = template_osalFreertosThreadDelay,
    .threadDelayUntil = template_osalFreertosThreadDelayUntil,
    .threadExit       = template_osalFreertosThreadExit,

// END THREAD

// BEGIN CRITICAL_SECTION
    .criticalSectionEnter = template_osalFreertosCriticalSectionEnter,
    .criticalSectionExit  = template_osalFreertosCriticalSectionExit,

// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
    .softwareTimerCreate = template_osalFreertosSoftwareTimerCreate,
    .softwareTimerDelete = template_osalFreertosSoftwareTimerDelete,
    .softwareTimerStart  = template_osalFreertosSoftwareTimerStart,
    .softwareTimerStop   = template_osalFreertosSoftwareTimerStop,
    .softwareTimerReset  = template_osalFreertosSoftwareTimerReset,

// END SOFTWARE_TIMER

// BEGIN TIME
    /*--------------------------------- Time ----------------------------------*/
    .timeMsGet = template_osalFreertosTimeMsGet,

// END TIME

// BEGIN MEMORY
    /*-------------------------------- Memory ---------------------------------*/

    .memAlloc = template_osalFreertosMemAlloc,
    .memFree  = template_osalFreertosMemFree,

// END MEMORY

    /*------------------------------- Predicate -------------------------------*/

    .isValid = template_osalFreertosIsValid
};

//=======================================================================[ PUBLIC INTERFACE FUNCTIONS ]===============================================================================

/**
 * \brief Initialize the Template FreeRTOS OSAL instance.
 *
 * \details
 * Validates and normalizes optional instance parameters, initializes the generic
 * OSAL base object, creates the internal resource mutex and binds the FreeRTOS
 * backend vtable. Passing NULL as param selects the default port policy.
 *
 * \param osalFreertos  Pointer to the FreeRTOS-specific OSAL instance.
 * \param name          Optional instance name. May be NULL.
 * \param parent        Optional parent object pointer. May be NULL.
 * \param param         Optional FreeRTOS instance parameters. NULL selects the default port policy.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
Template_osalErr_e template_osalFreertosInit(Template_osalFreertos_s *const osalFreertos,
                                             const char *const name,
                                             void *const parent,
                                             const Template_osalFreertosParam_s *const param)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosInit(%p, %s, %p, %p)",
                                 (void *)osalFreertos,
                                 (name != NULL) ? name : "(null)",
                                 parent,
                                 (const void *)param);

    /* Validate args */
    if (osalFreertos == NULL)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid args
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Initialize the default instance policy */
    template_osalFreertosParamDefaultSet(&osalFreertos->param);

    /* Validate and apply explicitly supplied instance parameters */
    if ((param != NULL) &&
        !template_osalFreertosParamApply(&osalFreertos->param, param))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid instance parameters
    }

    /* Initialize the generic OSAL base */
    osalStatus = template_osalInit(&osalFreertos->base, name, parent);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: base initialization failed
    }

    /* Reset the FreeRTOS-specific state */
    osalFreertos->validFlag     = false;
    osalFreertos->resourceMutex = NULL;

    // BEGIN THREAD
    #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
        /* Reset backend-owned MPU task stack buffers. */
        for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
        {
            osalFreertos->threadStackPtr[i] = NULL;
        }
    #endif
    // END THREAD

    /* Create the internal resource mutex */
    osalFreertos->resourceMutex = xSemaphoreCreateMutex();
    if (osalFreertos->resourceMutex == NULL)
    {
        /* Roll back the generic base because backend initialization is atomic. */
        (void)template_osalDeinit(&osalFreertos->base);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex creation failed
    }

    /* Bind the FreeRTOS backend vtable */
    osalFreertos->base.vtable = &template_osalFreertosVtable;

    /* Mark the FreeRTOS backend as valid */
    osalFreertos->validFlag = true;

    /* Trace initialization success */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosInit -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: FreeRTOS OSAL was initialized
}


/**
 * \brief Deinitialize the Template FreeRTOS OSAL instance.
 *
 * \details
 * Releases all registered resources on a best-effort basis, deletes the
 * internal resource mutex and deinitializes the generic OSAL base object.
 *
 * \param osalFreertos  Pointer to the FreeRTOS-specific OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
Template_osalErr_e template_osalFreertosDeinit(Template_osalFreertos_s *const osalFreertos)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosDeinit(%p)", (void *)osalFreertos);

    /* Validate args */
    if (osalFreertos == NULL)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid args
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Validate backend state */
    if (!template_osalFreertosIsValid(osalFreertos))
    {
        osalStatus = TEMPLATE_OSAL_NOT_INIT_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: backend is not initialized
    }

// BEGIN SOFTWARE_TIMER
    /* Delete registered software timers before deleting worker threads. */
    for (size_t i = 0u; i < TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.softwareTimerObj[i].handle != NULL)
        {
            (void)template_osalFreertosSoftwareTimerDelete(osalFreertos,
                                                           osalFreertos->base.softwareTimerObj[i].handle);
        }
    }

// END SOFTWARE_TIMER

// BEGIN THREAD
    /* Delete registered threads */
    for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.threadObjHandle[i].handle != NULL)
        {
            (void)template_osalFreertosThreadDelete(osalFreertos,
                                                    osalFreertos->base.threadObjHandle[i].handle);
        }
    }

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
        /* Release any retained stack from a task that deleted itself. */
        for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
        {
            if (osalFreertos->threadStackPtr[i] != NULL)
            {
                vPortFree(osalFreertos->threadStackPtr[i]);
                osalFreertos->threadStackPtr[i] = NULL;
            }
        }
    #endif

// END THREAD

// BEGIN QUEUE
    /* Delete registered queues */
    for (size_t i = 0u; i < TEMPLATE_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.queueObjHandle[i] != NULL)
        {
            (void)template_osalFreertosQueueDelete(osalFreertos,
                                                   osalFreertos->base.queueObjHandle[i]);
        }
    }

// END QUEUE

// BEGIN STREAM_BUFFER
    /* Delete registered stream buffers */
    for (size_t i = 0u; i < TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.streamBufferObjHandle[i] != NULL)
        {
            (void)template_osalFreertosStreamBufferDelete(osalFreertos,
                                                          osalFreertos->base.streamBufferObjHandle[i]);
        }
    }

// END STREAM_BUFFER

// BEGIN MUTEX
    /* Delete registered mutexes */
    for (size_t i = 0u; i < TEMPLATE_OSAL_MUTEX_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.mutexHandle[i] != NULL)
        {
            (void)template_osalFreertosMutexDelete(osalFreertos,
                                                   osalFreertos->base.mutexHandle[i]);
        }
    }

// END MUTEX

// BEGIN SEMAPHORE
    /* Delete registered counting semaphores */
    for (size_t i = 0u; i < TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.semaphoreObjHandle[i] != NULL)
        {
            (void)template_osalFreertosSemaphoreDelete(osalFreertos,
                                                       osalFreertos->base.semaphoreObjHandle[i]);
        }
    }

// END SEMAPHORE

// BEGIN EVENT_FLAGS
    /* Delete registered event flags objects */
    for (size_t i = 0u; i < TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.eventFlagsObjHandle[i] != NULL)
        {
            (void)template_osalFreertosEventFlagsDelete(osalFreertos,
                                                        osalFreertos->base.eventFlagsObjHandle[i]);
        }
    }

// END EVENT_FLAGS

// BEGIN MEMORY
    /* Free registered memory blocks */
    for (size_t i = 0u; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.memPtr[i] != NULL)
        {
            (void)template_osalFreertosMemFree(osalFreertos,
                                               osalFreertos->base.memPtr[i]);
        }
    }

// END MEMORY

    /* Clear the FreeRTOS-specific state */
    osalFreertos->validFlag   = false;
    osalFreertos->base.vtable = NULL;
    vSemaphoreDelete(osalFreertos->resourceMutex);
    osalFreertos->resourceMutex = NULL;
    osalFreertos->param         = (Template_osalFreertosParam_s) {
        0
    };

    /* Deinitialize the generic OSAL base */
    osalStatus = template_osalDeinit(&osalFreertos->base);

    /* Trace the deinitialization result */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosDeinit -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: FreeRTOS OSAL was deinitialized
}

//============================================================================[ PRIVATE FUNCTIONS ]==================================================================================

/**
 * \brief Initialize FreeRTOS-specific instance parameters with the default port policy.
 *
 * \param param  Destination parameter structure.
 */
static void template_osalFreertosParamDefaultSet(Template_osalFreertosParam_s *const param)
{
    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamDefaultSet(%p)", (void *)param);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(param != NULL);

    /* Reset common integration context */
    param->handle = NULL;

    // BEGIN THREAD
    /* Apply the default priority mapping */
    param->prio.hasParam = false;

    for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_PRIO_MAX_COUNT; ++i)
    {
        param->prio.policy[i] = template_osalFreertosThreadPriority[i];
    }

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
        /* Disable the custom MPU policy and clear all configurable regions. */
        param->mpu.hasParam = false;

        for (size_t i = 0u; i < portNUM_CONFIGURABLE_REGIONS; ++i)
        {
            param->mpu.region[i] = (MemoryRegion_t) {
                0
            };
        }
    #endif

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP
        /* Preserve the FreeRTOS default core-affinity policy when no override is supplied. */
        param->smp.hasParam = false;

        for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
        {
            #ifdef configTASK_DEFAULT_CORE_AFFINITY
                param->smp.coreAffinityMask[i] = (UBaseType_t)configTASK_DEFAULT_CORE_AFFINITY;
            #else
                param->smp.coreAffinityMask[i] = tskNO_AFFINITY;
            #endif
        }
    #endif /* ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP */
    // END THREAD

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamDefaultSet -> ok");
}


/**
 * \brief Validate explicitly supplied FreeRTOS-specific instance parameters.
 *
 * \param param  Parameter structure to validate.
 *
 * \return true if all explicitly supplied parameter groups are valid; false otherwise.
 */
static bool template_osalFreertosParamValidate(const Template_osalFreertosParam_s *const param)
{
    bool isValid = true;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamValidate(%p)", (const void *)param);

    /* Validate input args */
    if (param == NULL)
    {
        isValid = false;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamValidate -> %d", (int)isValid);

        return isValid;  // Exit: Error: invalid args
    }

    // BEGIN THREAD
    /* Validate an explicitly supplied priority mapping */
    if (param->prio.hasParam)
    {
        const UBaseType_t low      = param->prio.policy[TEMPLATE_OSAL_THREAD_PRIO_LOW];
        const UBaseType_t normal   = param->prio.policy[TEMPLATE_OSAL_THREAD_PRIO_NORMAL];
        const UBaseType_t high     = param->prio.policy[TEMPLATE_OSAL_THREAD_PRIO_HIGH];
        const UBaseType_t critical = param->prio.policy[TEMPLATE_OSAL_THREAD_PRIO_CRITICAL];

        if ((low <= (UBaseType_t)tskIDLE_PRIORITY) ||
            (low >= (UBaseType_t)configMAX_PRIORITIES) ||
            (normal >= (UBaseType_t)configMAX_PRIORITIES) ||
            (high >= (UBaseType_t)configMAX_PRIORITIES) ||
            (critical >= (UBaseType_t)configMAX_PRIORITIES) ||
            (low >= normal) ||
            (normal >= high) ||
            (high >= critical))
        {
            isValid = false;
        }
    }

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
        /* Validate explicitly supplied memory regions */
        if (isValid &&
            param->mpu.hasParam)
        {
            for (size_t i = 0u; i < portNUM_CONFIGURABLE_REGIONS; ++i)
            {
                if (!template_osalFreertosMpuRegionValidate(&param->mpu.region[i]))
                {
                    isValid = false;
                    break;
                }
            }

            /* Reject overlapping memory regions. */
            for (size_t i = 0u; isValid &&
                 (i < portNUM_CONFIGURABLE_REGIONS); ++i)
            {
                const uintptr_t startA = (uintptr_t)param->mpu.region[i].pvBaseAddress;
                const uintptr_t endA   = startA + (uintptr_t)param->mpu.region[i].ulLengthInBytes;

                for (size_t j = i + 1u; j < portNUM_CONFIGURABLE_REGIONS; ++j)
                {
                    const uintptr_t startB = (uintptr_t)param->mpu.region[j].pvBaseAddress;
                    const uintptr_t endB   = startB + (uintptr_t)param->mpu.region[j].ulLengthInBytes;

                    if ((startA < endB) &&
                        (startB < endA))
                    {
                        isValid = false;
                        break;
                    }
                }
            }
        }
    #endif /* ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU */

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP
        /* Validate an explicitly supplied thread-slot affinity policy */
        if (isValid &&
            param->smp.hasParam)
        {
            const size_t affinityBits = sizeof(UBaseType_t) * 8u;
            UBaseType_t validCoreMask = 0u;

            for (size_t core = 0u; core < (size_t)configNUMBER_OF_CORES; ++core)
            {
                if (core >= affinityBits)
                {
                    validCoreMask = (UBaseType_t) ~(UBaseType_t)0u;
                    break;
                }

                validCoreMask |= (UBaseType_t)((UBaseType_t)1u << core);
            }

            for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
            {
                const UBaseType_t affinityMask = param->smp.coreAffinityMask[i];
                if ((affinityMask != tskNO_AFFINITY) &&
                    ((affinityMask == 0u) ||
                     ((affinityMask & ~validCoreMask) != 0u)))
                {
                    isValid = false;
                    break;
                }
            }
        }
    #endif /* ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP */
    // END THREAD

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamValidate -> %d", (int)isValid);

    return isValid;  // Exit: Success: validation result returned
}


/**
 * \brief Validate and apply FreeRTOS-specific instance parameters over the default port policy.
 *
 * \param dst  Destination parameter structure initialized with the default port policy.
 * \param src  Parameter structure supplied by the caller.
 *
 * \return true if the parameters are valid and were applied; false otherwise.
 */
static bool template_osalFreertosParamApply(Template_osalFreertosParam_s *const dst,
                                            const Template_osalFreertosParam_s *const src)
{
    bool isApplied = false;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamApply(%p, %p)",
                                 (void *)dst,
                                 (const void *)src);

    /* Validate input args */
    if ((dst == NULL) ||
        (src == NULL))
    {
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamApply -> %d", (int)isApplied);

        return isApplied;  // Exit: Error: invalid args
    }

    /* Validate explicitly supplied instance parameters */
    if (!template_osalFreertosParamValidate(src))
    {
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamApply -> %d", (int)isApplied);

        return isApplied;  // Exit: Error: invalid instance parameters
    }

    /* Apply common integration context */
    dst->handle = src->handle;

    // BEGIN THREAD
    /* Apply an optional instance-specific priority policy */
    if (src->prio.hasParam)
    {
        dst->prio = src->prio;
    }

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
        /* Apply an optional instance-specific MPU policy */
        if (src->mpu.hasParam)
        {
            dst->mpu = src->mpu;
        }
    #endif

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP
        /* Apply an optional instance-specific core-affinity policy */
        if (src->smp.hasParam)
        {
            dst->smp = src->smp;
        }
    #endif
    // END THREAD

    isApplied = true;

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosParamApply -> %d", (int)isApplied);

    return isApplied;  // Exit: Success: instance parameters were applied
}


// BEGIN THREAD
#ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
/**
 * \brief Validate one FreeRTOS MPU memory region.
 *
 * \details Performs common range validation and, for known FreeRTOS MPU ports,
 *          validates architecture-specific alignment and region-size rules.
 *
 * \param region  Memory region to validate.
 *
 * \return true if the memory region is valid; false otherwise.
 */
    static bool template_osalFreertosMpuRegionValidate(const MemoryRegion_t *const region)
    {
        bool isValid = true;

        /* Trace input args */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMpuRegionValidate(%p)",
                                     (const void *)region);

        /* Validate input args */
        if (region == NULL)
        {
            isValid = false;
            TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMpuRegionValidate -> %d", (int)isValid);

            return isValid; // Exit: Error: invalid args
        }

        const uintptr_t regionStart = (uintptr_t)region->pvBaseAddress;
        const uintptr_t regionSize  = (uintptr_t)region->ulLengthInBytes;

        /* Validate the generic region range */
        if ((regionSize == 0u) ||
            (regionSize > (UINTPTR_MAX - regionStart)))
        {
            isValid = false;
        }

        #if defined(TEMPLATE_OSAL_FREERTOS_MPU_MODEL_ARMV8M)
            /* ARMv8-M RBAR/RLAR addresses use 32-byte granularity. */
            if (isValid &&
                ((regionSize < TEMPLATE_OSAL_FREERTOS_MPU_MIN_SIZE) ||
                 ((regionStart % TEMPLATE_OSAL_FREERTOS_MPU_ALIGNMENT) != 0u) ||
                 ((regionSize % TEMPLATE_OSAL_FREERTOS_MPU_ALIGNMENT) != 0u)))
            {
                isValid = false;
            }
        #elif defined(TEMPLATE_OSAL_FREERTOS_MPU_MODEL_ARMV6M) || \
        defined(TEMPLATE_OSAL_FREERTOS_MPU_MODEL_CLASSIC_RASR)
            /* Classic Arm MPU regions are power-of-two sized and aligned to their size. */
            if (isValid &&
                ((regionSize < TEMPLATE_OSAL_FREERTOS_MPU_MIN_SIZE) ||
                 ((regionSize & (regionSize - 1u)) != 0u) ||
                 ((regionStart & (regionSize - 1u)) != 0u)))
            {
                isValid = false;
            }
        #elif defined(TEMPLATE_OSAL_FREERTOS_MPU_REGION_PLATFORM_VALIDATE)
            /* Use the application-provided validator for an otherwise unknown MPU model. */
            if (isValid &&
                !TEMPLATE_OSAL_FREERTOS_MPU_REGION_PLATFORM_VALIDATE(region))
            {
                isValid = false;
            }
        #endif /* if defined(TEMPLATE_OSAL_FREERTOS_MPU_MODEL_ARMV8M) */

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMpuRegionValidate -> %d", (int)isValid);

        return isValid; // Exit: Success: validation result returned
    }
#endif /* ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU */
// END THREAD


// BEGIN QUEUE
/*-------------------------------- Queues ---------------------------------*/

/**
 * \brief Create a FreeRTOS queue.
 *
 * \details Reserves a free registry slot through \c ptable, creates a queue
 *          and registers its handle under the resource mutex.
 *
 * \param osal           Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueItemSize  Size of one queue item in bytes.
 * \param queueDepth     Maximum number of queue items.
 * \param queueHandle    Output pointer receiving the queue handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueCreate(void *const osal,
                                                           const size_t queueItemSize,
                                                           const size_t queueDepth,
                                                           Template_osalQueueHandle_t *const queueHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate(%p, %lu, %lu, %p)",
                                 osal,
                                 (unsigned long)queueItemSize,
                                 (unsigned long)queueDepth,
                                 (void *)queueHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueItemSize != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueDepth != 0u);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Clear the output value */
    *queueHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free queue registry slot */
    const size_t queueId = port->base.ptable->queueFreeSlotFind(port);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free queue slot
    }

    /* Create the native FreeRTOS queue */
    const QueueHandle_t nativeQueue = xQueueCreate((UBaseType_t)queueDepth,
                                                   (UBaseType_t)queueItemSize);
    if (nativeQueue == NULL)
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue allocation failed
    }

    /* Register the resource handle */
    port->base.queueObjHandle[queueId - 1u] = (Template_osalQueueHandle_t)nativeQueue;
    *queueHandle                            = (Template_osalQueueHandle_t)nativeQueue;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue was created and registered
}


/**
 * \brief Delete a FreeRTOS queue.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle  Queue handle to delete.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueDelete(void *const osal,
                                                           const Template_osalQueueHandle_t queueHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueDelete(%p, %p)", osal, (void *)queueHandle);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the queue handle in the registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Delete the native FreeRTOS queue */
    vQueueDelete((QueueHandle_t)queueHandle);

    /* Clear the registry slot */
    port->base.queueObjHandle[queueId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue was deleted and unregistered
}


/**
 * \brief Put an item into a registered FreeRTOS queue without waiting for capacity.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Pointer to the item to enqueue.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemPut(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            const void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t putStatus          = pdFALSE;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut(%p, %p, %p)",
                                 osal, (void *)queueHandle, queueItemPtr);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Find the queue handle in the registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Put the item without waiting for queue capacity */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        putStatus = xQueueSendFromISR((QueueHandle_t)queueHandle,
                                      queueItemPtr,
                                      &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        putStatus = xQueueSend((QueueHandle_t)queueHandle, queueItemPtr, 0u);
    }

    if (putStatus != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_QUEUE_IS_FULL_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue is full
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was put
}


/**
 * \brief Post an item to a registered FreeRTOS queue with a finite or infinite timeout.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Pointer to the item to enqueue.
 * \param timeoutMs     Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemPost(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             const void *const queueItemPtr,
                                                             const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPost(%p, %p, %p, %u)",
                                 osal,
                                 (void *)queueHandle,
                                 queueItemPtr,
                                 (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find and validate the resource handle in the component registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    if (xQueueSend((QueueHandle_t)queueHandle,
                   queueItemPtr,
                   template_osalFreertosTimeMsToTicksConvert(timeoutMs)) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_QUEUE_OVERFLOW_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue post failed or timed out
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPost -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was posted
}


/**
 * \brief Retrieve an already available item from a registered FreeRTOS queue without waiting.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Destination buffer receiving the queue item.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemGet(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t receiveStatus      = pdFALSE;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemGet(%p, %p, %p)",
                                 osal, (void *)queueHandle, queueItemPtr);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Find and validate the resource handle in the component registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        receiveStatus = xQueueReceiveFromISR((QueueHandle_t)queueHandle,
                                             queueItemPtr,
                                             &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        receiveStatus = xQueueReceive((QueueHandle_t)queueHandle, queueItemPtr, 0u);
    }

    if (receiveStatus != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue is empty
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was retrieved
}


/**
 * \brief Wait indefinitely for an item from a registered FreeRTOS queue.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Destination buffer receiving the queue item.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemWait(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemWait(%p, %p, %p)",
                                 osal, (void *)queueHandle, queueItemPtr);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find and validate the resource handle in the component registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    if (xQueueReceive((QueueHandle_t)queueHandle, queueItemPtr, portMAX_DELAY) != pdTRUE)
    {
        /* With an infinite wait this normally indicates an invalid FreeRTOS configuration/state. */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: infinite queue wait failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was retrieved
}


/**
 * \brief Pend an item from a FreeRTOS queue.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle   Queue handle to read from.
 * \param queueItemPtr  Destination buffer receiving the queue item.
 * \param timeoutMs     Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemPend(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             void *const queueItemPtr,
                                                             const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPend(%p, %p, %p, %u)",
                                 osal, (void *)queueHandle, queueItemPtr, (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the queue handle in the registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);

    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Receive an item from the queue */
    if (xQueueReceive((QueueHandle_t)queueHandle,
                      queueItemPtr,
                      template_osalFreertosTimeMsToTicksConvert(timeoutMs)) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue receive failed or timed out
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was retrieved
}


/**
 * \brief Reset a FreeRTOS queue.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle  Queue handle to reset.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueReset(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueReset(%p, %p)", osal, (void *)queueHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the queue handle in the registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);

    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Reset the native FreeRTOS queue */
    if (xQueueReset((QueueHandle_t)queueHandle) != pdPASS)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native queue reset failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueReset -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue was reset
}


// END QUEUE

// BEGIN STREAM_BUFFER
/*----------------------------- Stream buffers -----------------------------*/

/**
 * \brief Create a FreeRTOS stream buffer and register it in the OSAL instance.
 *
 * \details The operation follows the component-scoped OSAL ownership model and
 *          registers the native stream-buffer handle in the OSAL registry.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param bufferSizeBytes     Stream-buffer capacity in bytes.
 * \param triggerLevelBytes   Receive trigger level in bytes.
 * \param streamBufferHandle  Output pointer receiving the created handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferCreate(void *const osal,
                                                                  const size_t bufferSizeBytes,
                                                                  const size_t triggerLevelBytes,
                                                                  Template_osalStreamBufferHandle_t *const streamBufferHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate(%p, %lu, %lu, %p)",
                                 osal,
                                 (unsigned long)bufferSizeBytes,
                                 (unsigned long)triggerLevelBytes,
                                 (void *)streamBufferHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bufferSizeBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(triggerLevelBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(triggerLevelBytes <= bufferSizeBytes);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Clear the output value */
    *streamBufferHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free stream-buffer registry slot */
    const size_t streamBufferId = port->base.ptable->streamBufferFreeSlotFind(port);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free stream-buffer registry slot
    }

    /* Create the native FreeRTOS stream buffer */
    const StreamBufferHandle_t nativeHandle = xStreamBufferCreate(bufferSizeBytes, triggerLevelBytes);
    if (nativeHandle == NULL)
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer allocation failed
    }

    /* Register the resource handle */
    port->base.streamBufferObjHandle[streamBufferId - 1u] = (Template_osalStreamBufferHandle_t)nativeHandle;
    *streamBufferHandle                                   = (Template_osalStreamBufferHandle_t)nativeHandle;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream buffer was created and registered
}


/**
 * \brief Delete a registered FreeRTOS stream buffer.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferDelete(void *const osal,
                                                                  const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferDelete(%p, %p)",
                                 osal, (void *)streamBufferHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the stream-buffer handle in the registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Delete the native FreeRTOS stream buffer */
    vStreamBufferDelete((StreamBufferHandle_t)streamBufferHandle);

    /* Clear the registry slot */
    port->base.streamBufferObjHandle[streamBufferId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream buffer was deleted and unregistered
}


/**
 * \brief Put bytes into a registered FreeRTOS stream buffer without waiting for free capacity.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Pointer to source bytes.
 * \param dataLengthBytes     Number of bytes requested for transfer.
 * \param bytesPut            Output pointer receiving the number of bytes written.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferPut(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                               const void *const data,
                                                               const size_t dataLengthBytes,
                                                               size_t *const bytesPut)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPut(%p, %p, %p, %lu, %p)",
                                 osal,
                                 (void *)streamBufferHandle,
                                 data,
                                 (unsigned long)dataLengthBytes,
                                 (void *)bytesPut);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(data != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bytesPut != NULL);

    /* Clear the output value */
    *bytesPut = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Find the stream-buffer handle in the registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Put available data without waiting for free capacity */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        *bytesPut = xStreamBufferSendFromISR((StreamBufferHandle_t)streamBufferHandle,
                                             data,
                                             dataLengthBytes,
                                             &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        *bytesPut = xStreamBufferSend((StreamBufferHandle_t)streamBufferHandle,
                                      data,
                                      dataLengthBytes,
                                      0u);
    }

    if (*bytesPut == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_FULL_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer capacity was available
    }

    /* Trace output value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPut: bytesPut = %lu",
                                 (unsigned long)*bytesPut);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPut -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was put
}


/**
 * \brief Put bytes into a registered FreeRTOS stream buffer using the requested timeout.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Pointer to source bytes.
 * \param dataLengthBytes     Number of bytes requested for transfer.
 * \param timeoutMs           Maximum wait time in milliseconds.
 * \param bytesPut            Output pointer receiving the number of bytes written.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferPost(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                const void *const data,
                                                                const size_t dataLengthBytes,
                                                                const Template_osalTimeMs_t timeoutMs,
                                                                size_t *const bytesPut)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPost(%p, %p, %p, %lu, %u, %p)",
                                 osal,
                                 (void *)streamBufferHandle,
                                 data,
                                 (unsigned long)dataLengthBytes,
                                 (unsigned int)timeoutMs,
                                 (void *)bytesPut);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(data != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bytesPut != NULL);

    /* Clear the output value */
    *bytesPut = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the stream-buffer handle in the registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Put data using the requested timeout */
    *bytesPut = xStreamBufferSend((StreamBufferHandle_t)streamBufferHandle,
                                  data,
                                  dataLengthBytes,
                                  template_osalFreertosTimeMsToTicksConvert(timeoutMs));
    if (*bytesPut == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_FULL_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer data was put before timeout
    }

    /* Trace output value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPost: bytesPut = %lu",
                                 (unsigned long)*bytesPut);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPost -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was put
}


/**
 * \brief Get already available bytes from a registered FreeRTOS stream buffer without waiting.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Destination byte buffer.
 * \param dataLengthBytes     Maximum number of bytes to transfer.
 * \param bytesGet            Output pointer receiving the number of bytes read.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferGet(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                               void *const data,
                                                               const size_t dataLengthBytes,
                                                               size_t *const bytesGet)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferGet(%p, %p, %p, %lu, %p)",
                                 osal,
                                 (void *)streamBufferHandle,
                                 data,
                                 (unsigned long)dataLengthBytes,
                                 (void *)bytesGet);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(data != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bytesGet != NULL);

    /* Clear the output value */
    *bytesGet = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Find the stream-buffer handle in the registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Get available data without waiting */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        *bytesGet = xStreamBufferReceiveFromISR((StreamBufferHandle_t)streamBufferHandle,
                                                data,
                                                dataLengthBytes,
                                                &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        *bytesGet = xStreamBufferReceive((StreamBufferHandle_t)streamBufferHandle,
                                         data,
                                         dataLengthBytes,
                                         0u);
    }

    if (*bytesGet == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer data was available
    }

    /* Trace output value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferGet: bytesGet = %lu",
                                 (unsigned long)*bytesGet);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was retrieved
}


/**
 * \brief Wait indefinitely for bytes and get them from a registered FreeRTOS stream buffer.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Destination byte buffer.
 * \param dataLengthBytes     Maximum number of bytes to transfer.
 * \param bytesGet            Output pointer receiving the number of bytes read.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferWait(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                void *const data,
                                                                const size_t dataLengthBytes,
                                                                size_t *const bytesGet)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferWait(%p, %p, %p, %lu, %p)",
                                 osal,
                                 (void *)streamBufferHandle,
                                 data,
                                 (unsigned long)dataLengthBytes,
                                 (void *)bytesGet);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(data != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bytesGet != NULL);

    /* Clear the output value */
    *bytesGet = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the stream-buffer handle in the registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Wait indefinitely for data */
    *bytesGet = xStreamBufferReceive((StreamBufferHandle_t)streamBufferHandle,
                                     data,
                                     dataLengthBytes,
                                     portMAX_DELAY);
    if (*bytesGet == 0u)
    {
        /* An infinite wait failure indicates an invalid FreeRTOS state/configuration. */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer wait failed
    }

    /* Trace output value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferWait: bytesGet = %lu",
                                 (unsigned long)*bytesGet);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was retrieved
}


/**
 * \brief Get bytes from a registered FreeRTOS stream buffer using the requested timeout.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Destination byte buffer.
 * \param dataLengthBytes     Maximum number of bytes to transfer.
 * \param timeoutMs           Maximum wait time in milliseconds.
 * \param bytesGet            Output pointer receiving the number of bytes read.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferPend(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                void *const data,
                                                                const size_t dataLengthBytes,
                                                                const Template_osalTimeMs_t timeoutMs,
                                                                size_t *const bytesGet)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPend(%p, %p, %p, %lu, %u, %p)",
                                 osal,
                                 (void *)streamBufferHandle,
                                 data,
                                 (unsigned long)dataLengthBytes,
                                 (unsigned int)timeoutMs,
                                 (void *)bytesGet);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(data != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bytesGet != NULL);

    /* Clear the output value */
    *bytesGet = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the stream-buffer handle in the registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Wait up to the requested timeout for data */
    *bytesGet = xStreamBufferReceive((StreamBufferHandle_t)streamBufferHandle,
                                     data,
                                     dataLengthBytes,
                                     template_osalFreertosTimeMsToTicksConvert(timeoutMs));
    if (*bytesGet == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer data was retrieved before timeout
    }

    /* Trace output value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPend: bytesGet = %lu",
                                 (unsigned long)*bytesGet);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferPend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was retrieved
}


/**
 * \brief Reset a registered FreeRTOS stream buffer to the empty state.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferReset(void *const osal,
                                                                 const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReset(%p, %p)",
                                 osal, (void *)streamBufferHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the stream-buffer handle in the registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Reset the native FreeRTOS stream buffer */
    if (xStreamBufferReset((StreamBufferHandle_t)streamBufferHandle) != pdPASS)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_RESET_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native stream-buffer reset failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReset -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream buffer was reset
}


// END STREAM_BUFFER

// BEGIN MUTEX
/*-------------------------------- Mutexes ----------------------------------*/

/**
 * \brief Create a recursive FreeRTOS mutex.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param mutexHandle  Output pointer receiving the mutex handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMutexCreate(void *const osal,
                                                           Template_osalMutexHandle_t *const mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexCreate(%p, %p)",
                                 osal, (void *)mutexHandle);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(mutexHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->mutexFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Clear the output value */
    *mutexHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free mutex registry slot */
    const size_t mutexId = port->base.ptable->mutexFreeSlotFind(port);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free mutex slot
    }

    /* Create the native recursive mutex */
    const SemaphoreHandle_t nativeMutex = xSemaphoreCreateRecursiveMutex();
    if (nativeMutex == NULL)
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex allocation failed
    }

    /* Register the resource handle */
    port->base.mutexHandle[mutexId - 1u] = (Template_osalMutexHandle_t)nativeMutex;
    *mutexHandle                         = (Template_osalMutexHandle_t)nativeMutex;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was created and registered
}


/**
 * \brief Delete a recursive FreeRTOS mutex.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param mutexHandle  Mutex handle to delete.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMutexDelete(void *const osal,
                                                           const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexDelete(%p, %p)",
                                 osal, (void *)mutexHandle);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(mutexHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the mutex handle in the registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Delete the native mutex */
    vSemaphoreDelete((SemaphoreHandle_t)mutexHandle);

    /* Clear the registry slot */
    port->base.mutexHandle[mutexId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was deleted and unregistered
}


/**
 * \brief Lock a recursive FreeRTOS mutex and wait indefinitely.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param mutexHandle  Mutex handle to lock.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMutexLock(void *const osal,
                                                         const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexLock(%p, %p)", osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(mutexHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the mutex handle in the registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);

    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Acquire the native recursive mutex */
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)mutexHandle, portMAX_DELAY) != pdTRUE)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native recursive mutex operation failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was locked
}


/**
 * \brief Try to lock a registered recursive FreeRTOS mutex without waiting.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param mutexHandle  Registered mutex handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMutexTryLock(void *const osal,
                                                            const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexTryLock(%p, %p)",
                                 osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(mutexHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexTryLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the mutex handle in the registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexTryLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Try to lock the native recursive mutex without waiting */
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)mutexHandle, 0u) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_MUTEX_LOCK_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexTryLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex is not immediately available
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexTryLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was locked
}


/**
 * \brief Lock a registered recursive FreeRTOS mutex using the requested timeout.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param mutexHandle  Registered mutex handle.
 * \param timeoutMs    Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMutexPendLock(void *const osal,
                                                             const Template_osalMutexHandle_t mutexHandle,
                                                             const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexPendLock(%p, %p, %u)",
                                 osal, (void *)mutexHandle, (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(mutexHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexPendLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the mutex handle in the registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexPendLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Lock the native recursive mutex using the requested timeout */
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)mutexHandle,
                                template_osalFreertosTimeMsToTicksConvert(timeoutMs)) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_MUTEX_LOCK_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexPendLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex lock failed or timed out
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexPendLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was locked
}


/**
 * \brief Unlock a recursive FreeRTOS mutex.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param mutexHandle  Mutex handle to unlock.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMutexUnlock(void *const osal,
                                                           const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexUnlock(%p, %p)", osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(mutexHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the mutex handle in the registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);

    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Release the native recursive mutex */
    if (xSemaphoreGiveRecursive((SemaphoreHandle_t)mutexHandle) != pdTRUE)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native recursive mutex operation failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMutexUnlock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was unlocked
}


// END MUTEX

// BEGIN SEMAPHORE
/*--------------------------- Counting semaphores --------------------------*/

/**
 * \brief Create a FreeRTOS counting semaphore and register it in the OSAL instance.
 *
 * \details The operation follows the component-scoped OSAL ownership model and
 *          validates/registers the native object through the OSAL registry.
 *
 * \param osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param maxCount         Maximum semaphore count.
 * \param initialCount     Initial semaphore count.
 * \param semaphoreHandle  Output pointer receiving the created handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreCreate(void *const osal,
                                                               const Template_osalSemaphoreCount_t maxCount,
                                                               const Template_osalSemaphoreCount_t initialCount,
                                                               Template_osalSemaphoreHandle_t *const semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCreate(%p, %u, %u, %p)",
                                 osal,
                                 (unsigned int)maxCount,
                                 (unsigned int)initialCount,
                                 (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(semaphoreHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(maxCount != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(initialCount <= maxCount);

    *semaphoreHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->semaphoreFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    const size_t semaphoreId = port->base.ptable->semaphoreFreeSlotFind(port);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free semaphore registry slot
    }

    const SemaphoreHandle_t nativeHandle =
        xSemaphoreCreateCounting((UBaseType_t)maxCount, (UBaseType_t)initialCount);
    if (nativeHandle == NULL)
    {
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore allocation failed
    }

    port->base.semaphoreObjHandle[semaphoreId - 1u] =
        (Template_osalSemaphoreHandle_t)nativeHandle;
    *semaphoreHandle = (Template_osalSemaphoreHandle_t)nativeHandle;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore was created and registered
}


/**
 * \brief Delete a registered FreeRTOS counting semaphore.
 *
 * \details The handle must belong to this OSAL instance. Registry bookkeeping
 *          is updated together with the native FreeRTOS resource lifecycle.
 *
 * \param osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param semaphoreHandle  Registered counting semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreDelete(void *const osal,
                                                               const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreDelete(%p, %p)",
                                 osal, (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(semaphoreHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find and validate the resource handle in the component registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        (void)template_osalFreertosResourceUnlock(port);
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    vSemaphoreDelete((SemaphoreHandle_t)semaphoreHandle);
    port->base.semaphoreObjHandle[semaphoreId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore was deleted and unregistered
}


/**
 * \brief Wait indefinitely for one count from a registered FreeRTOS counting semaphore.
 *
 * \param osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param semaphoreHandle  Registered counting semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreWait(void *const osal,
                                                             const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreWait(%p, %p)",
                                 osal, (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(semaphoreHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the semaphore handle in the registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore handle is not registered
    }

    /* Wait indefinitely for one semaphore count */
    if (xSemaphoreTake((SemaphoreHandle_t)semaphoreHandle, portMAX_DELAY) != pdTRUE)
    {
        /* An infinite wait failure indicates an invalid FreeRTOS state/configuration. */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_WAIT_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore wait failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore wait completed
}


/**
 * \brief Pend for one count from a registered counting semaphore using the requested timeout.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param semaphoreHandle  Registered counting semaphore handle.
 * \param timeoutMs        Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphorePend(void *const osal,
                                                             const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                             const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePend(%p, %p, %u)",
                                 osal, (void *)semaphoreHandle, (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(semaphoreHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find and validate the resource handle in the component registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    if (xSemaphoreTake((SemaphoreHandle_t)semaphoreHandle,
                       template_osalFreertosTimeMsToTicksConvert(timeoutMs)) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_WAIT_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore pend failed or timed out
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore pend completed
}


/**
 * \brief Post one count to a registered FreeRTOS counting semaphore.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param semaphoreHandle  Registered counting semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphorePost(void *const osal,
                                                             const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t postStatus         = pdFALSE;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePost(%p, %p)",
                                 osal, (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(semaphoreHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Find and validate the resource handle in the component registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        postStatus = xSemaphoreGiveFromISR((SemaphoreHandle_t)semaphoreHandle,
                                           &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        postStatus = xSemaphoreGive((SemaphoreHandle_t)semaphoreHandle);
    }

    if (postStatus != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_POST_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore post failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphorePost -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore count was posted
}


/**
 * \brief Read the current count of a registered FreeRTOS counting semaphore.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param semaphoreHandle  Registered counting semaphore handle.
 * \param semaphoreCount   Output pointer receiving the current count.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreCountGet(void *const osal,
                                                                 const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                                 Template_osalSemaphoreCount_t *const semaphoreCount)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCountGet(%p, %p, %p)",
                                 osal, (void *)semaphoreHandle, (void *)semaphoreCount);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(semaphoreHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(semaphoreCount != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCountGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find and validate the resource handle in the component registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCountGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    *semaphoreCount =
        (Template_osalSemaphoreCount_t)uxSemaphoreGetCount((SemaphoreHandle_t)semaphoreHandle);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreCountGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore count was read
}


// END SEMAPHORE

// BEGIN EVENT_FLAGS
/*-------------------------------- Event flags ------------------------------*/

/**
 * \brief Create a FreeRTOS event group and register it in the component OSAL instance.
 *
 * \details The operation follows the component-scoped OSAL ownership model and
 *          registers the native event-group handle in the OSAL registry.
 *
 * \param osal              Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param eventFlagsHandle  Output pointer receiving the event flags handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosEventFlagsCreate(void *const osal,
                                                                Template_osalEventFlagsHandle_t *const eventFlagsHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsCreate(%p, %p)",
                                 osal, (void *)eventFlagsHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(eventFlagsHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->eventFlagsFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Clear the output value */
    *eventFlagsHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free event-flags registry slot */
    const size_t eventFlagsId = port->base.ptable->eventFlagsFreeSlotFind(port);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free event-flags registry slot
    }

    /* Create the native FreeRTOS event group */
    const EventGroupHandle_t nativeHandle = xEventGroupCreate();
    if (nativeHandle == NULL)
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native event-group allocation failed
    }

    /* Register the resource handle */
    port->base.eventFlagsObjHandle[eventFlagsId - 1u] = (Template_osalEventFlagsHandle_t)nativeHandle;
    *eventFlagsHandle                                 = (Template_osalEventFlagsHandle_t)nativeHandle;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags object was created and registered
}


/**
 * \brief Delete a registered FreeRTOS event group.
 *
 * \param osal              Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param eventFlagsHandle  Registered event flags handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosEventFlagsDelete(void *const osal,
                                                                const Template_osalEventFlagsHandle_t eventFlagsHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsDelete(%p, %p)",
                                 osal, (void *)eventFlagsHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(eventFlagsHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the event-flags handle in the registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event flags handle is not registered
    }

    /* Delete the native FreeRTOS event group */
    vEventGroupDelete((EventGroupHandle_t)eventFlagsHandle);

    /* Clear the registry slot */
    port->base.eventFlagsObjHandle[eventFlagsId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags object was deleted and unregistered
}


/**
 * \brief Set one or more bits in a registered FreeRTOS event group.
 *
 * \param osal              Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param eventFlagsHandle  Registered event flags handle.
 * \param flags             Non-zero bit mask to set.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosEventFlagsSet(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                             const uint32_t flags)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsSet(%p, %p, 0x%08lX)",
                                 osal, (void *)eventFlagsHandle, (unsigned long)flags);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(flags != 0u);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsSet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the event-flags handle in the registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsSet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event flags handle is not registered
    }

    /*
     * Set the requested bits.
     * The return value is intentionally not used for success validation because
     * an unblocked task may clear bits before xEventGroupSetBits() returns.
     */
    (void)xEventGroupSetBits((EventGroupHandle_t)eventFlagsHandle, (EventBits_t)flags);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsSet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags were set
}


/**
 * \brief Clear one or more bits in a registered FreeRTOS event group.
 *
 * \param osal              Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param eventFlagsHandle  Registered event flags handle.
 * \param flags             Non-zero bit mask to clear.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosEventFlagsClear(void *const osal,
                                                               const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                               const uint32_t flags)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsClear(%p, %p, 0x%08lX)",
                                 osal, (void *)eventFlagsHandle, (unsigned long)flags);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(flags != 0u);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsClear -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the event-flags handle in the registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsClear -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event flags handle is not registered
    }

    /* Clear the requested bits */
    (void)xEventGroupClearBits((EventGroupHandle_t)eventFlagsHandle, (EventBits_t)flags);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsClear -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags were cleared
}


/**
 * \brief Read currently set bits from a registered FreeRTOS event group.
 *
 * \param osal              Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param eventFlagsHandle  Registered event flags handle.
 * \param flags             Output pointer receiving the current bit mask.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosEventFlagsGet(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                             uint32_t *const flags)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsGet(%p, %p, %p)",
                                 osal, (void *)eventFlagsHandle, (void *)flags);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(flags != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the event-flags handle in the registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event flags handle is not registered
    }

    /* Read the current native event-group bits */
    *flags = (uint32_t)xEventGroupGetBits((EventGroupHandle_t)eventFlagsHandle);

    /* Trace output value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsGet: flags = 0x%08lX",
                                 (unsigned long)*flags);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags were read
}


/**
 * \brief Wait for any or all requested bits in a registered FreeRTOS event group.
 *
 * \param osal              Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param eventFlagsHandle  Registered event flags handle.
 * \param flags             Non-zero bit mask to wait for.
 * \param options           WAIT_ANY/WAIT_ALL and optional NO_CLEAR behavior.
 * \param timeoutMs         Maximum wait time in milliseconds.
 * \param actualFlags       Output pointer receiving the observed flags snapshot.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosEventFlagsWait(void *const osal,
                                                              const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                              const uint32_t flags,
                                                              const Template_osalEventFlagsOptions_e options,
                                                              const Template_osalTimeMs_t timeoutMs,
                                                              uint32_t *const actualFlags)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    const uint32_t validOptions   = (uint32_t)TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ALL |
                                    (uint32_t)TEMPLATE_OSAL_EVENT_FLAGS_NO_CLEAR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsWait(%p, %p, 0x%08lX, 0x%08lX, %u, %p)",
                                 osal,
                                 (void *)eventFlagsHandle,
                                 (unsigned long)flags,
                                 (unsigned long)options,
                                 (unsigned int)timeoutMs,
                                 (void *)actualFlags);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(flags != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(actualFlags != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(((uint32_t)options & ~validOptions) == 0u);

    /* Clear the output value */
    *actualFlags = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the event-flags handle in the registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event flags handle is not registered
    }

    /* Translate generic wait options to FreeRTOS event-group options */
    const BaseType_t clearOnExit =
        (((uint32_t)options & (uint32_t)TEMPLATE_OSAL_EVENT_FLAGS_NO_CLEAR) != 0u) ? pdFALSE : pdTRUE;
    const BaseType_t waitForAll =
        (((uint32_t)options & (uint32_t)TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ALL) != 0u) ? pdTRUE : pdFALSE;

    /* Wait for the requested event bits */
    const EventBits_t result = xEventGroupWaitBits((EventGroupHandle_t)eventFlagsHandle,
                                                   (EventBits_t)flags,
                                                   clearOnExit,
                                                   waitForAll,
                                                   template_osalFreertosTimeMsToTicksConvert(timeoutMs));
    *actualFlags = (uint32_t)result;

    /* Check whether the requested wait condition was satisfied */
    const bool conditionSatisfied = (waitForAll == pdTRUE)
                                  ? ((result & (EventBits_t)flags) == (EventBits_t)flags)
                                  : ((result & (EventBits_t)flags) != 0u);
    if (!conditionSatisfied)
    {
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event flags condition was not satisfied
    }

    /* Trace output value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsWait: actualFlags = 0x%08lX",
                                 (unsigned long)*actualFlags);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosEventFlagsWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags condition was satisfied
}

// END EVENT_FLAGS

// BEGIN THREAD
/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a FreeRTOS task.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle  Output pointer receiving the thread handle.
 * \param threadAttr    Thread attributes.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosThreadCreate(void *const osal,
                                                            Template_osalThreadHandle_t *const threadHandle,
                                                            Template_osalThreadAttr_s threadAttr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate(%p, %p, {%p, %s, %lu, %p, %d})",
                                 osal,
                                 (void *)threadHandle,
                                 (void *)(uintptr_t)threadAttr.worker,
                                 (threadAttr.name != NULL) ? threadAttr.name : "(null)",
                                 (unsigned long)threadAttr.stackSize,
                                 threadAttr.args,
                                 (int)threadAttr.prio);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(threadHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->threadFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    if (!template_osalFreertosThreadAttrValidate(&threadAttr))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid thread attributes
    }

    /* Clear the output value */
    *threadHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free thread registry slot */
    const size_t threadId = port->base.ptable->threadFreeSlotFind(port);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free thread slot
    }

    const size_t threadIdx                  = threadId - 1u;
    const size_t stackWordSize              = sizeof(StackType_t);
    const size_t stackWordsRaw              = (threadAttr.stackSize + stackWordSize - 1u) / stackWordSize;
    const configSTACK_DEPTH_TYPE stackWords = (configSTACK_DEPTH_TYPE)stackWordsRaw;
    const UBaseType_t priority              = port->param.prio.policy[threadAttr.prio];

    TaskHandle_t nativeThread = NULL;
    BaseType_t rc             = pdFAIL;

    /* Create the native FreeRTOS task using the normalized instance policy */
    #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
        /* Release a retained stack from a task that previously deleted itself in this slot. */
        if (port->threadStackPtr[threadIdx] != NULL)
        {
            vPortFree(port->threadStackPtr[threadIdx]);
            port->threadStackPtr[threadIdx] = NULL;
        }

        const size_t stackSizeBytes = stackWordsRaw * stackWordSize;
        StackType_t *const stackPtr = (StackType_t *)pvPortMalloc(stackSizeBytes);
        if (stackPtr == NULL)
        {
            /* Release the resource mutex */
            (void)template_osalFreertosResourceUnlock(port);
            osalStatus = TEMPLATE_OSAL_THREAD_MEM_ALLOCATION_ERR;

            /* Trace returned value */
            TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

            return osalStatus; // Exit: Error: task stack allocation failed
        }

        port->threadStackPtr[threadIdx] = stackPtr;

        TaskParameters_t taskParam =
        {
            .pvTaskCode     = (TaskFunction_t)threadAttr.worker,
            .pcName         = threadAttr.name,
            .usStackDepth   = stackWords,
            .pvParameters   = threadAttr.args,
            .uxPriority     = priority,
            .puxStackBuffer = stackPtr,
            .xRegions       = {0}
            #if (configSUPPORT_STATIC_ALLOCATION == 1)
                , .pxTaskBuffer = NULL
            #endif
        };

        for (size_t i = 0u; i < portNUM_CONFIGURABLE_REGIONS; ++i)
        {
            taskParam.xRegions[i] = port->param.mpu.region[i];
        }

        #ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP
            rc = xTaskCreateRestrictedAffinitySet(&taskParam,
                                                  port->param.smp.coreAffinityMask[threadIdx],
                                                  &nativeThread);
        #else
            rc = xTaskCreateRestricted(&taskParam, &nativeThread);
        #endif
    #else  /* ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU */
        #ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP
            rc = xTaskCreateAffinitySet((TaskFunction_t)threadAttr.worker,
                                        threadAttr.name,
                                        stackWords,
                                        threadAttr.args,
                                        priority,
                                        port->param.smp.coreAffinityMask[threadIdx],
                                        &nativeThread);
        #else
            rc = xTaskCreate((TaskFunction_t)threadAttr.worker,
                             threadAttr.name,
                             stackWords,
                             threadAttr.args,
                             priority,
                             &nativeThread);
        #endif /* ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP */
    #endif /* ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU */

    if ((rc != pdPASS) ||
        (nativeThread == NULL))
    {
        #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
            vPortFree(port->threadStackPtr[threadIdx]);
            port->threadStackPtr[threadIdx] = NULL;
        #endif

        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: task creation failed
    }

    port->base.threadObjHandle[threadIdx].attr = threadAttr;

    /* Register the created thread */
    port->base.threadObjHandle[threadIdx].handle = (Template_osalThreadHandle_t)nativeThread;
    *threadHandle                                = (Template_osalThreadHandle_t)nativeThread;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was created and registered
}


/**
 * \brief Delete a FreeRTOS task.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle  Thread handle to delete.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosThreadDelete(void *const osal,
                                                            const Template_osalThreadHandle_t threadHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete(%p, %p)",
                                 osal, (void *)threadHandle);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(threadHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->threadHandleFind != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->threadSlotClear != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the thread handle in the registry */
    const size_t threadId = port->base.ptable->threadHandleFind(port, threadHandle);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread handle is not registered
    }

    const size_t threadIdx = threadId - 1u;
    const bool deleteSelf  = ((TaskHandle_t)threadHandle == xTaskGetCurrentTaskHandle());
    port->base.ptable->threadSlotClear(port, threadIdx);

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    if (deleteSelf)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete -> no return");

        /* Delete the native FreeRTOS task */
        vTaskDelete(NULL);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);

        while (1)
        {
            (void)0;
        }
    }

    /* Delete the native FreeRTOS task */
    vTaskDelete((TaskHandle_t)threadHandle);

    #ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
        /* Release the backend-owned stack after an externally deleted restricted task. */
        if (port->threadStackPtr[threadIdx] != NULL)
        {
            vPortFree(port->threadStackPtr[threadIdx]);
            port->threadStackPtr[threadIdx] = NULL;
        }
    #endif

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was deleted
}


/**
 * \brief Suspend a FreeRTOS task.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle  Thread handle to suspend.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosThreadSuspend(void *const osal,
                                                             const Template_osalThreadHandle_t threadHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadSuspend(%p, %p)",
                                 osal, (void *)threadHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(threadHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->threadHandleFind != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->threadSlotClear != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadSuspend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the thread handle in the registry */
    const size_t threadId = port->base.ptable->threadHandleFind(port, threadHandle);

    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadSuspend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread handle is not registered
    }

    /* Suspend the native FreeRTOS task */
    vTaskSuspend((TaskHandle_t)threadHandle);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadSuspend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was suspended
}


/**
 * \brief Resume a FreeRTOS task.
 *
 * \param osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle  Thread handle to resume.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosThreadResume(void *const osal,
                                                            const Template_osalThreadHandle_t threadHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadResume(%p, %p)",
                                 osal, (void *)threadHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(threadHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->threadHandleFind != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->threadSlotClear != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadResume -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the thread handle in the registry */
    const size_t threadId = port->base.ptable->threadHandleFind(port, threadHandle);

    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadResume -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread handle is not registered
    }

    /* Resume the native FreeRTOS task */
    vTaskResume((TaskHandle_t)threadHandle);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadResume -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was resumed
}


/**
 * \brief Delay the calling FreeRTOS task.
 *
 * \param osal     Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param delayMs  Delay duration in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosThreadDelay(void *const osal,
                                                           const Template_osalTimeMs_t delayMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelay(%p, %u)",
                                 osal, (unsigned int)delayMs);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    if (!template_osalFreertosIsValid(port))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_NOT_INIT_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelay -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: backend is not initialized
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelay -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Delay the calling FreeRTOS task */
    vTaskDelay(template_osalFreertosTimeMsToTicksConvert(delayMs));

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelay -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread delay completed
}


/**
 * \brief Delay the calling FreeRTOS task until the next periodic wake-up point.
 *
 * \param osal                Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param previousWakeTimeMs  In/out periodic wake-up reference in OSAL milliseconds.
 * \param periodMs            Period in milliseconds; must be non-zero.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosThreadDelayUntil(void *const osal,
                                                                Template_osalTimeMs_t *const previousWakeTimeMs,
                                                                const Template_osalTimeMs_t periodMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelayUntil(%p, %p, %u)",
                                 osal, (void *)previousWakeTimeMs, (unsigned int)periodMs);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(previousWakeTimeMs != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(periodMs != 0u);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelayUntil -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Convert the generic OSAL wake reference and period to FreeRTOS ticks */
    TickType_t previousWakeTicks = template_osalFreertosTimeMsToTicksConvert(*previousWakeTimeMs);
    const TickType_t periodTicks = template_osalFreertosTimeMsToTicksConvert(periodMs);

    /* Delay until the next scheduled wake-up point */
    (void)xTaskDelayUntil(&previousWakeTicks, periodTicks);

    /* Convert the updated scheduled wake reference back to OSAL milliseconds */
    *previousWakeTimeMs = (Template_osalTimeMs_t)(((uint64_t)previousWakeTicks * 1000u) /
                                                  (uint64_t)configTICK_RATE_HZ);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelayUntil -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: periodic delay completed
}


/**
 * \brief Terminate the calling FreeRTOS task.
 *
 * \param osal  Opaque pointer to the initialized FreeRTOS OSAL instance.
 *
 * \note This function does not return on a valid call.
 */
static void template_osalFreertosThreadExit(void *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit(%p)", osal);

    /* Validate input args */
    if (osal == NULL)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> invalid OSAL");

        return;  // Exit: Error: invalid OSAL instance
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> ISR context is not supported");

        return;  // Exit: Error: ISR context is not supported
    }

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    if (!template_osalFreertosIsValid(port) ||
        (port->base.ptable == NULL) ||
        (port->base.ptable->threadHandleFind == NULL) ||
        (port->base.ptable->threadSlotClear == NULL))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> invalid backend state");

        return;  // Exit: Error: backend invariant is not satisfied
    }

    /* Get the native handle of the calling task */
    const TaskHandle_t currentThread = xTaskGetCurrentTaskHandle();
    if (currentThread == NULL)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> current task is unavailable");

        return;  // Exit: Error: current task handle is unavailable
    }

    /* Acquire the resource mutex */
    Template_osalErr_e osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> resource lock failed: %d",
                                     (int)osalStatus);

        return;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the calling task handle in the registry */
    const size_t threadId =
        port->base.ptable->threadHandleFind(port, (Template_osalThreadHandle_t)currentThread);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> current task is not registered");

        return;  // Exit: Error: current task is not registered
    }

    /* Clear the thread registry slot before deleting the current task */
    port->base.ptable->threadSlotClear(port, threadId - 1u);

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> resource unlock failed: %d",
                                     (int)osalStatus);

        return;  // Exit: Error: resource mutex release failed
    }

    /* Delete the calling FreeRTOS task */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> no return");
    vTaskDelete(NULL);

    /* vTaskDelete(NULL) shall not return */
    TEMPLATE_OSAL_FREERTOS_ASSERT(0);

    while (1)
    {
        (void)0;
    }
}


/**
 * \brief Validate FreeRTOS thread attributes.
 *
 * \param threadAttr  Pointer to the thread attributes.
 *
 * \return true if the thread attributes are valid; false otherwise.
 */
static bool template_osalFreertosThreadAttrValidate(const Template_osalThreadAttr_s *const threadAttr)
{
    bool isValid = true;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadAttrValidate(%p)",
                                 (const void *)threadAttr);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(threadAttr != NULL);

    if (threadAttr->worker == NULL)
    {
        isValid = false;
    }

    if ((threadAttr->prio < TEMPLATE_OSAL_THREAD_PRIO_LOW) ||
        (threadAttr->prio >= TEMPLATE_OSAL_THREAD_PRIO_MAX_COUNT))
    {
        isValid = false;
    }

    const size_t stackWordSize = sizeof(StackType_t);
    if ((threadAttr->stackSize == 0u) ||
        (threadAttr->stackSize > (SIZE_MAX - (stackWordSize - 1u))))
    {
        isValid = false;
    }
    else
    {
        const size_t stackWords = (threadAttr->stackSize + stackWordSize - 1u) / stackWordSize;
        if ((stackWords < (size_t)configMINIMAL_STACK_SIZE) ||
            (stackWords > (size_t)((configSTACK_DEPTH_TYPE) - 1)))
        {
            isValid = false;
        }
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadAttrValidate -> %d", (int)isValid);

    return isValid;  // Exit: Success: validation result returned
}


// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ------------------------*/

/**
 * \brief Enter a FreeRTOS critical section from task context.
 *
 * \details This primitive does not allocate a registry-backed object; it maps
 *          the unified OSAL critical-section contract to the FreeRTOS backend.
 *
 * \param osal  Opaque pointer to the initialized FreeRTOS OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosCriticalSectionEnter(void *const osal)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosCriticalSectionEnter(%p)", osal);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid((const Template_osalFreertos_s *)osal));

    /* Keep the parameter referenced when tracing/assertions are compiled out */
    (void)osal;

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosCriticalSectionEnter -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    taskENTER_CRITICAL();

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosCriticalSectionEnter -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: critical section was entered
}


/**
 * \brief Exit a previously entered FreeRTOS critical section from task context.
 *
 * \details This primitive does not allocate a registry-backed object; it maps
 *          the unified OSAL critical-section contract to the FreeRTOS backend.
 *
 * \param osal  Opaque pointer to the initialized FreeRTOS OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosCriticalSectionExit(void *const osal)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosCriticalSectionExit(%p)", osal);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid((const Template_osalFreertos_s *)osal));

    /* Keep the parameter referenced when tracing/assertions are compiled out */
    (void)osal;

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosCriticalSectionExit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    taskEXIT_CRITICAL();

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosCriticalSectionExit -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: critical section was exited
}


// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------------- Software timers -------------------------*/

/**
 * \brief Create a FreeRTOS software timer and register it in the OSAL instance.
 *
 * \details The operation follows the component-scoped OSAL ownership model and
 *          validates/registers the native object through the OSAL registry.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param timerHandle  Output pointer receiving the created timer handle.
 * \param timerAttr    Software timer creation configuration.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerCreate(void *const osal,
                                                                   Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                   Template_osalSoftwareTimerAttr_s timerAttr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate(%p, %p, {%s, %p, %p, %d, %u})",
                                 osal,
                                 (void *)timerHandle,
                                 (timerAttr.name != NULL) ? timerAttr.name : "(null)",
                                 timerAttr.timerParam,
                                 (void *)(uintptr_t)timerAttr.timerExpiredCb,
                                 (int)timerAttr.autoReload,
                                 (unsigned int)timerAttr.periodMs);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerAttr.timerExpiredCb != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerAttr.periodMs != 0u);

    *timerHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->softwareTimerFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    const size_t timerId = port->base.ptable->softwareTimerFreeSlotFind(port);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free software-timer registry slot
    }

    Template_osalSoftwareTimer_s *const timerObj = &port->base.softwareTimerObj[timerId - 1u];
    timerObj->attr = timerAttr;

    const TimerHandle_t nativeHandle =
        xTimerCreate(timerAttr.name,
                     template_osalFreertosTimeMsToTicksConvert(timerAttr.periodMs),
                     timerAttr.autoReload ? pdTRUE : pdFALSE,
                     timerObj,
                     template_osalFreertosSoftwareTimerCallback);
    if (nativeHandle == NULL)
    {
        timerObj->attr.name           = NULL;
        timerObj->attr.timerParam     = NULL;
        timerObj->attr.timerExpiredCb = NULL;
        timerObj->attr.autoReload     = false;
        timerObj->attr.periodMs       = 0u;
        (void)template_osalFreertosResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: software-timer allocation failed
    }

    timerObj->handle = (Template_osalSoftwareTimerHandle_t)nativeHandle;
    *timerHandle     = (Template_osalSoftwareTimerHandle_t)nativeHandle;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was created and registered
}


/**
 * \brief Delete a registered FreeRTOS software timer.
 *
 * \details The handle must belong to this OSAL instance. Registry bookkeeping
 *          is updated together with the native FreeRTOS resource lifecycle.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param timerHandle  Registered software timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerDelete(void *const osal,
                                                                   const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerDelete(%p, %p)",
                                 osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find and validate the resource handle in the component registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        (void)template_osalFreertosResourceUnlock(port);
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    if (xTimerDelete((TimerHandle_t)timerHandle, 0u) != pdPASS)
    {
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native FreeRTOS operation failed
    }

    Template_osalSoftwareTimer_s *const timerObj = &port->base.softwareTimerObj[timerId - 1u];
    timerObj->handle              = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;
    timerObj->attr.name           = NULL;
    timerObj->attr.timerParam     = NULL;
    timerObj->attr.timerExpiredCb = NULL;
    timerObj->attr.autoReload     = false;
    timerObj->attr.periodMs       = 0u;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was deleted and unregistered
}


/**
 * \brief Start a registered FreeRTOS software timer.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param timerHandle  Registered software timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStart(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t timerStatus        = pdFAIL;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStart(%p, %p)",
                                 osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Find and validate the resource handle in the component registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStart -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        timerStatus = xTimerStartFromISR((TimerHandle_t)timerHandle, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        timerStatus = xTimerStart((TimerHandle_t)timerHandle, 0u);
    }

    if (timerStatus != pdPASS)
    {
        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_START_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStart -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native software-timer start failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStart -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was started
}


/**
 * \brief Stop a registered FreeRTOS software timer.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param timerHandle  Registered software timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStop(void *const osal,
                                                                 const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t timerStatus        = pdFAIL;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStop(%p, %p)",
                                 osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Find and validate the resource handle in the component registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStop -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        timerStatus = xTimerStopFromISR((TimerHandle_t)timerHandle, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        timerStatus = xTimerStop((TimerHandle_t)timerHandle, 0u);
    }

    if (timerStatus != pdPASS)
    {
        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_STOP_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStop -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native software-timer stop failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerStop -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was stopped
}


/**
 * \brief Reset and restart the period of a registered FreeRTOS software timer.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param timerHandle  Registered software timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerReset(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t timerStatus        = pdFAIL;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerReset(%p, %p)",
                                 osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Find and validate the resource handle in the component registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        timerStatus = xTimerResetFromISR((TimerHandle_t)timerHandle, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        timerStatus = xTimerReset((TimerHandle_t)timerHandle, 0u);
    }

    if (timerStatus != pdPASS)
    {
        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_RESET_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native software-timer reset failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerReset -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was reset
}


/**
 * \brief Dispatch a native FreeRTOS timer callback to the component callback.
 *
 * \details The native timer identifier stores the OSAL timer object used to
 *          recover the component callback and user parameter.
 *
 * \param timerHandle  Native FreeRTOS timer handle passed by the timer service.
 */
static void template_osalFreertosSoftwareTimerCallback(TimerHandle_t timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCallback(%p)", (void *)timerHandle);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerHandle != NULL);

    Template_osalSoftwareTimer_s *const timerObj =
        (Template_osalSoftwareTimer_s *)pvTimerGetTimerID(timerHandle);

    TEMPLATE_OSAL_FREERTOS_ASSERT(timerObj != NULL);
    if (timerObj == NULL)
    {
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCallback -> invalid timer object");

        return;  // Exit: Error: timer callback object is invalid
    }

    TEMPLATE_OSAL_FREERTOS_ASSERT(timerObj->handle == (Template_osalSoftwareTimerHandle_t)timerHandle);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerObj->attr.timerExpiredCb != NULL);

    if ((timerObj->handle != (Template_osalSoftwareTimerHandle_t)timerHandle) ||
        (timerObj->attr.timerExpiredCb == NULL))
    {
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCallback -> invalid timer state");

        return;  // Exit: Error: timer callback state is invalid
    }

    timerObj->attr.timerExpiredCb(timerObj->attr.timerParam);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCallback -> ok");
}


// END SOFTWARE_TIMER

/*--------------------------------- Time ----------------------------------*/

// BEGIN TIME
/**
 * \brief Retrieve the current FreeRTOS system time in milliseconds.
 *
 * \param osal      Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param osTimeMs  Output pointer receiving the current time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosTimeMsGet(void *const osal,
                                                         Template_osalTimeMs_t *const osTimeMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsGet(%p, %p)",
                                 osal, (void *)osTimeMs);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(osTimeMs != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    if (!template_osalFreertosIsValid(port))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_NOT_INIT_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: backend is not initialized
    }

    TickType_t tickCount = 0u;

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Read the system tick count from ISR context */
        tickCount = xTaskGetTickCountFromISR();
    }
    else
    {
        /* Read the system tick count */
        tickCount = xTaskGetTickCount();
    }

    *osTimeMs = (Template_osalTimeMs_t)(((uint64_t)tickCount * 1000u) /
                                        (uint64_t)configTICK_RATE_HZ);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: system time was read
}

// END TIME

/**
 * \brief Convert milliseconds to FreeRTOS ticks.
 *
 * \param timeMs  Time interval in milliseconds.
 *
 * \return Converted FreeRTOS tick count.
 */
static inline TickType_t template_osalFreertosTimeMsToTicksConvert(const Template_osalTimeMs_t timeMs)
{
    TickType_t tickCount = 0u;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsToTicksConvert(%lu)",
                                 (unsigned long)timeMs);

    if (timeMs == TEMPLATE_OSAL_INFINITY_TOUT)
    {
        tickCount = portMAX_DELAY;
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsToTicksConvert -> %lu",
                                     (unsigned long)tickCount);

        return tickCount;  // Exit: Success: infinite timeout converted
    }

    tickCount = pdMS_TO_TICKS(timeMs);
    if ((timeMs != 0u) &&
        (tickCount == 0u))
    {
        tickCount = 1u;
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsToTicksConvert -> %lu",
                                 (unsigned long)tickCount);

    return tickCount;  // Exit: Success: timeout converted
}

// BEGIN MEMORY
/*-------------------------------- Memory ---------------------------------*/

/**
 * \brief Allocate memory from the FreeRTOS heap and register the resulting pointer.
 *
 * \param osal    Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param size    Allocation size in bytes.
 * \param memPtr  Output pointer receiving the allocated memory address.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMemAlloc(void *const osal,
                                                        const size_t size,
                                                        void **const memPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc(%p, %lu, %p)",
                                 osal, (unsigned long)size, (void *)memPtr);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(memPtr != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(size != 0u);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->memFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Clear the output value */
    *memPtr = NULL;

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free memory registry slot */
    const size_t memoryId = port->base.ptable->memFreeSlotFind(port);
    if ((memoryId == 0u) ||
        (memoryId > TEMPLATE_OSAL_MEM_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free memory registry slot
    }

    /* Allocate memory from the FreeRTOS heap */
    void *const allocatedPtr = pvPortMalloc(size);
    if (allocatedPtr == NULL)
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: FreeRTOS heap allocation failed
    }

    /* Register the allocated memory pointer */
    port->base.memPtr[memoryId - 1u] = allocatedPtr;
    *memPtr                          = allocatedPtr;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: memory block was allocated and registered
}


/**
 * \brief Free a registered memory block allocated from the FreeRTOS heap.
 *
 * \param osal    Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param memPtr  Registered memory pointer to release.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMemFree(void *const osal,
                                                       void *const memPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemFree(%p, %p)", osal, memPtr);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(memPtr != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->memPtrFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the memory pointer in the registry */
    const size_t memoryId = port->base.ptable->memPtrFind(port, memPtr);
    if ((memoryId == 0u) ||
        (memoryId > TEMPLATE_OSAL_MEM_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: memory pointer is not registered
    }

    /* Clear the registry slot */
    port->base.memPtr[memoryId - 1u] = NULL;

    /* Release memory to the FreeRTOS heap */
    vPortFree(memPtr);

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemFree -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: memory block was freed and unregistered
}


// END MEMORY

/*------------------------------- Predicate -------------------------------*/

/**
 * \brief Validate the FreeRTOS OSAL backend.
 *
 * \param osal  Opaque pointer to the FreeRTOS OSAL instance.
 *
 * \return true if the instance is valid and initialized, otherwise false.
 */
static bool template_osalFreertosIsValid(const void *const osal)
{
    bool isValid = false;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosIsValid(%p)", osal);

    if (osal == NULL)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosIsValid -> %d", (int)isValid);

        return isValid;  // Exit: Error: invalid args
    }

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    const Template_osalFreertos_s *const port = (const Template_osalFreertos_s *)osal;
    isValid =
        (port->validFlag == true) &&
        (port->resourceMutex != NULL) &&
        (port->base.validFlag == true) &&
        (port->base.vtable == &template_osalFreertosVtable) &&
        (port->base.ptable != NULL);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosIsValid -> %d", (int)isValid);

    return isValid;  // Exit: Success: validation result returned
}

/*-------------------------- Resource synchronization ---------------------*/

/**
 * \brief Acquire the internal resource mutex.
 *
 * \param osalFreertos  Pointer to the initialized FreeRTOS OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static inline Template_osalErr_e template_osalFreertosResourceLock(Template_osalFreertos_s *const osalFreertos)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosResourceLock(%p)",
                                 (void *)osalFreertos);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(osalFreertos->resourceMutex != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(!xPortIsInsideInterrupt());

    if (xSemaphoreTake(osalFreertos->resourceMutex, portMAX_DELAY) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosResourceLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosResourceLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: resource mutex was acquired
}


/**
 * \brief Release the internal resource mutex.
 *
 * \param osalFreertos  Pointer to the initialized FreeRTOS OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static inline Template_osalErr_e template_osalFreertosResourceUnlock(Template_osalFreertos_s *const osalFreertos)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosResourceUnlock(%p)",
                                 (void *)osalFreertos);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(osalFreertos->resourceMutex != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(!xPortIsInsideInterrupt());

    if (xSemaphoreGive(osalFreertos->resourceMutex) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosResourceUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosResourceUnlock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: resource mutex was released
}
