/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

/**
 * \file     template_osal_posix.c
 * \brief    POSIX OSAL port for Template.
 * \details  POSIX implementation of the component-scoped Template OSAL contract.
 *
 * \note     Milestone 1 implements queues, recursive mutexes, counting semaphores,
 *           core thread lifecycle/delay operations, monotonic time and memory.
 *           Stream buffers, event flags, critical sections and software timers are
 *           intentionally kept as compile-time stubs for a subsequent milestone.
 */

//===============================================================================[ INCLUDE ]========================================================================================

#include "template_osal_posix.h"
#include "template_osal.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//=====================================================================[ INTERNAL MACRO DEFINITIONS ]===============================================================================

/**
 * \def   TEMPLATE_OSAL_POSIX_ASSERT
 * \brief Assertion macro for the POSIX OSAL backend.
 */
#ifndef TEMPLATE_OSAL_POSIX_ASSERT
    #if defined(TEMPLATE_OSAL_ASSERT)
        #define TEMPLATE_OSAL_POSIX_ASSERT(cond)    TEMPLATE_OSAL_ASSERT(cond)
    #elif defined(TEMPLATE_ASSERT)
        #define TEMPLATE_OSAL_POSIX_ASSERT(cond)    TEMPLATE_ASSERT(cond)
    #else
        #include <assert.h>
        #define TEMPLATE_OSAL_POSIX_ASSERT(cond)    assert(cond)
    #endif
#endif

/**
 * \def   TEMPLATE_OSAL_POSIX_TRACE
 * \brief Tracing macro for the POSIX OSAL backend.
 */
#ifndef TEMPLATE_OSAL_POSIX_TRACE
    #if defined(TEMPLATE_OSAL_TRACE)
        #define TEMPLATE_OSAL_POSIX_TRACE(...)    TEMPLATE_OSAL_TRACE(__VA_ARGS__)
    #elif defined(TEMPLATE_TRACE)
        #define TEMPLATE_OSAL_POSIX_TRACE(...)    TEMPLATE_TRACE(__VA_ARGS__)
    #else
        #define TEMPLATE_OSAL_POSIX_TRACE(...)    ((void)0)
    #endif
#endif

//====================================================================[ INTERNAL DATA TYPES DEFINITIONS ]===========================================================================

// BEGIN QUEUE
/**
 * \struct  Template_osalPosixQueue_s
 * \brief   POSIX bounded FIFO queue control block.
 * \details The ring buffer is protected by a native mutex. Two backend-private
 *          POSIX semaphores represent free and occupied slots. They are not
 *          generic OSAL semaphore objects and never consume semaphore registry slots.
 */
typedef struct
{
    size_t          itemSize;       /*!< Size of one queue item in bytes. */
    size_t          depth;          /*!< Maximum number of queue items. */
    size_t          readIdx;        /*!< Next ring-buffer read index. */
    size_t          writeIdx;       /*!< Next ring-buffer write index. */
    size_t          itemCount;      /*!< Number of committed queue items. */
    void           *buffer;         /*!< Ring-buffer storage. */
    pthread_mutex_t mutex;          /*!< Native ring-buffer protection mutex. */
    sem_t           freeSlotsSmphr; /*!< Backend-private count of free queue slots. */
    sem_t           busySlotsSmphr; /*!< Backend-private count of occupied queue slots. */
} Template_osalPosixQueue_s;
// END QUEUE

// BEGIN MUTEX
/**
 * \struct  Template_osalPosixMutex_s
 * \brief   POSIX recursive-mutex control block.
 */
typedef struct
{
    pthread_mutex_t mutex; /*!< Native recursive POSIX mutex. */
} Template_osalPosixMutex_s;
// END MUTEX

// BEGIN SEMAPHORE
/**
 * \struct  Template_osalPosixSemaphore_s
 * \brief   POSIX counting-semaphore control block.
 * \details availableCountSmphr holds consumable counts. freeCountSmphr tracks
 *          the remaining configured capacity so Post cannot exceed maxCount.
 *          Both native semaphores are backend-private implementation details.
 */
typedef struct
{
    sem_t                        availableCountSmphr; /*!< Counts currently available to Wait/Pend. */
    sem_t                        freeCountSmphr;      /*!< Remaining capacity up to maxCount. */
    Template_osalSemaphoreCount_t maxCount;           /*!< Configured maximum semaphore count. */
} Template_osalPosixSemaphore_s;
// END SEMAPHORE

// BEGIN THREAD
/**
 * \struct  Template_osalPosixThreadArg_s
 * \brief   Argument pack adapting the OSAL worker signature to pthread entry.
 */
typedef struct
{
    Template_osalThreadWorker_f worker;     /*!< OSAL worker entry function. */
    void                       *workerArgs; /*!< User argument passed to the worker. */
} Template_osalPosixThreadArg_s;

/**
 * \struct  Template_osalPosixThread_s
 * \brief   POSIX thread control block owned by the POSIX backend.
 */
typedef struct
{
    pthread_t                    thread; /*!< Native pthread identifier. */
    Template_osalPosixThreadArg_s arg;   /*!< Embedded pthread thunk argument pack. */
} Template_osalPosixThread_s;
// END THREAD

//===============================================================[ INTERNAL FUNCTIONS AND OBJECTS DECLARATION ]=====================================================================

// BEGIN QUEUE
/*-------------------------------- Queues ---------------------------------*/

/**
 * \brief Create a bounded POSIX queue and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalPosixQueueCreate(void *const osal,
                                                        const size_t queueItemSize,
                                                        const size_t queueDepth,
                                                        Template_osalQueueHandle_t *const queueHandle);

/**
 * \brief Delete a registered POSIX queue.
 */
static Template_osalErr_e template_osalPosixQueueDelete(void *const osal,
                                                        const Template_osalQueueHandle_t queueHandle);

/**
 * \brief Put an item into a registered POSIX queue without waiting for capacity.
 */
static Template_osalErr_e template_osalPosixQueueItemPut(void *const osal,
                                                         const Template_osalQueueHandle_t queueHandle,
                                                         const void *const queueItemPtr);

/**
 * \brief Post an item to a registered POSIX queue using the requested timeout.
 */
static Template_osalErr_e template_osalPosixQueueItemPost(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle,
                                                          const void *const queueItemPtr,
                                                          const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Retrieve an already available item from a registered POSIX queue without waiting.
 */
static Template_osalErr_e template_osalPosixQueueItemGet(void *const osal,
                                                         const Template_osalQueueHandle_t queueHandle,
                                                         void *const queueItemPtr);

/**
 * \brief Wait indefinitely for an item from a registered POSIX queue.
 */
static Template_osalErr_e template_osalPosixQueueItemWait(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle,
                                                          void *const queueItemPtr);

/**
 * \brief Pend an item from a registered POSIX queue using the requested timeout.
 */
static Template_osalErr_e template_osalPosixQueueItemPend(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle,
                                                          void *const queueItemPtr,
                                                          const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Reset a registered POSIX queue to its initial empty state.
 */
static Template_osalErr_e template_osalPosixQueueReset(void *const osal,
                                                       const Template_osalQueueHandle_t queueHandle);
// END QUEUE

// BEGIN STREAM_BUFFER
/*----------------------------- Stream buffers -----------------------------*/

/**
 * \brief Create a POSIX stream buffer and register it in the OSAL instance.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferCreate(void *const osal,
                                                               const size_t bufferSizeBytes,
                                                               const size_t triggerLevelBytes,
                                                               Template_osalStreamBufferHandle_t *const streamBufferHandle);

/**
 * \brief Delete a registered POSIX stream buffer.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferDelete(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Put bytes into a registered POSIX stream buffer without waiting.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferPut(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            const void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesPut);

/**
 * \brief Put bytes into a registered POSIX stream buffer using the requested timeout.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferPost(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             const void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesPut);

/**
 * \brief Get already available bytes from a registered POSIX stream buffer.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferGet(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesGet);

/**
 * \brief Wait indefinitely for bytes from a registered POSIX stream buffer.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferWait(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             size_t *const bytesGet);

/**
 * \brief Get bytes from a registered POSIX stream buffer using the requested timeout.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferPend(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesGet);

/**
 * \brief Reset a registered POSIX stream buffer.
 * \note Milestone 1 stub; POSIX stream buffers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixStreamBufferReset(void *const osal,
                                                              const Template_osalStreamBufferHandle_t streamBufferHandle);
// END STREAM_BUFFER

// BEGIN MUTEX
/*-------------------------------- Mutexes ----------------------------------*/

/**
 * \brief Create a recursive POSIX mutex and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalPosixMutexCreate(void *const osal,
                                                        Template_osalMutexHandle_t *const mutexHandle);

/**
 * \brief Delete a registered recursive POSIX mutex.
 */
static Template_osalErr_e template_osalPosixMutexDelete(void *const osal,
                                                        const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Lock a registered recursive POSIX mutex indefinitely.
 */
static Template_osalErr_e template_osalPosixMutexLock(void *const osal,
                                                      const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Try to lock a registered recursive POSIX mutex without waiting.
 */
static Template_osalErr_e template_osalPosixMutexTryLock(void *const osal,
                                                         const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Lock a registered recursive POSIX mutex using the requested timeout.
 */
static Template_osalErr_e template_osalPosixMutexPendLock(void *const osal,
                                                          const Template_osalMutexHandle_t mutexHandle,
                                                          const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Unlock a registered recursive POSIX mutex.
 */
static Template_osalErr_e template_osalPosixMutexUnlock(void *const osal,
                                                        const Template_osalMutexHandle_t mutexHandle);
// END MUTEX

// BEGIN SEMAPHORE
/*--------------------------- Counting semaphores --------------------------*/

/**
 * \brief Create a bounded POSIX counting semaphore and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalPosixSemaphoreCreate(void *const osal,
                                                            const Template_osalSemaphoreCount_t maxCount,
                                                            const Template_osalSemaphoreCount_t initialCount,
                                                            Template_osalSemaphoreHandle_t *const semaphoreHandle);

/**
 * \brief Delete a registered POSIX counting semaphore.
 */
static Template_osalErr_e template_osalPosixSemaphoreDelete(void *const osal,
                                                            const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Wait indefinitely for one count from a registered POSIX counting semaphore.
 */
static Template_osalErr_e template_osalPosixSemaphoreWait(void *const osal,
                                                          const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Wait for one count from a registered POSIX counting semaphore using the requested timeout.
 */
static Template_osalErr_e template_osalPosixSemaphorePend(void *const osal,
                                                          const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                          const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Post one count to a registered POSIX counting semaphore.
 */
static Template_osalErr_e template_osalPosixSemaphorePost(void *const osal,
                                                          const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Retrieve the current count of a registered POSIX counting semaphore.
 */
static Template_osalErr_e template_osalPosixSemaphoreCountGet(void *const osal,
                                                              const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                              Template_osalSemaphoreCount_t *const semaphoreCount);
// END SEMAPHORE

// BEGIN EVENT_FLAGS
/*-------------------------------- Event flags ------------------------------*/

/**
 * \brief Create a POSIX event-flags object.
 * \note Milestone 1 stub; POSIX event flags are not implemented yet.
 */
static Template_osalErr_e template_osalPosixEventFlagsCreate(void *const osal,
                                                             Template_osalEventFlagsHandle_t *const eventFlagsHandle);

/**
 * \brief Delete a POSIX event-flags object.
 * \note Milestone 1 stub; POSIX event flags are not implemented yet.
 */
static Template_osalErr_e template_osalPosixEventFlagsDelete(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle);

/**
 * \brief Set bits in a POSIX event-flags object.
 * \note Milestone 1 stub; POSIX event flags are not implemented yet.
 */
static Template_osalErr_e template_osalPosixEventFlagsSet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          const uint32_t flags);

/**
 * \brief Clear bits in a POSIX event-flags object.
 * \note Milestone 1 stub; POSIX event flags are not implemented yet.
 */
static Template_osalErr_e template_osalPosixEventFlagsClear(void *const osal,
                                                            const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                            const uint32_t flags);

/**
 * \brief Read a POSIX event-flags object.
 * \note Milestone 1 stub; POSIX event flags are not implemented yet.
 */
static Template_osalErr_e template_osalPosixEventFlagsGet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          uint32_t *const flags);

/**
 * \brief Wait for a POSIX event-flags condition.
 * \note Milestone 1 stub; POSIX event flags are not implemented yet.
 */
static Template_osalErr_e template_osalPosixEventFlagsWait(void *const osal,
                                                           const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                           const uint32_t flags,
                                                           const Template_osalEventFlagsOptions_e options,
                                                           const Template_osalTimeMs_t timeoutMs,
                                                           uint32_t *const actualFlags);
// END EVENT_FLAGS

// BEGIN THREAD
/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a POSIX thread and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalPosixThreadCreate(void *const osal,
                                                         Template_osalThreadHandle_t *const threadHandle,
                                                         Template_osalThreadAttr_s threadAttr);

/**
 * \brief Delete a registered POSIX thread synchronously.
 */
static Template_osalErr_e template_osalPosixThreadDelete(void *const osal,
                                                         const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Suspend a registered POSIX thread.
 * \note Milestone 1 runtime stub; portable pthreads provide no direct suspend primitive.
 */
static Template_osalErr_e template_osalPosixThreadSuspend(void *const osal,
                                                          const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Resume a registered POSIX thread.
 * \note Milestone 1 runtime stub; portable pthreads provide no direct resume primitive.
 */
static Template_osalErr_e template_osalPosixThreadResume(void *const osal,
                                                         const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Delay the calling POSIX thread.
 */
static Template_osalErr_e template_osalPosixThreadDelay(void *const osal,
                                                        const Template_osalTimeMs_t delayMs);

/**
 * \brief Delay the calling POSIX thread until the next periodic wake-up point.
 */
static Template_osalErr_e template_osalPosixThreadDelayUntil(void *const osal,
                                                             Template_osalTimeMs_t *const previousWakeTimeMs,
                                                             const Template_osalTimeMs_t periodMs);

/**
 * \brief Terminate the calling POSIX thread.
 */
static void template_osalPosixThreadExit(void *const osal);

/**
 * \brief Validate POSIX thread attributes.
 */
static bool template_osalPosixThreadAttrValidate(const Template_osalThreadAttr_s *const threadAttr);

/**
 * \brief Adapt the OSAL worker signature to the pthread entry signature.
 */
static void *template_osalPosixThreadThunk(void *const context);
// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ------------------------*/

/**
 * \brief Enter a POSIX critical section.
 * \note Milestone 1 stub; the POSIX critical-section policy is not implemented yet.
 */
static Template_osalErr_e template_osalPosixCriticalSectionEnter(void *const osal);

/**
 * \brief Exit a POSIX critical section.
 * \note Milestone 1 stub; the POSIX critical-section policy is not implemented yet.
 */
static Template_osalErr_e template_osalPosixCriticalSectionExit(void *const osal);
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------------- Software timers -------------------------*/

/**
 * \brief Create a POSIX software timer.
 * \note Milestone 1 stub; POSIX software timers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerCreate(void *const osal,
                                                                Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                Template_osalSoftwareTimerAttr_s timerAttr);

/**
 * \brief Delete a POSIX software timer.
 * \note Milestone 1 stub; POSIX software timers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerDelete(void *const osal,
                                                                const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Start a POSIX software timer.
 * \note Milestone 1 stub; POSIX software timers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStart(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Stop a POSIX software timer.
 * \note Milestone 1 stub; POSIX software timers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStop(void *const osal,
                                                              const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Reset a POSIX software timer.
 * \note Milestone 1 stub; POSIX software timers are not implemented yet.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerReset(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle);
// END SOFTWARE_TIMER

// BEGIN TIME
/*--------------------------------- Time ----------------------------------*/

/**
 * \brief Retrieve the current POSIX monotonic time in milliseconds.
 */
static Template_osalErr_e template_osalPosixTimeMsGet(void *const osal,
                                                      Template_osalTimeMs_t *const osTimeMs);
// END TIME

// BEGIN MEMORY
/*-------------------------------- Memory ---------------------------------*/

/**
 * \brief Allocate memory from the host C runtime and register it in the OSAL instance.
 */
static Template_osalErr_e template_osalPosixMemAlloc(void *const osal,
                                                     const size_t size,
                                                     void **const memPtr);

/**
 * \brief Free host memory previously allocated and registered by the POSIX backend.
 */
static Template_osalErr_e template_osalPosixMemFree(void *const osal,
                                                    void *const memPtr);
// END MEMORY

/*------------------------------- Predicate -------------------------------*/

/**
 * \brief Validate the POSIX OSAL backend.
 */
static bool template_osalPosixIsValid(const void *const osal);

/*-------------------------- Resource synchronization ---------------------*/

/**
 * \brief Acquire the internal resource mutex.
 */
static inline Template_osalErr_e template_osalPosixResourceLock(Template_osalPosix_s *const osalPosix);

/**
 * \brief Release the internal resource mutex.
 */
static inline Template_osalErr_e template_osalPosixResourceUnlock(Template_osalPosix_s *const osalPosix);

/*---------------------------- Native time helpers ------------------------*/

/**
 * \brief Add milliseconds to a normalized POSIX timespec value.
 */
static inline void template_osalPosixTimespecAddMs(struct timespec *const timeSpec,
                                                   const Template_osalTimeMs_t timeMs);

/**
 * \brief Build an absolute CLOCK_REALTIME deadline for POSIX timed waits.
 */
static inline bool template_osalPosixRealtimeDeadlineGet(const Template_osalTimeMs_t timeoutMs,
                                                         struct timespec *const deadline);

/**
 * \brief Wait on a native POSIX semaphore using the generic OSAL timeout semantics.
 */
static inline int template_osalPosixSemaphorePendNative(sem_t *const semaphore,
                                                        const Template_osalTimeMs_t timeoutMs);

/**
 * \brief POSIX OSAL backend vtable.
 */
static const Template_osalVtable_s template_osalPosixVtable =
{
// BEGIN QUEUE
    /*-------------------------------- Queues ---------------------------------*/
    .queueCreate   = template_osalPosixQueueCreate,
    .queueDelete   = template_osalPosixQueueDelete,
    .queueItemPut  = template_osalPosixQueueItemPut,
    .queueItemPost = template_osalPosixQueueItemPost,
    .queueItemGet  = template_osalPosixQueueItemGet,
    .queueItemWait = template_osalPosixQueueItemWait,
    .queueItemPend = template_osalPosixQueueItemPend,
    .queueReset    = template_osalPosixQueueReset,
// END QUEUE

// BEGIN STREAM_BUFFER
    /*----------------------------- Stream buffers ----------------------------*/
    .streamBufferCreate = template_osalPosixStreamBufferCreate,
    .streamBufferDelete = template_osalPosixStreamBufferDelete,
    .streamBufferPut    = template_osalPosixStreamBufferPut,
    .streamBufferPost   = template_osalPosixStreamBufferPost,
    .streamBufferGet    = template_osalPosixStreamBufferGet,
    .streamBufferWait   = template_osalPosixStreamBufferWait,
    .streamBufferPend   = template_osalPosixStreamBufferPend,
    .streamBufferReset  = template_osalPosixStreamBufferReset,
// END STREAM_BUFFER

// BEGIN MUTEX
    /*-------------------------------- Mutexes ----------------------------------*/
    .mutexCreate   = template_osalPosixMutexCreate,
    .mutexDelete   = template_osalPosixMutexDelete,
    .mutexLock     = template_osalPosixMutexLock,
    .mutexTryLock  = template_osalPosixMutexTryLock,
    .mutexPendLock = template_osalPosixMutexPendLock,
    .mutexUnlock   = template_osalPosixMutexUnlock,
// END MUTEX

// BEGIN SEMAPHORE
    /*--------------------------- Counting semaphores --------------------------*/
    .semaphoreCreate   = template_osalPosixSemaphoreCreate,
    .semaphoreDelete   = template_osalPosixSemaphoreDelete,
    .semaphoreWait     = template_osalPosixSemaphoreWait,
    .semaphorePend     = template_osalPosixSemaphorePend,
    .semaphorePost     = template_osalPosixSemaphorePost,
    .semaphoreCountGet = template_osalPosixSemaphoreCountGet,
// END SEMAPHORE

// BEGIN EVENT_FLAGS
    /*-------------------------------- Event flags ------------------------------*/
    .eventFlagsCreate = template_osalPosixEventFlagsCreate,
    .eventFlagsDelete = template_osalPosixEventFlagsDelete,
    .eventFlagsSet    = template_osalPosixEventFlagsSet,
    .eventFlagsClear  = template_osalPosixEventFlagsClear,
    .eventFlagsGet    = template_osalPosixEventFlagsGet,
    .eventFlagsWait   = template_osalPosixEventFlagsWait,
// END EVENT_FLAGS

// BEGIN THREAD
    /*-------------------------------- Threads --------------------------------*/
    .threadCreate     = template_osalPosixThreadCreate,
    .threadDelete     = template_osalPosixThreadDelete,
    .threadSuspend    = template_osalPosixThreadSuspend,
    .threadResume     = template_osalPosixThreadResume,
    .threadDelay      = template_osalPosixThreadDelay,
    .threadDelayUntil = template_osalPosixThreadDelayUntil,
    .threadExit       = template_osalPosixThreadExit,
// END THREAD

// BEGIN CRITICAL_SECTION
    .criticalSectionEnter = template_osalPosixCriticalSectionEnter,
    .criticalSectionExit  = template_osalPosixCriticalSectionExit,
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
    .softwareTimerCreate = template_osalPosixSoftwareTimerCreate,
    .softwareTimerDelete = template_osalPosixSoftwareTimerDelete,
    .softwareTimerStart  = template_osalPosixSoftwareTimerStart,
    .softwareTimerStop   = template_osalPosixSoftwareTimerStop,
    .softwareTimerReset  = template_osalPosixSoftwareTimerReset,
// END SOFTWARE_TIMER

// BEGIN TIME
    /*--------------------------------- Time ----------------------------------*/
    .timeMsGet = template_osalPosixTimeMsGet,
// END TIME

// BEGIN MEMORY
    /*-------------------------------- Memory ---------------------------------*/
    .memAlloc = template_osalPosixMemAlloc,
    .memFree  = template_osalPosixMemFree,
// END MEMORY

    /*------------------------------- Predicate -------------------------------*/
    .isValid = template_osalPosixIsValid
};

//=======================================================================[ PUBLIC INTERFACE FUNCTIONS ]===============================================================================

/**
 * \brief Initialize the Template POSIX OSAL instance.
 *
 * \details Initializes the generic OSAL base object, initializes the backend-owned
 *          resource mutex and binds the POSIX vtable. The resource mutex is native
 *          backend state and does not consume a generic OSAL mutex registry slot.
 *
 * \param osalPosix  Pointer to the POSIX-specific OSAL instance.
 * \param name       Optional instance name. May be NULL.
 * \param parent     Optional parent object pointer. May be NULL.
 * \param param      Optional POSIX instance parameters. NULL selects the default policy.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalPosixInit(Template_osalPosix_s *const osalPosix,
                                          const char *const name,
                                          void *const parent,
                                          const Template_osalPosixParam_s *const param)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit(%p, %s, %p, %p)",
                              (void *)osalPosix,
                              (name != NULL) ? name : "(null)",
                              parent,
                              (const void *)param);

    /* Validate args */
    if (osalPosix == NULL)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid args
    }

    /* Initialize the generic OSAL base */
    osalStatus = template_osalInit(&osalPosix->base, name, parent);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: base initialization failed
    }

    /* Reset POSIX-specific instance state */
    osalPosix->param = (Template_osalPosixParam_s) {
        0
    };
    osalPosix->validFlag = false;

    if (param != NULL)
    {
        osalPosix->param = *param;
    }

    /* Create the backend-owned registry synchronization mutex */
    if (pthread_mutex_init(&osalPosix->resourceMutex, NULL) != 0)
    {
        (void)template_osalDeinit(&osalPosix->base);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex creation failed
    }

    /* Bind the POSIX backend vtable */
    osalPosix->base.vtable = &template_osalPosixVtable;

    /* Mark the POSIX backend as valid */
    osalPosix->validFlag = true;

    /* Trace initialization success */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: POSIX OSAL was initialized
}


/**
 * \brief Deinitialize the Template POSIX OSAL instance.
 *
 * \details Releases all registered Milestone-1 resources on a best-effort basis,
 *          destroys the backend-owned resource mutex and deinitializes the generic
 *          OSAL base object.
 *
 * \param osalPosix  Pointer to the POSIX-specific OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalPosixDeinit(Template_osalPosix_s *const osalPosix)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit(%p)", (void *)osalPosix);

    /* Validate args */
    if (osalPosix == NULL)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid args
    }

    /* Validate backend state */
    if (!template_osalPosixIsValid(osalPosix))
    {
        osalStatus = TEMPLATE_OSAL_NOT_INIT_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: backend is not initialized
    }

// BEGIN THREAD
    /* Delete registered threads */
    for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.threadObjHandle[i].handle != NULL)
        {
            (void)template_osalPosixThreadDelete(osalPosix,
                                                 osalPosix->base.threadObjHandle[i].handle);
        }
    }
// END THREAD

// BEGIN QUEUE
    /* Delete registered queues */
    for (size_t i = 0u; i < TEMPLATE_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.queueObjHandle[i] != NULL)
        {
            (void)template_osalPosixQueueDelete(osalPosix,
                                                osalPosix->base.queueObjHandle[i]);
        }
    }
// END QUEUE

// BEGIN MUTEX
    /* Delete registered mutexes */
    for (size_t i = 0u; i < TEMPLATE_OSAL_MUTEX_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.mutexHandle[i] != NULL)
        {
            (void)template_osalPosixMutexDelete(osalPosix,
                                                osalPosix->base.mutexHandle[i]);
        }
    }
// END MUTEX

// BEGIN SEMAPHORE
    /* Delete registered counting semaphores */
    for (size_t i = 0u; i < TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.semaphoreObjHandle[i] != NULL)
        {
            (void)template_osalPosixSemaphoreDelete(osalPosix,
                                                    osalPosix->base.semaphoreObjHandle[i]);
        }
    }
// END SEMAPHORE

// BEGIN MEMORY
    /* Free registered memory blocks */
    for (size_t i = 0u; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.memPtr[i] != NULL)
        {
            (void)template_osalPosixMemFree(osalPosix,
                                            osalPosix->base.memPtr[i]);
        }
    }
// END MEMORY

    /* Clear the POSIX-specific state */
    osalPosix->validFlag   = false;
    osalPosix->base.vtable = NULL;
    osalPosix->param       = (Template_osalPosixParam_s) {
        0
    };

    if (pthread_mutex_destroy(&osalPosix->resourceMutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex destruction failed
    }

    /* Deinitialize the generic OSAL base */
    osalStatus = template_osalDeinit(&osalPosix->base);

    /* Trace the deinitialization result */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: POSIX OSAL was deinitialized
}

//============================================================================[ PRIVATE FUNCTIONS ]==================================================================================

// BEGIN QUEUE
/*-------------------------------- Queues ---------------------------------*/

/**
 * \brief Create a bounded POSIX queue and register it in the OSAL instance.
 *
 * \details The queue is implemented as a private ring buffer protected by a
 *          pthread mutex. freeSlotsSmphr and busySlotsSmphr are native POSIX
 *          semaphores owned by the queue control block; they are not generic
 *          OSAL semaphore objects and do not consume semaphore registry slots.
 *
 * \param osal           Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueItemSize  Size of one queue item in bytes.
 * \param queueDepth     Maximum number of queue items.
 * \param queueHandle    Output pointer receiving the queue handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueCreate(void *const osal,
                                                        const size_t queueItemSize,
                                                        const size_t queueDepth,
                                                        Template_osalQueueHandle_t *const queueHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate(%p, %lu, %lu, %p)",
                              osal,
                              (unsigned long)queueItemSize,
                              (unsigned long)queueDepth,
                              (void *)queueHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueItemSize != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(queueDepth != 0u);

    /* Downcast the generic OSAL instance to the POSIX-specific type */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueFreeSlotFind != NULL);

    /* Defensively reject values that cannot be represented by POSIX semaphores or storage size */
    if ((queueItemSize == 0u) ||
        (queueDepth == 0u) ||
        (queueItemSize > (SIZE_MAX / queueDepth)) ||
        (queueDepth > (size_t)SEM_VALUE_MAX))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue dimensions are not representable by the POSIX backend
    }

    /* Clear the output value */
    *queueHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free queue registry slot */
    const size_t queueId = port->base.ptable->queueFreeSlotFind(port);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free queue slot
    }

    /* Allocate the queue control block */
    Template_osalPosixQueue_s *const queue =
        (Template_osalPosixQueue_s *)calloc(1u, sizeof(Template_osalPosixQueue_s));
    if (queue == NULL)
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_MEM_ALLOCATION_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue control-block allocation failed
    }

    /* Allocate ring-buffer storage */
    queue->buffer = calloc(queueDepth, queueItemSize);
    if (queue->buffer == NULL)
    {
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_MEM_ALLOCATION_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue storage allocation failed
    }

    /* Initialize the queue control block */
    queue->itemSize  = queueItemSize;
    queue->depth     = queueDepth;
    queue->readIdx   = 0u;
    queue->writeIdx  = 0u;
    queue->itemCount = 0u;

    /* Initialize native queue synchronization primitives */
    if (pthread_mutex_init(&queue->mutex, NULL) != 0)
    {
        free(queue->buffer);
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex initialization failed
    }

    if (sem_init(&queue->freeSlotsSmphr, 0, (unsigned int)queueDepth) != 0)
    {
        (void)pthread_mutex_destroy(&queue->mutex);
        free(queue->buffer);
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore initialization failed
    }

    if (sem_init(&queue->busySlotsSmphr, 0, 0u) != 0)
    {
        (void)sem_destroy(&queue->freeSlotsSmphr);
        (void)pthread_mutex_destroy(&queue->mutex);
        free(queue->buffer);
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: occupied-slot semaphore initialization failed
    }

    /* Register the queue handle */
    port->base.queueObjHandle[queueId - 1u] = (Template_osalQueueHandle_t)queue;
    *queueHandle                            = (Template_osalQueueHandle_t)queue;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue was created and registered
}


/**
 * \brief Delete a registered POSIX queue.
 *
 * \note The caller must ensure no producer or consumer is blocked on or using
 *       the queue while it is deleted.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueHandle  Registered queue handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueDelete(void *const osal,
                                                        const Template_osalQueueHandle_t queueHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete(%p, %p)", osal, (void *)queueHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the queue handle in the registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Destroy backend-private synchronization resources */
    const int busyRc  = sem_destroy(&queue->busySlotsSmphr);
    const int freeRc  = sem_destroy(&queue->freeSlotsSmphr);
    const int mutexRc = pthread_mutex_destroy(&queue->mutex);
    if ((busyRc != 0) ||
        (freeRc != 0) ||
        (mutexRc != 0))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native queue synchronization teardown failed
    }

    /* Release queue storage and clear the registry slot */
    free(queue->buffer);
    free(queue);
    port->base.queueObjHandle[queueId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue was deleted and unregistered
}


/**
 * \brief Put an item into a registered POSIX queue without waiting for capacity.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Pointer to the item to enqueue.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueItemPut(void *const osal,
                                                         const Template_osalQueueHandle_t queueHandle,
                                                         const void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut(%p, %p, %p)",
                              osal, (void *)queueHandle, queueItemPtr);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueItemPtr != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Reserve one free queue slot without waiting */
    if (template_osalPosixSemaphorePendNative(&queue->freeSlotsSmphr, 0u) != 0)
    {
        if (errno == EAGAIN)
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_IS_FULL_ERR;
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        }

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue has no immediately available free slot
    }

    /* Commit the item to the ring buffer */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        (void)sem_post(&queue->freeSlotsSmphr);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    void *const dst = (uint8_t *)queue->buffer + (queue->writeIdx * queue->itemSize);
    memcpy(dst, queueItemPtr, queue->itemSize);
    queue->writeIdx = (queue->writeIdx + 1u) % queue->depth;
    ++queue->itemCount;

    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Publish one occupied queue slot */
    if (sem_post(&queue->busySlotsSmphr) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: occupied-slot semaphore update failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was put
}


/**
 * \brief Post an item to a registered POSIX queue using the requested timeout.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Pointer to the item to enqueue.
 * \param timeoutMs     Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueItemPost(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle,
                                                          const void *const queueItemPtr,
                                                          const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost(%p, %p, %p, %u)",
                              osal, (void *)queueHandle, queueItemPtr, (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueItemPtr != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Reserve one free queue slot using the requested timeout */
    if (template_osalPosixSemaphorePendNative(&queue->freeSlotsSmphr, timeoutMs) != 0)
    {
        if ((errno == EAGAIN) ||
            (errno == ETIMEDOUT))
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_OVERFLOW_ERR;
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        }

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue post timed out or native wait failed
    }

    /* Commit the item to the ring buffer */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        (void)sem_post(&queue->freeSlotsSmphr);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    void *const dst = (uint8_t *)queue->buffer + (queue->writeIdx * queue->itemSize);
    memcpy(dst, queueItemPtr, queue->itemSize);
    queue->writeIdx = (queue->writeIdx + 1u) % queue->depth;
    ++queue->itemCount;

    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Publish one occupied queue slot */
    if (sem_post(&queue->busySlotsSmphr) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: occupied-slot semaphore update failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was posted
}


/**
 * \brief Retrieve an already available item from a registered POSIX queue without waiting.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Destination buffer for the item.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueItemGet(void *const osal,
                                                         const Template_osalQueueHandle_t queueHandle,
                                                         void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet(%p, %p, %p)",
                              osal, (void *)queueHandle, queueItemPtr);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueItemPtr != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Reserve one occupied queue slot without waiting */
    if (template_osalPosixSemaphorePendNative(&queue->busySlotsSmphr, 0u) != 0)
    {
        if (errno == EAGAIN)
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR;
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        }

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue has no immediately available item
    }

    /* Retrieve the item from the ring buffer */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        (void)sem_post(&queue->busySlotsSmphr);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    void *const src = (uint8_t *)queue->buffer + (queue->readIdx * queue->itemSize);
    memcpy(queueItemPtr, src, queue->itemSize);
    queue->readIdx = (queue->readIdx + 1u) % queue->depth;
    --queue->itemCount;

    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Return one slot to the producer side */
    if (sem_post(&queue->freeSlotsSmphr) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore update failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was retrieved
}


/**
 * \brief Wait indefinitely for an item from a registered POSIX queue.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Destination buffer for the item.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueItemWait(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle,
                                                          void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait(%p, %p, %p)",
                              osal, (void *)queueHandle, queueItemPtr);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueItemPtr != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Wait indefinitely for one occupied queue slot */
    if (template_osalPosixSemaphorePendNative(&queue->busySlotsSmphr,
                                              TEMPLATE_OSAL_INFINITY_TOUT) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native infinite queue wait failed
    }

    /* Retrieve the item from the ring buffer */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        (void)sem_post(&queue->busySlotsSmphr);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    void *const src = (uint8_t *)queue->buffer + (queue->readIdx * queue->itemSize);
    memcpy(queueItemPtr, src, queue->itemSize);
    queue->readIdx = (queue->readIdx + 1u) % queue->depth;
    --queue->itemCount;

    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    if (sem_post(&queue->freeSlotsSmphr) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore update failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was retrieved
}


/**
 * \brief Pend an item from a registered POSIX queue using the requested timeout.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueHandle   Registered queue handle.
 * \param queueItemPtr  Destination buffer for the item.
 * \param timeoutMs     Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueItemPend(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle,
                                                          void *const queueItemPtr,
                                                          const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend(%p, %p, %p, %u)",
                              osal, (void *)queueHandle, queueItemPtr, (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueItemPtr != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Wait for one occupied queue slot using the requested timeout */
    if (template_osalPosixSemaphorePendNative(&queue->busySlotsSmphr, timeoutMs) != 0)
    {
        if ((errno == EAGAIN) ||
            (errno == ETIMEDOUT))
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR;
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        }

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue receive timed out or native wait failed
    }

    /* Retrieve the item from the ring buffer */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        (void)sem_post(&queue->busySlotsSmphr);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    void *const src = (uint8_t *)queue->buffer + (queue->readIdx * queue->itemSize);
    memcpy(queueItemPtr, src, queue->itemSize);
    queue->readIdx = (queue->readIdx + 1u) % queue->depth;
    --queue->itemCount;

    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    if (sem_post(&queue->freeSlotsSmphr) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore update failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was retrieved
}


/**
 * \brief Reset a registered POSIX queue to its initial empty state.
 *
 * \details Resets ring-buffer indices, discards queued data, drains both native
 *          slot semaphores and restores freeSlotsSmphr to queue depth.
 *
 * \note The caller must ensure no producer or consumer is blocked on or using
 *       the queue while reset is performed. This matches the resource-lifecycle
 *       expectation for native synchronization objects in this backend.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param queueHandle  Registered queue handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixQueueReset(void *const osal,
                                                       const Template_osalQueueHandle_t queueHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset(%p, %p)", osal, (void *)queueHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Protect ring-buffer state while it is reset */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    queue->readIdx   = 0u;
    queue->writeIdx  = 0u;
    queue->itemCount = 0u;
    memset(queue->buffer, 0, queue->itemSize * queue->depth);

    /* Drain the occupied-slot semaphore */
    int nativeStatus = 0;
    do
    {
        nativeStatus = sem_trywait(&queue->busySlotsSmphr);
    }
    while ((nativeStatus == 0) ||
           ((nativeStatus != 0) && (errno == EINTR)));

    if (errno != EAGAIN)
    {
        (void)pthread_mutex_unlock(&queue->mutex);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: occupied-slot semaphore reset failed
    }

    /* Drain and restore the free-slot semaphore to queue depth */
    do
    {
        nativeStatus = sem_trywait(&queue->freeSlotsSmphr);
    }
    while ((nativeStatus == 0) ||
           ((nativeStatus != 0) && (errno == EINTR)));

    if (errno != EAGAIN)
    {
        (void)pthread_mutex_unlock(&queue->mutex);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore drain failed
    }

    for (size_t i = 0u; i < queue->depth; ++i)
    {
        if (sem_post(&queue->freeSlotsSmphr) != 0)
        {
            (void)pthread_mutex_unlock(&queue->mutex);
            TEMPLATE_OSAL_POSIX_ASSERT(0);
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: free-slot semaphore restore failed
        }
    }

    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue was reset
}

// END QUEUE

// BEGIN STREAM_BUFFER
/*----------------------------- Stream buffers -----------------------------*/

#error "KIWI POSIX backend Milestone 1: stream-buffer API is not implemented"

/**
 * \brief Milestone-1 POSIX stream-buffer creation stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferCreate(void *const osal,
                                                               const size_t bufferSizeBytes,
                                                               const size_t triggerLevelBytes,
                                                               Template_osalStreamBufferHandle_t *const streamBufferHandle)
{
    (void)osal;
    (void)bufferSizeBytes;
    (void)triggerLevelBytes;
    (void)streamBufferHandle;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX stream-buffer deletion stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferDelete(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    (void)osal;
    (void)streamBufferHandle;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX stream-buffer immediate-write stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferPut(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            const void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesPut)
{
    (void)osal;
    (void)streamBufferHandle;
    (void)data;
    (void)dataLengthBytes;
    (void)bytesPut;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX stream-buffer timed-write stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferPost(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             const void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesPut)
{
    (void)osal;
    (void)streamBufferHandle;
    (void)data;
    (void)dataLengthBytes;
    (void)timeoutMs;
    (void)bytesPut;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX stream-buffer immediate-read stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferGet(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesGet)
{
    (void)osal;
    (void)streamBufferHandle;
    (void)data;
    (void)dataLengthBytes;
    (void)bytesGet;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX stream-buffer infinite-read stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferWait(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             size_t *const bytesGet)
{
    (void)osal;
    (void)streamBufferHandle;
    (void)data;
    (void)dataLengthBytes;
    (void)bytesGet;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX stream-buffer timed-read stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferPend(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesGet)
{
    (void)osal;
    (void)streamBufferHandle;
    (void)data;
    (void)dataLengthBytes;
    (void)timeoutMs;
    (void)bytesGet;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX stream-buffer reset stub.
 */
static Template_osalErr_e template_osalPosixStreamBufferReset(void *const osal,
                                                              const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    (void)osal;
    (void)streamBufferHandle;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX stream buffers are not implemented in Milestone 1
}

// END STREAM_BUFFER

// BEGIN MUTEX
/*-------------------------------- Mutexes ----------------------------------*/

/**
 * \brief Create a recursive POSIX mutex and register it in the OSAL instance.
 *
 * \details The generic KIWI OSAL mutex contract requires every public mutex to
 *          be recursive/reentrant. The POSIX backend therefore creates each
 *          native mutex with PTHREAD_MUTEX_RECURSIVE semantics.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param mutexHandle  Output pointer receiving the mutex handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMutexCreate(void *const osal,
                                                        Template_osalMutexHandle_t *const mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate(%p, %p)", osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(mutexHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexFreeSlotFind != NULL);

    /* Clear the output value */
    *mutexHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free mutex registry slot */
    const size_t mutexId = port->base.ptable->mutexFreeSlotFind(port);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free mutex slot
    }

    /* Allocate the mutex control block */
    Template_osalPosixMutex_s *const mutex =
        (Template_osalPosixMutex_s *)calloc(1u, sizeof(Template_osalPosixMutex_s));
    if (mutex == NULL)
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_MEM_ALLOCATION_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex control-block allocation failed
    }

    /* Configure native recursive-mutex attributes */
    pthread_mutexattr_t attr;
    int nativeStatus = pthread_mutexattr_init(&attr);
    if (nativeStatus != 0)
    {
        free(mutex);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native mutex attributes initialization failed
    }

    nativeStatus = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (nativeStatus != 0)
    {
        (void)pthread_mutexattr_destroy(&attr);
        free(mutex);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: recursive mutex attribute configuration failed
    }

    nativeStatus = pthread_mutex_init(&mutex->mutex, &attr);
    (void)pthread_mutexattr_destroy(&attr);
    if (nativeStatus != 0)
    {
        free(mutex);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: recursive mutex initialization failed
    }

    /* Register the mutex handle */
    port->base.mutexHandle[mutexId - 1u] = (Template_osalMutexHandle_t)mutex;
    *mutexHandle                         = (Template_osalMutexHandle_t)mutex;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: recursive mutex was created and registered
}


/**
 * \brief Delete a registered recursive POSIX mutex.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param mutexHandle  Registered mutex handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMutexDelete(void *const osal,
                                                        const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete(%p, %p)", osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(mutexHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the mutex handle in the registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    /* Destroy the native recursive mutex */
    if (pthread_mutex_destroy(&mutex->mutex) != 0)
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native mutex destruction failed
    }

    free(mutex);
    port->base.mutexHandle[mutexId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was deleted and unregistered
}


/**
 * \brief Lock a registered recursive POSIX mutex indefinitely.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param mutexHandle  Registered mutex handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMutexLock(void *const osal,
                                                      const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexLock(%p, %p)", osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(mutexHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    if (pthread_mutex_lock(&mutex->mutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native recursive mutex operation failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was locked
}


/**
 * \brief Try to lock a registered recursive POSIX mutex without waiting.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param mutexHandle  Registered mutex handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMutexTryLock(void *const osal,
                                                         const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexTryLock(%p, %p)", osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(mutexHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexTryLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;
    const int nativeStatus = pthread_mutex_trylock(&mutex->mutex);
    if (nativeStatus != 0)
    {
        if (nativeStatus == EBUSY)
        {
            osalStatus = TEMPLATE_OSAL_MUTEX_LOCK_ERR;
        }
        else
        {
            TEMPLATE_OSAL_POSIX_ASSERT(0);
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        }

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexTryLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex is not immediately available or native lock failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexTryLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was locked
}


/**
 * \brief Lock a registered recursive POSIX mutex using the requested timeout.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param mutexHandle  Registered mutex handle.
 * \param timeoutMs    Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMutexPendLock(void *const osal,
                                                          const Template_osalMutexHandle_t mutexHandle,
                                                          const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock(%p, %p, %u)",
                              osal, (void *)mutexHandle, (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(mutexHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;
    int nativeStatus = 0;

    if (timeoutMs == TEMPLATE_OSAL_INFINITY_TOUT)
    {
        nativeStatus = pthread_mutex_lock(&mutex->mutex);
    }
    else if (timeoutMs == 0u)
    {
        nativeStatus = pthread_mutex_trylock(&mutex->mutex);
    }
    else
    {
        struct timespec deadline;
        if (!template_osalPosixRealtimeDeadlineGet(timeoutMs, &deadline))
        {
            TEMPLATE_OSAL_POSIX_ASSERT(0);
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: timed-lock deadline creation failed
        }

        nativeStatus = pthread_mutex_timedlock(&mutex->mutex, &deadline);
    }

    if (nativeStatus != 0)
    {
        if ((nativeStatus == EBUSY) ||
            (nativeStatus == ETIMEDOUT))
        {
            osalStatus = TEMPLATE_OSAL_MUTEX_LOCK_ERR;
        }
        else
        {
            TEMPLATE_OSAL_POSIX_ASSERT(0);
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        }

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex lock timed out or native lock failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was locked
}


/**
 * \brief Unlock a registered recursive POSIX mutex.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param mutexHandle  Registered mutex handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMutexUnlock(void *const osal,
                                                        const Template_osalMutexHandle_t mutexHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexUnlock(%p, %p)", osal, (void *)mutexHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(mutexHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    if (pthread_mutex_unlock(&mutex->mutex) != 0)
    {
        osalStatus = TEMPLATE_OSAL_MUTEX_UNLOCK_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native recursive mutex unlock failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexUnlock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was unlocked
}

// END MUTEX

// BEGIN SEMAPHORE
/*--------------------------- Counting semaphores --------------------------*/

/**
 * \brief Create a bounded POSIX counting semaphore and register it in the OSAL instance.
 *
 * \details The implementation uses two backend-private native semaphores. The
 *          available-count semaphore represents consumable counts while the
 *          free-count semaphore enforces the configured maxCount exactly.
 *
 * \param osal             Opaque pointer to the initialized POSIX OSAL instance.
 * \param maxCount         Maximum semaphore count.
 * \param initialCount     Initial semaphore count.
 * \param semaphoreHandle  Output pointer receiving the semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSemaphoreCreate(void *const osal,
                                                            const Template_osalSemaphoreCount_t maxCount,
                                                            const Template_osalSemaphoreCount_t initialCount,
                                                            Template_osalSemaphoreHandle_t *const semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate(%p, %u, %u, %p)",
                              osal,
                              (unsigned int)maxCount,
                              (unsigned int)initialCount,
                              (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(semaphoreHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(maxCount != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(initialCount <= maxCount);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreFreeSlotFind != NULL);

    /* Defensively reject counts that cannot be represented by native semaphores */
    if ((maxCount == 0u) ||
        (initialCount > maxCount) ||
        (maxCount > (Template_osalSemaphoreCount_t)SEM_VALUE_MAX))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore count range is invalid for the POSIX backend
    }

    /* Clear the output value */
    *semaphoreHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free semaphore registry slot */
    const size_t semaphoreId = port->base.ptable->semaphoreFreeSlotFind(port);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free counting-semaphore slot
    }

    /* Allocate the counting-semaphore control block */
    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)calloc(1u, sizeof(Template_osalPosixSemaphore_s));
    if (semaphore == NULL)
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_MEM_ALLOCATION_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore control-block allocation failed
    }

    /* Initialize the native available-count semaphore */
    if (sem_init(&semaphore->availableCountSmphr, 0, (unsigned int)initialCount) != 0)
    {
        free(semaphore);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: available-count semaphore initialization failed
    }

    /* Initialize the native capacity semaphore */
    const Template_osalSemaphoreCount_t freeCount = maxCount - initialCount;
    if (sem_init(&semaphore->freeCountSmphr, 0, (unsigned int)freeCount) != 0)
    {
        (void)sem_destroy(&semaphore->availableCountSmphr);
        free(semaphore);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: capacity semaphore initialization failed
    }

    semaphore->maxCount = maxCount;

    /* Register the counting-semaphore handle */
    port->base.semaphoreObjHandle[semaphoreId - 1u] = (Template_osalSemaphoreHandle_t)semaphore;
    *semaphoreHandle                                  = (Template_osalSemaphoreHandle_t)semaphore;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: counting semaphore was created and registered
}


/**
 * \brief Delete a registered POSIX counting semaphore.
 *
 * \note The caller must ensure no thread is blocked on the semaphore while it is deleted.
 *
 * \param osal             Opaque pointer to the initialized POSIX OSAL instance.
 * \param semaphoreHandle  Registered counting-semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSemaphoreDelete(void *const osal,
                                                            const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete(%p, %p)",
                              osal, (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(semaphoreHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the semaphore handle in the registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Destroy backend-private native semaphores */
    const int availableRc = sem_destroy(&semaphore->availableCountSmphr);
    const int freeRc      = sem_destroy(&semaphore->freeCountSmphr);
    if ((availableRc != 0) ||
        (freeRc != 0))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native counting-semaphore teardown failed
    }

    free(semaphore);
    port->base.semaphoreObjHandle[semaphoreId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: counting semaphore was deleted and unregistered
}


/**
 * \brief Wait indefinitely for one count from a registered POSIX counting semaphore.
 *
 * \param osal             Opaque pointer to the initialized POSIX OSAL instance.
 * \param semaphoreHandle  Registered counting-semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSemaphoreWait(void *const osal,
                                                          const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait(%p, %p)",
                              osal, (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(semaphoreHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Consume one available count */
    if (template_osalPosixSemaphorePendNative(&semaphore->availableCountSmphr,
                                              TEMPLATE_OSAL_INFINITY_TOUT) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_WAIT_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native counting-semaphore wait failed
    }

    /* Return one unit of configured capacity */
    if (sem_post(&semaphore->freeCountSmphr) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore capacity update failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: one semaphore count was consumed
}


/**
 * \brief Wait for one count from a registered POSIX counting semaphore using the requested timeout.
 *
 * \param osal             Opaque pointer to the initialized POSIX OSAL instance.
 * \param semaphoreHandle  Registered counting-semaphore handle.
 * \param timeoutMs        Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSemaphorePend(void *const osal,
                                                          const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                          const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend(%p, %p, %u)",
                              osal, (void *)semaphoreHandle, (unsigned int)timeoutMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(semaphoreHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Consume one available count using the requested timeout */
    if (template_osalPosixSemaphorePendNative(&semaphore->availableCountSmphr, timeoutMs) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_WAIT_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore wait timed out or native wait failed
    }

    /* Return one unit of configured capacity */
    if (sem_post(&semaphore->freeCountSmphr) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore capacity update failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: one semaphore count was consumed
}


/**
 * \brief Post one count to a registered POSIX counting semaphore.
 *
 * \param osal             Opaque pointer to the initialized POSIX OSAL instance.
 * \param semaphoreHandle  Registered counting-semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSemaphorePost(void *const osal,
                                                          const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost(%p, %p)",
                              osal, (void *)semaphoreHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(semaphoreHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Reserve one unit of configured capacity without waiting */
    if (template_osalPosixSemaphorePendNative(&semaphore->freeCountSmphr, 0u) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_POST_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting semaphore is already at maxCount
    }

    /* Publish one available count */
    if (sem_post(&semaphore->availableCountSmphr) != 0)
    {
        (void)sem_post(&semaphore->freeCountSmphr);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_POST_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native counting-semaphore post failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: one semaphore count was posted
}


/**
 * \brief Retrieve the current count of a registered POSIX counting semaphore.
 *
 * \param osal             Opaque pointer to the initialized POSIX OSAL instance.
 * \param semaphoreHandle  Registered counting-semaphore handle.
 * \param semaphoreCount   Output pointer receiving the current available count.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSemaphoreCountGet(void *const osal,
                                                              const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                              Template_osalSemaphoreCount_t *const semaphoreCount)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet(%p, %p, %p)",
                              osal, (void *)semaphoreHandle, (void *)semaphoreCount);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(semaphoreHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(semaphoreCount != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    int nativeCount = 0;
    if (sem_getvalue(&semaphore->availableCountSmphr, &nativeCount) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native semaphore count query failed
    }

    if (nativeCount < 0)
    {
        nativeCount = 0;
    }

    *semaphoreCount = (Template_osalSemaphoreCount_t)nativeCount;

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet: count = %u",
                              (unsigned int)*semaphoreCount);
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore count was returned
}

// END SEMAPHORE

// BEGIN EVENT_FLAGS
/*-------------------------------- Event flags ------------------------------*/

#error "KIWI POSIX backend Milestone 1: event-flags API is not implemented"

/**
 * \brief Milestone-1 POSIX event-flags creation stub.
 */
static Template_osalErr_e template_osalPosixEventFlagsCreate(void *const osal,
                                                             Template_osalEventFlagsHandle_t *const eventFlagsHandle)
{
    (void)osal;
    (void)eventFlagsHandle;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX event flags are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX event-flags deletion stub.
 */
static Template_osalErr_e template_osalPosixEventFlagsDelete(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle)
{
    (void)osal;
    (void)eventFlagsHandle;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX event flags are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX event-flags set stub.
 */
static Template_osalErr_e template_osalPosixEventFlagsSet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          const uint32_t flags)
{
    (void)osal;
    (void)eventFlagsHandle;
    (void)flags;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX event flags are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX event-flags clear stub.
 */
static Template_osalErr_e template_osalPosixEventFlagsClear(void *const osal,
                                                            const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                            const uint32_t flags)
{
    (void)osal;
    (void)eventFlagsHandle;
    (void)flags;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX event flags are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX event-flags get stub.
 */
static Template_osalErr_e template_osalPosixEventFlagsGet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          uint32_t *const flags)
{
    (void)osal;
    (void)eventFlagsHandle;
    (void)flags;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX event flags are not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX event-flags wait stub.
 */
static Template_osalErr_e template_osalPosixEventFlagsWait(void *const osal,
                                                           const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                           const uint32_t flags,
                                                           const Template_osalEventFlagsOptions_e options,
                                                           const Template_osalTimeMs_t timeoutMs,
                                                           uint32_t *const actualFlags)
{
    (void)osal;
    (void)eventFlagsHandle;
    (void)flags;
    (void)options;
    (void)timeoutMs;
    (void)actualFlags;
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX event flags are not implemented in Milestone 1
}

// END EVENT_FLAGS

// BEGIN THREAD
/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a POSIX thread and register it in the OSAL instance.
 *
 * \details The OSAL worker is launched through a pthread-compatible thunk. The
 *          requested stack size is applied through pthread attributes. Generic
 *          OSAL priority levels are mapped to the active native scheduler on a
 *          best-effort basis; lack of host privileges to change priority does not
 *          invalidate an otherwise successful thread creation.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param threadHandle  Output pointer receiving the thread handle.
 * \param threadAttr    Thread attributes.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixThreadCreate(void *const osal,
                                                         Template_osalThreadHandle_t *const threadHandle,
                                                         Template_osalThreadAttr_s threadAttr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate(%p, %p, {%p, %s, %lu, %p, %d})",
                              osal,
                              (void *)threadHandle,
                              (void *)(uintptr_t)threadAttr.worker,
                              (threadAttr.name != NULL) ? threadAttr.name : "(null)",
                              (unsigned long)threadAttr.stackSize,
                              threadAttr.args,
                              (int)threadAttr.prio);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(threadHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadFreeSlotFind != NULL);

    if (!template_osalPosixThreadAttrValidate(&threadAttr))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid thread attributes
    }

    /* Clear the output value */
    *threadHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free thread registry slot */
    const size_t threadId = port->base.ptable->threadFreeSlotFind(port);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free thread slot
    }

    const size_t threadIdx = threadId - 1u;

    /* Allocate the POSIX thread control block */
    Template_osalPosixThread_s *const thread =
        (Template_osalPosixThread_s *)calloc(1u, sizeof(Template_osalPosixThread_s));
    if (thread == NULL)
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_MEM_ALLOCATION_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread control-block allocation failed
    }

    pthread_attr_t nativeAttr;
    if (pthread_attr_init(&nativeAttr) != 0)
    {
        free(thread);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread attributes initialization failed
    }

    /* POSIX may enforce a host-specific minimum stack larger than an embedded profile requests. */
    size_t nativeStackSize = threadAttr.stackSize;
    const long minimumStackSize = sysconf(_SC_THREAD_STACK_MIN);
    if ((minimumStackSize > 0L) &&
        (nativeStackSize < (size_t)minimumStackSize))
    {
        nativeStackSize = (size_t)minimumStackSize;
    }

    if (pthread_attr_setstacksize(&nativeAttr, nativeStackSize) != 0)
    {
        (void)pthread_attr_destroy(&nativeAttr);
        free(thread);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: requested pthread stack size is not supported
    }

    /* Prepare the embedded pthread thunk arguments */
    thread->arg.worker     = threadAttr.worker;
    thread->arg.workerArgs = threadAttr.args;

    /* Register the control block before starting the worker */
    port->base.threadObjHandle[threadIdx].attr   = threadAttr;
    port->base.threadObjHandle[threadIdx].handle = (Template_osalThreadHandle_t)thread;

    /* Create the native POSIX thread */
    const int nativeStatus = pthread_create(&thread->thread,
                                            &nativeAttr,
                                            template_osalPosixThreadThunk,
                                            (void *)&thread->arg);
    (void)pthread_attr_destroy(&nativeAttr);

    if (nativeStatus != 0)
    {
        port->base.ptable->threadSlotClear(port, threadIdx);
        free(thread);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_CREATE_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread creation failed
    }

    *threadHandle = (Template_osalThreadHandle_t)thread;

    /* Release the resource mutex before optional scheduler tuning */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Apply the generic priority on a best-effort basis */
    int policy = SCHED_OTHER;
    struct sched_param schedParam = {
        0
    };
    if (pthread_getschedparam(thread->thread, &policy, &schedParam) == 0)
    {
        const int prioMin = sched_get_priority_min(policy);
        const int prioMax = sched_get_priority_max(policy);

        if ((prioMin >= 0) &&
            (prioMax >= prioMin))
        {
            const int prioSpan = prioMax - prioMin;

            switch (threadAttr.prio)
            {
                case TEMPLATE_OSAL_THREAD_PRIO_LOW:
                {
                    schedParam.sched_priority = prioMin;
                    break;
                }

                case TEMPLATE_OSAL_THREAD_PRIO_NORMAL:
                {
                    schedParam.sched_priority = prioMin + (prioSpan / 3);
                    break;
                }

                case TEMPLATE_OSAL_THREAD_PRIO_HIGH:
                {
                    schedParam.sched_priority = prioMin + ((2 * prioSpan) / 3);
                    break;
                }

                case TEMPLATE_OSAL_THREAD_PRIO_CRITICAL:
                {
                    schedParam.sched_priority = prioMax;
                    break;
                }

                default:
                {
                    TEMPLATE_OSAL_POSIX_ASSERT(0);
                    break;
                }
            }

            const int schedStatus = pthread_setschedparam(thread->thread, policy, &schedParam);
            if (schedStatus != 0)
            {
                TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate: priority mapping skipped (%d)",
                                          schedStatus);
            }
        }
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was created and registered
}


/**
 * \brief Delete a registered POSIX thread synchronously.
 *
 * \details Requests pthread cancellation, joins the native thread and releases
 *          the backend control block before clearing the OSAL registry slot.
 *
 * \note The component should arrange for the thread operation to be stopped or
 *       cancellation-safe before invoking Delete, as required by the generic contract.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param threadHandle  Registered thread handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixThreadDelete(void *const osal,
                                                         const Template_osalThreadHandle_t threadHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete(%p, %p)", osal, (void *)threadHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(threadHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadSlotClear != NULL);

    /* Locate the registered thread */
    const size_t threadId = port->base.ptable->threadHandleFind(port, threadHandle);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread handle is not registered
    }

    Template_osalPosixThread_s *const thread = (Template_osalPosixThread_s *)threadHandle;

    /* Reject self-delete; the threadExit operation is the portable self-termination path */
    if (pthread_equal(thread->thread, pthread_self()) != 0)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: a thread cannot synchronously delete itself
    }

    /* Request cancellation; an already terminating joinable thread may report ESRCH */
    const int cancelStatus = pthread_cancel(thread->thread);
    if ((cancelStatus != 0) &&
        (cancelStatus != ESRCH))
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread cancellation request failed
    }

    /* Join to make deletion synchronous and reclaim native thread resources */
    const int joinStatus = pthread_join(thread->thread, NULL);
    if (joinStatus != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread join failed
    }

    /* Acquire the registry lock before clearing ownership state */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Revalidate the registry slot before release-build mutation */
    const size_t currentThreadId = port->base.ptable->threadHandleFind(port, threadHandle);
    if ((currentThreadId == 0u) ||
        (currentThreadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread registry changed unexpectedly during deletion
    }

    port->base.ptable->threadSlotClear(port, currentThreadId - 1u);
    free(thread);

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was deleted and unregistered
}


/**
 * \brief Milestone-1 POSIX thread-suspend runtime stub.
 *
 * \details Portable pthreads do not provide a direct thread-suspend primitive
 *          with semantics equivalent to the generic OSAL contract. Returning
 *          success as a no-op would violate substitutability, therefore this
 *          operation fails explicitly until a portable policy is implemented.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param threadHandle  Registered thread handle.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR in Milestone 1.
 */
static Template_osalErr_e template_osalPosixThreadSuspend(void *const osal,
                                                          const Template_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadSuspend(%p, %p)", osal, (void *)threadHandle);

    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(threadHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind(port, threadHandle) != 0u);

    TEMPLATE_OSAL_POSIX_ASSERT(0);
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadSuspend -> %d", (int)TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: portable POSIX thread suspend is not implemented in Milestone 1
}


/**
 * \brief Milestone-1 POSIX thread-resume runtime stub.
 *
 * \details Portable pthreads do not provide a direct thread-resume primitive
 *          with semantics equivalent to the generic OSAL contract. Returning
 *          success as a no-op would violate substitutability, therefore this
 *          operation fails explicitly until a portable policy is implemented.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param threadHandle  Registered thread handle.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR in Milestone 1.
 */
static Template_osalErr_e template_osalPosixThreadResume(void *const osal,
                                                         const Template_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadResume(%p, %p)", osal, (void *)threadHandle);

    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(threadHandle != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind(port, threadHandle) != 0u);

    TEMPLATE_OSAL_POSIX_ASSERT(0);
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadResume -> %d", (int)TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: portable POSIX thread resume is not implemented in Milestone 1
}


/**
 * \brief Delay the calling POSIX thread.
 *
 * \param osal     Opaque pointer to the initialized POSIX OSAL instance.
 * \param delayMs  Delay duration in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixThreadDelay(void *const osal,
                                                        const Template_osalTimeMs_t delayMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay(%p, %u)", osal, (unsigned int)delayMs);

    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));

    if (delayMs == 0u)
    {
        (void)sched_yield();
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Success: zero-duration delay yielded execution
    }

    /* Build an absolute monotonic deadline to avoid accumulated EINTR drift */
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: monotonic clock query failed
    }

    template_osalPosixTimespecAddMs(&deadline, delayMs);

    int nativeStatus = 0;
    do
    {
        nativeStatus = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    }
    while (nativeStatus == EINTR);

    if (nativeStatus != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX thread delay failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread delay completed
}


/**
 * \brief Delay the calling POSIX thread until the next periodic wake-up point.
 *
 * \details The caller-owned wake reference remains in generic 32-bit OSAL
 *          milliseconds. The next reference is calculated arithmetically from
 *          the previous reference, while CLOCK_MONOTONIC and TIMER_ABSTIME are
 *          used for the actual wait so repeated periods do not accumulate drift.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param previousWakeTimeMs  In/out periodic wake reference in OSAL milliseconds.
 * \param periodMs            Period in milliseconds; must be non-zero.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixThreadDelayUntil(void *const osal,
                                                             Template_osalTimeMs_t *const previousWakeTimeMs,
                                                             const Template_osalTimeMs_t periodMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil(%p, %p, %u)",
                              osal, (void *)previousWakeTimeMs, (unsigned int)periodMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(previousWakeTimeMs != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(periodMs != 0u);

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: monotonic clock query failed
    }

    const uint64_t nowMs64 = ((uint64_t)now.tv_sec * 1000u) +
                             ((uint64_t)now.tv_nsec / 1000000u);
    const Template_osalTimeMs_t nowMs = (Template_osalTimeMs_t)nowMs64;
    const Template_osalTimeMs_t nextWakeTimeMs = *previousWakeTimeMs + periodMs;
    const int32_t waitMs = (int32_t)(nextWakeTimeMs - nowMs);

    if (waitMs > 0)
    {
        struct timespec deadline = now;
        template_osalPosixTimespecAddMs(&deadline, (Template_osalTimeMs_t)waitMs);

        int nativeStatus = 0;
        do
        {
            nativeStatus = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        }
        while (nativeStatus == EINTR);

        if (nativeStatus != 0)
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: periodic POSIX thread delay failed
        }
    }

    /* Advance the caller-owned periodic reference even when the deadline was already due */
    *previousWakeTimeMs = nextWakeTimeMs;

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: periodic delay completed
}


/**
 * \brief Terminate the calling POSIX thread.
 *
 * \details The current registered thread slot is cleared before native thread
 *          termination. The pthread is detached because no external Delete can
 *          join a resource after ThreadExit has explicitly released its registry ownership.
 *
 * \param osal  Opaque pointer to the initialized POSIX OSAL instance.
 *
 * \note This function does not return on a valid call.
 */
static void template_osalPosixThreadExit(void *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit(%p)", osal);

    if (osal == NULL)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> invalid OSAL");

        return;  // Exit: Error: invalid OSAL instance
    }

    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    if (!template_osalPosixIsValid(port) ||
        (port->base.ptable == NULL) ||
        (port->base.ptable->threadSlotClear == NULL))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> invalid backend state");

        return;  // Exit: Error: backend invariant is not satisfied
    }

    /* Acquire the resource mutex while locating and unregistering the current thread */
    Template_osalErr_e osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> resource lock failed: %d",
                                  (int)osalStatus);

        return;  // Exit: Error: resource mutex acquisition failed
    }

    const pthread_t currentThread = pthread_self();
    Template_osalPosixThread_s *currentTcb = NULL;
    size_t currentThreadIdx = TEMPLATE_OSAL_THREAD_SLOTS_NUM;

    for (size_t i = 0u; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (port->base.threadObjHandle[i].handle != NULL)
        {
            Template_osalPosixThread_s *const thread =
                (Template_osalPosixThread_s *)port->base.threadObjHandle[i].handle;

            if (pthread_equal(thread->thread, currentThread) != 0)
            {
                currentTcb       = thread;
                currentThreadIdx = i;
                break;
            }
        }
    }

    if (currentTcb == NULL)
    {
        (void)template_osalPosixResourceUnlock(port);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> current thread is not registered");

        return;  // Exit: Error: current thread is not registered
    }

    port->base.ptable->threadSlotClear(port, currentThreadIdx);

    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> resource unlock failed: %d",
                                  (int)osalStatus);

        return;  // Exit: Error: resource mutex release failed
    }

    /* Release backend bookkeeping before terminating the calling pthread */
    (void)pthread_detach(currentThread);
    free(currentTcb);

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> no return");
    pthread_exit(NULL);

    /* pthread_exit() shall not return */
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    while (1)
    {
        (void)0;
    }
}


/**
 * \brief Validate POSIX thread attributes.
 *
 * \param threadAttr  Pointer to the thread attributes.
 *
 * \return true if the thread attributes are valid; false otherwise.
 */
static bool template_osalPosixThreadAttrValidate(const Template_osalThreadAttr_s *const threadAttr)
{
    bool isValid = true;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadAttrValidate(%p)", (const void *)threadAttr);

    TEMPLATE_OSAL_POSIX_ASSERT(threadAttr != NULL);

    if (threadAttr->worker == NULL)
    {
        isValid = false;
    }

    if ((threadAttr->prio < TEMPLATE_OSAL_THREAD_PRIO_LOW) ||
        (threadAttr->prio >= TEMPLATE_OSAL_THREAD_PRIO_MAX_COUNT))
    {
        isValid = false;
    }

    if (threadAttr->stackSize == 0u)
    {
        isValid = false;
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadAttrValidate -> %d", (int)isValid);

    return isValid;  // Exit: Success: validation result returned
}


/**
 * \brief Adapt the OSAL worker signature to the pthread entry signature.
 *
 * \param context  Pointer to the embedded Template_osalPosixThreadArg_s argument pack.
 *
 * \return NULL after the component worker returns naturally.
 */
static void *template_osalPosixThreadThunk(void *const context)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadThunk(%p)", context);

    TEMPLATE_OSAL_POSIX_ASSERT(context != NULL);

    Template_osalPosixThreadArg_s *const arg = (Template_osalPosixThreadArg_s *)context;
    TEMPLATE_OSAL_POSIX_ASSERT(arg->worker != NULL);

    /* Run the component worker */
    arg->worker(arg->workerArgs);

    /* Natural worker return leaves the joinable thread resource registered for Delete/Deinit. */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadThunk -> natural worker return");

    return NULL;  // Exit: Success: worker returned naturally
}

// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ------------------------*/

#error "KIWI POSIX backend Milestone 1: critical-section API is not implemented"

/**
 * \brief Enter a POSIX critical section.
 *
 * \details Milestone 1 placeholder. The generic API and vtable entry are kept so
 *          generated code preserves the complete OSAL contract, but a POSIX
 *          critical-section policy has not been selected yet.
 *
 * \param osal  Opaque pointer to the initialized POSIX OSAL instance.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR while this milestone stub is selected.
 */
static Template_osalErr_e template_osalPosixCriticalSectionEnter(void *const osal)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixCriticalSectionEnter(%p)", osal);
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Milestone 1 stub is not implemented
}


/**
 * \brief Exit a POSIX critical section.
 *
 * \details Milestone 1 placeholder paired with template_osalPosixCriticalSectionEnter().
 *
 * \param osal  Opaque pointer to the initialized POSIX OSAL instance.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR while this milestone stub is selected.
 */
static Template_osalErr_e template_osalPosixCriticalSectionExit(void *const osal)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixCriticalSectionExit(%p)", osal);
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Milestone 1 stub is not implemented
}
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------------- Software timers -------------------------*/

#error "KIWI POSIX backend Milestone 1: software-timer API is not implemented"

/**
 * \brief Create a POSIX software timer.
 *
 * \details Milestone 1 placeholder. Software timers are intentionally deferred;
 *          the full generic interface remains present for a later milestone.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Output timer handle.
 * \param timerAttr    Generic software-timer attributes.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR while this milestone stub is selected.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerCreate(void *const osal,
                                                                Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                Template_osalSoftwareTimerAttr_s timerAttr)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate(%p, %p)",
                              osal, (void *)timerHandle);
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);
    (void)timerAttr;
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Milestone 1 stub is not implemented
}


/**
 * \brief Delete a POSIX software timer.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Timer handle to delete.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR while this milestone stub is selected.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerDelete(void *const osal,
                                                                const Template_osalSoftwareTimerHandle_t timerHandle)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerDelete(%p, %p)",
                              osal, (void *)timerHandle);
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Milestone 1 stub is not implemented
}


/**
 * \brief Start a POSIX software timer.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Timer handle to start.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR while this milestone stub is selected.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStart(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStart(%p, %p)",
                              osal, (void *)timerHandle);
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Milestone 1 stub is not implemented
}


/**
 * \brief Stop a POSIX software timer.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Timer handle to stop.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR while this milestone stub is selected.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStop(void *const osal,
                                                              const Template_osalSoftwareTimerHandle_t timerHandle)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStop(%p, %p)",
                              osal, (void *)timerHandle);
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Milestone 1 stub is not implemented
}


/**
 * \brief Reset a POSIX software timer.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Timer handle to reset.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR while this milestone stub is selected.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerReset(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle)
{
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerReset(%p, %p)",
                              osal, (void *)timerHandle);
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    TEMPLATE_OSAL_POSIX_ASSERT(0);

    return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Milestone 1 stub is not implemented
}
// END SOFTWARE_TIMER

// BEGIN TIME
/*--------------------------------- Time ----------------------------------*/

/**
 * \brief Retrieve the current POSIX monotonic time in milliseconds.
 *
 * \details CLOCK_MONOTONIC is used so the generic OSAL time base is not affected by
 *          wall-clock corrections. The value is intentionally truncated to the
 *          generic Template_osalTimeMs_t width, preserving wrap-around semantics.
 *
 * \param osal      Opaque pointer to the initialized POSIX OSAL instance.
 * \param osTimeMs  Output pointer receiving the monotonic time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixTimeMsGet(void *const osal,
                                                      Template_osalTimeMs_t *const osTimeMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixTimeMsGet(%p, %p)", osal, (void *)osTimeMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(osTimeMs != NULL);

    if (!template_osalPosixIsValid(osal))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_NOT_INIT_ERR;

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixTimeMsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: backend is not initialized
    }

    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixTimeMsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: monotonic clock read failed
    }

    const uint64_t timeMs = ((uint64_t)now.tv_sec * 1000u) +
                            ((uint64_t)now.tv_nsec / 1000000u);
    *osTimeMs = (Template_osalTimeMs_t)timeMs;

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixTimeMsGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: monotonic time was read
}
// END TIME

// BEGIN MEMORY
/*-------------------------------- Memory ---------------------------------*/

/**
 * \brief Allocate host memory and register the resulting pointer.
 *
 * \param osal    Opaque pointer to the initialized POSIX OSAL instance.
 * \param size    Allocation size in bytes.
 * \param memPtr  Output pointer receiving the allocated memory address.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMemAlloc(void *const osal,
                                                     const size_t size,
                                                     void **const memPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc(%p, %lu, %p)",
                              osal, (unsigned long)size, (void *)memPtr);

    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(memPtr != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(size != 0u);

    Template_osalPosix_s *const osalPosix = (Template_osalPosix_s *)osal;
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osalPosix));
    TEMPLATE_OSAL_POSIX_ASSERT(osalPosix->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(osalPosix->base.ptable->memFreeSlotFind != NULL);

    *memPtr = NULL;

    osalStatus = template_osalPosixResourceLock(osalPosix);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    const size_t memoryId = osalPosix->base.ptable->memFreeSlotFind(osalPosix);
    if ((memoryId == 0u) ||
        (memoryId > TEMPLATE_OSAL_MEM_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(osalPosix);
        osalStatus = TEMPLATE_OSAL_MEM_ALLOCATION_ERR;

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free memory registry slot
    }

    void *const allocatedPtr = malloc(size);
    if (allocatedPtr == NULL)
    {
        (void)template_osalPosixResourceUnlock(osalPosix);
        osalStatus = TEMPLATE_OSAL_MEM_ALLOCATION_ERR;

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: host heap allocation failed
    }

    osalPosix->base.memPtr[memoryId - 1u] = allocatedPtr;
    *memPtr                               = allocatedPtr;

    osalStatus = template_osalPosixResourceUnlock(osalPosix);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: memory block was allocated and registered
}


/**
 * \brief Free a registered host memory block.
 *
 * \param osal    Opaque pointer to the initialized POSIX OSAL instance.
 * \param memPtr  Registered memory pointer to release.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixMemFree(void *const osal,
                                                    void *const memPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree(%p, %p)", osal, memPtr);

    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(memPtr != NULL);

    Template_osalPosix_s *const osalPosix = (Template_osalPosix_s *)osal;
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osalPosix));
    TEMPLATE_OSAL_POSIX_ASSERT(osalPosix->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(osalPosix->base.ptable->memPtrFind != NULL);

    osalStatus = template_osalPosixResourceLock(osalPosix);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    const size_t memoryId = osalPosix->base.ptable->memPtrFind(osalPosix, memPtr);
    if ((memoryId == 0u) ||
        (memoryId > TEMPLATE_OSAL_MEM_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(osalPosix);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: memory pointer is not registered
    }

    osalPosix->base.memPtr[memoryId - 1u] = NULL;
    free(memPtr);

    osalStatus = template_osalPosixResourceUnlock(osalPosix);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: memory block was freed and unregistered
}
// END MEMORY

/*------------------------------- Predicate -------------------------------*/

/**
 * \brief Validate the POSIX OSAL backend instance.
 *
 * \param osal  Opaque pointer expected to reference Template_osalPosix_s.
 *
 * \return true when the instance is initialized and bound to the POSIX vtable; false otherwise.
 */
static bool template_osalPosixIsValid(const void *const osal)
{
    if (osal == NULL)
    {
        return false;  // Exit: Error: NULL instance is invalid
    }

    const Template_osalPosix_s *const osalPosix = (const Template_osalPosix_s *)osal;

    return (osalPosix->validFlag &&
            (osalPosix->base.vtable == &template_osalPosixVtable));  // Exit: Success: backend validity returned
}


/*-------------------------- Resource synchronization ---------------------*/

/**
 * \brief Acquire the backend-owned POSIX resource mutex.
 *
 * \details This native mutex protects OSAL registry updates only. It is not a
 *          generic Template_osalMutexHandle_t and consumes no user mutex slot.
 *
 * \param osalPosix  POSIX OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static inline Template_osalErr_e template_osalPosixResourceLock(Template_osalPosix_s *const osalPosix)
{
    TEMPLATE_OSAL_POSIX_ASSERT(osalPosix != NULL);

    if (pthread_mutex_lock(&osalPosix->resourceMutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: native resource mutex lock failed
    }

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: resource mutex was acquired
}


/**
 * \brief Release the backend-owned POSIX resource mutex.
 *
 * \param osalPosix  POSIX OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static inline Template_osalErr_e template_osalPosixResourceUnlock(Template_osalPosix_s *const osalPosix)
{
    TEMPLATE_OSAL_POSIX_ASSERT(osalPosix != NULL);

    if (pthread_mutex_unlock(&osalPosix->resourceMutex) != 0)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: native resource mutex unlock failed
    }

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: resource mutex was released
}


/*---------------------------- Native time helpers ------------------------*/

/**
 * \brief Add milliseconds to a normalized POSIX timespec value.
 *
 * \param timeSpec  POSIX timespec to update in place.
 * \param timeMs    Milliseconds to add.
 */
static inline void template_osalPosixTimespecAddMs(struct timespec *const timeSpec,
                                                   const Template_osalTimeMs_t timeMs)
{
    TEMPLATE_OSAL_POSIX_ASSERT(timeSpec != NULL);

    timeSpec->tv_sec += (time_t)(timeMs / 1000u);
    timeSpec->tv_nsec += (long)((timeMs % 1000u) * 1000000u);

    if (timeSpec->tv_nsec >= 1000000000L)
    {
        timeSpec->tv_sec += (time_t)(timeSpec->tv_nsec / 1000000000L);
        timeSpec->tv_nsec %= 1000000000L;
    }
}


/**
 * \brief Build an absolute CLOCK_REALTIME deadline for POSIX timed wait APIs.
 *
 * \param timeoutMs  Relative timeout in milliseconds.
 * \param deadline   Output absolute deadline.
 *
 * \return true when the deadline was created; false when clock_gettime() failed.
 */
static inline bool template_osalPosixRealtimeDeadlineGet(const Template_osalTimeMs_t timeoutMs,
                                                         struct timespec *const deadline)
{
    TEMPLATE_OSAL_POSIX_ASSERT(deadline != NULL);

    if (clock_gettime(CLOCK_REALTIME, deadline) != 0)
    {
        return false;  // Exit: Error: realtime clock read failed
    }

    template_osalPosixTimespecAddMs(deadline, timeoutMs);

    return true;  // Exit: Success: absolute realtime deadline was created
}


/**
 * \brief Wait on a native POSIX semaphore using the generic OSAL timeout semantics.
 *
 * \details The helper retries waits interrupted by signals. Immediate waits use
 *          sem_trywait(), infinite waits use sem_wait(), and finite waits use
 *          sem_timedwait() with one absolute CLOCK_REALTIME deadline. Queue and
 *          generic semaphore objects use this helper independently; native sem_t
 *          objects remain private backend implementation details.
 *
 * \param semaphore  Native POSIX semaphore.
 * \param timeoutMs  Generic OSAL timeout in milliseconds.
 *
 * \return Zero on success; otherwise -1 with errno preserved from the POSIX API.
 */
static inline int template_osalPosixSemaphorePendNative(sem_t *const semaphore,
                                                        const Template_osalTimeMs_t timeoutMs)
{
    TEMPLATE_OSAL_POSIX_ASSERT(semaphore != NULL);

    if (timeoutMs == 0u)
    {
        int result = 0;
        do
        {
            result = sem_trywait(semaphore);
        }
        while ((result != 0) && (errno == EINTR));

        return result;  // Exit: Success/Error: immediate native semaphore result returned
    }

    if (timeoutMs == TEMPLATE_OSAL_INFINITY_TOUT)
    {
        int result = 0;
        do
        {
            result = sem_wait(semaphore);
        }
        while ((result != 0) && (errno == EINTR));

        return result;  // Exit: Success/Error: infinite native semaphore result returned
    }

    struct timespec deadline = {0};
    if (!template_osalPosixRealtimeDeadlineGet(timeoutMs, &deadline))
    {
        errno = EINVAL;

        return -1;  // Exit: Error: absolute deadline could not be created
    }

    int result = 0;
    do
    {
        result = sem_timedwait(semaphore, &deadline);
    }
    while ((result != 0) && (errno == EINTR));

    return result;  // Exit: Success/Error: timed native semaphore result returned
}
