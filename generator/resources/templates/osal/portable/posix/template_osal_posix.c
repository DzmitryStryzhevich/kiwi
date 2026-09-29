/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

/**
 * \file     template_osal_posix.c
 * \brief    POSIX OSAL port for Template.
 * \details  POSIX implementation of the component-scoped Template OSAL contract.
 */

//===============================================================================[ INCLUDE ]========================================================================================

#include "template_osal_posix.h"
#include "template_osal.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
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
 * \details The ring buffer is protected by a POSIX mutex. Two backend-private
 *          POSIX semaphores represent free and occupied slots. They are not
 *          generic OSAL semaphore objects and never consume semaphore registry slots.
 */
typedef struct
{
    size_t          itemSize;        /*!< Size of one queue item in bytes. */
    size_t          depth;           /*!< Maximum number of queue items. */
    size_t          readIdx;         /*!< Next ring-buffer read index. */
    size_t          writeIdx;        /*!< Next ring-buffer write index. */
    size_t          itemCount;       /*!< Number of committed queue items. */
    void            *buffer;         /*!< Ring-buffer storage. */
    pthread_mutex_t mutex;           /*!< Native ring-buffer protection mutex. */
    sem_t           freeSlotsSmphr;  /*!< Backend-private count of free queue slots. */
    sem_t           busySlotsSmphr;  /*!< Backend-private count of occupied queue slots. */
} Template_osalPosixQueue_s;
// END QUEUE

// BEGIN STREAM_BUFFER
/**
 * \struct  Template_osalPosixStreamBuffer_s
 * \brief   POSIX byte stream-buffer control block.
 * \details The byte ring buffer is protected by an internal POSIX mutex.
 *          Separate condition variables notify blocked readers and writers
 *          when the configured receive trigger or required free capacity
 *          can be satisfied.
 */
typedef struct
{
    size_t          capacity;           /*!< Stream-buffer capacity in bytes. */
    size_t          triggerLevel;       /*!< Receive trigger level in bytes. */
    size_t          readIdx;            /*!< Next ring-buffer read index. */
    size_t          writeIdx;           /*!< Next ring-buffer write index. */
    size_t          dataCount;          /*!< Number of bytes currently stored. */
    size_t          readAwaiterCount;   /*!< Number of readers currently blocked for data. */
    size_t          writeAwaiterCount;  /*!< Number of writers currently blocked for capacity. */
    uint8_t         *buffer;            /*!< Byte ring-buffer storage. */
    pthread_mutex_t mutex;              /*!< Internal POSIX mutex protecting stream-buffer state. */
    pthread_cond_t  readCond;           /*!< POSIX condition variable used to notify blocked readers. */
    pthread_cond_t  writeCond;          /*!< POSIX condition variable used to notify blocked writers. */
} Template_osalPosixStreamBuffer_s;

/**
 * \struct  Template_osalPosixStreamBufferWaitCleanup_s
 * \brief   Cleanup context used when a blocked POSIX stream-buffer thread is cancelled.
 */
typedef struct
{
    Template_osalPosixStreamBuffer_s *streamBuffer; /*!< Stream-buffer object owning the blocked operation. */
    bool                             writeAwaiter;   /*!< true for a blocked writer; false for a blocked reader. */
} Template_osalPosixStreamBufferWaitCleanup_s;
// END STREAM_BUFFER

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
 *          Both POSIX semaphores are backend-private implementation details.
 */
typedef struct
{
    sem_t                         availableCountSmphr;  /*!< Counts currently available to Wait/Pend. */
    sem_t                         freeCountSmphr;       /*!< Remaining capacity up to maxCount. */
    Template_osalSemaphoreCount_t maxCount;             /*!< Configured maximum semaphore count. */
} Template_osalPosixSemaphore_s;
// END SEMAPHORE

// BEGIN EVENT_FLAGS
/**
 * \struct  Template_osalPosixEventFlagsAwaiter_s
 * \brief   POSIX event-flags awaiter descriptor.
 * \details The descriptor belongs to the waiting thread and remains linked to
 *          the event-flags object only while the wait operation is pending.
 */
typedef struct Template_osalPosixEventFlagsAwaiter_s
{
    struct Template_osalPosixEventFlagsAwaiter_s *next;         /*!< Next pending awaiter. */
    uint32_t                                     flags;        /*!< Requested event flags. */
    uint32_t                                     actualFlags;  /*!< Snapshot that satisfied the wait. */
    bool                                         waitAll;      /*!< true when all requested flags are required. */
    bool                                         noClear;      /*!< true when requested flags shall remain set. */
    bool                                         satisfied;    /*!< true when the wait condition has been satisfied. */
} Template_osalPosixEventFlagsAwaiter_s;

/**
 * \struct  Template_osalPosixEventFlags_s
 * \brief   POSIX event-flags control block.
 * \details The current flag state and awaiter list are protected by a native
 *          POSIX mutex. A condition variable wakes threads after their wait
 *          condition has been evaluated as satisfied.
 */
typedef struct
{
    pthread_mutex_t                       mutex;    /*!< Native event-flags protection mutex. */
    pthread_cond_t                        cond;     /*!< Native awaiter notification condition. */
    uint32_t                              flags;    /*!< Currently set event flags. */
    //
    Template_osalPosixEventFlagsAwaiter_s *awaiter;  /*!< Pending awaiter list. */
} Template_osalPosixEventFlags_s;

/**
 * \struct  Template_osalPosixEventFlagsWaitCleanup_s
 * \brief   Cleanup context used when a waiting POSIX thread is cancelled.
 */
typedef struct
{
    Template_osalPosixEventFlags_s        *eventFlags;  /*!< Event-flags object owning the awaiter list. */
    Template_osalPosixEventFlagsAwaiter_s *awaiter;     /*!< Awaiter descriptor owned by the calling thread. */
} Template_osalPosixEventFlagsWaitCleanup_s;
// END EVENT_FLAGS

// BEGIN THREAD
/**
 * \struct  Template_osalPosixThreadArg_s
 * \brief   Argument pack adapting the OSAL worker signature to pthread entry.
 */
typedef struct
{
    Template_osalThreadWorker_f worker;        /*!< OSAL worker entry function. */
    void                        *workerArgs;   /*!< User argument passed to the worker. */
} Template_osalPosixThreadArg_s;

/**
 * \struct  Template_osalPosixThread_s
 * \brief   POSIX thread control block owned by the POSIX backend.
 */
typedef struct
{
    pthread_t                     thread;   /*!< Native pthread identifier. */
    Template_osalPosixThreadArg_s arg;      /*!< Embedded pthread thunk argument pack. */
} Template_osalPosixThread_s;
// END THREAD

// BEGIN SOFTWARE_TIMER
/**
 * \struct  Template_osalPosixSoftwareTimer_s
 * \brief   POSIX software-timer control block.
 */
typedef struct
{
    timer_t nativeTimer; /*!< Native POSIX per-process timer identifier. */
} Template_osalPosixSoftwareTimer_s;
// END SOFTWARE_TIMER

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
 */
static Template_osalErr_e template_osalPosixStreamBufferCreate(void *const osal,
                                                               const size_t bufferSizeBytes,
                                                               const size_t triggerLevelBytes,
                                                               Template_osalStreamBufferHandle_t *const streamBufferHandle);

/**
 * \brief Delete a registered POSIX stream buffer.
 */
static Template_osalErr_e template_osalPosixStreamBufferDelete(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Put bytes into a registered POSIX stream buffer without waiting.
 */
static Template_osalErr_e template_osalPosixStreamBufferPut(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            const void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesPut);

/**
 * \brief Put bytes into a registered POSIX stream buffer using the requested timeout.
 */
static Template_osalErr_e template_osalPosixStreamBufferPost(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             const void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesPut);

/**
 * \brief Get already available bytes from a registered POSIX stream buffer.
 */
static Template_osalErr_e template_osalPosixStreamBufferGet(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesGet);

/**
 * \brief Wait indefinitely for bytes from a registered POSIX stream buffer.
 */
static Template_osalErr_e template_osalPosixStreamBufferWait(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             size_t *const bytesGet);

/**
 * \brief Get bytes from a registered POSIX stream buffer using the requested timeout.
 */
static Template_osalErr_e template_osalPosixStreamBufferPend(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesGet);

/**
 * \brief Reset a registered POSIX stream buffer.
 */
static Template_osalErr_e template_osalPosixStreamBufferReset(void *const osal,
                                                              const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Copy bytes into a stream buffer while its internal mutex is held.
 */
static size_t template_osalPosixStreamBufferDataWrite(Template_osalPosixStreamBuffer_s *const streamBuffer,
                                                      const void *const data,
                                                      const size_t dataLengthBytes);

/**
 * \brief Copy bytes from a stream buffer while its internal mutex is held.
 */
static size_t template_osalPosixStreamBufferDataRead(Template_osalPosixStreamBuffer_s *const streamBuffer,
                                                     void *const data,
                                                     const size_t dataLengthBytes);

/**
 * \brief Release a cancelled blocked stream-buffer operation and unlock its internal mutex.
 */
static void template_osalPosixStreamBufferWaitCleanup(void *const context);
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
 */
static Template_osalErr_e template_osalPosixEventFlagsCreate(void *const osal,
                                                             Template_osalEventFlagsHandle_t *const eventFlagsHandle);

/**
 * \brief Delete a POSIX event-flags object.
 */
static Template_osalErr_e template_osalPosixEventFlagsDelete(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle);

/**
 * \brief Set bits in a POSIX event-flags object.
 */
static Template_osalErr_e template_osalPosixEventFlagsSet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          const uint32_t flags);

/**
 * \brief Clear bits in a POSIX event-flags object.
 */
static Template_osalErr_e template_osalPosixEventFlagsClear(void *const osal,
                                                            const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                            const uint32_t flags);

/**
 * \brief Read a POSIX event-flags object.
 */
static Template_osalErr_e template_osalPosixEventFlagsGet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          uint32_t *const flags);

/**
 * \brief Wait for a POSIX event-flags condition.
 */
static Template_osalErr_e template_osalPosixEventFlagsWait(void *const osal,
                                                           const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                           const uint32_t flags,
                                                           const Template_osalEventFlagsOptions_e options,
                                                           const Template_osalTimeMs_t timeoutMs,
                                                           uint32_t *const actualFlags);

/**
 * \brief Check whether an event-flags snapshot satisfies one wait condition.
 */
static inline bool template_osalPosixEventFlagsConditionCheck(const uint32_t currentFlags,
                                                              const uint32_t requestedFlags,
                                                              const bool waitAll);

/**
 * \brief Remove one awaiter descriptor from an event-flags awaiter list.
 */
static void template_osalPosixEventFlagsAwaiterRemove(Template_osalPosixEventFlags_s *const eventFlags,
                                                      Template_osalPosixEventFlagsAwaiter_s *const awaiter);

/**
 * \brief Remove a cancelled awaiter and release the native event-flags mutex.
 */
static void template_osalPosixEventFlagsWaitCleanup(void *const context);
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
 */
static Template_osalErr_e template_osalPosixThreadSuspend(void *const osal,
                                                          const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Resume a registered POSIX thread.
 */
static Template_osalErr_e template_osalPosixThreadResume(void *const osal,
                                                         const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Yield execution of the calling POSIX thread.
 */
static Template_osalErr_e template_osalPosixThreadYield(void *const osal);

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
 */
static Template_osalErr_e template_osalPosixCriticalSectionEnter(void *const osal);

/**
 * \brief Exit a POSIX critical section.
 */
static Template_osalErr_e template_osalPosixCriticalSectionExit(void *const osal);
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------------- Software timers -------------------------*/

/**
 * \brief Create a POSIX software timer.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerCreate(void *const osal,
                                                                Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                Template_osalSoftwareTimerAttr_s timerAttr);

/**
 * \brief Delete a POSIX software timer.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerDelete(void *const osal,
                                                                const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Start a POSIX software timer.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStart(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Stop a POSIX software timer.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStop(void *const osal,
                                                              const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Reset a POSIX software timer.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerReset(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Arm a native POSIX software timer using the configured OSAL period.
 */
static inline int template_osalPosixSoftwareTimerArm(const Template_osalPosixSoftwareTimer_s *const timer,
                                                     const Template_osalSoftwareTimerAttr_s *const timerAttr);

/**
 * \brief Disarm a native POSIX software timer.
 */
static inline int template_osalPosixSoftwareTimerDisarm(const Template_osalPosixSoftwareTimer_s *const timer);

/**
 * \brief Dispatch a POSIX SIGEV_THREAD notification to the component callback.
 */
static void template_osalPosixSoftwareTimerCallback(union sigval value);
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
 * \brief Wait on a POSIX semaphore using the generic OSAL timeout semantics.
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
    .threadYield      = template_osalPosixThreadYield,
    .threadDelay      = template_osalPosixThreadDelay,
    .threadDelayUntil = template_osalPosixThreadDelayUntil,
    .threadExit       = template_osalPosixThreadExit,
// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------- Critical sections  ----------------- -------------*/
    .criticalSectionEnter = template_osalPosixCriticalSectionEnter,
    .criticalSectionExit  = template_osalPosixCriticalSectionExit,
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------- Software timers -- ----------------- -------------*/
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

    /*------------------------------- Validation ------------------------------*/
    .isValid = template_osalPosixIsValid
};

//=======================================================================[ PUBLIC INTERFACE FUNCTIONS ]===============================================================================

/**
 * \brief Initialize the Template POSIX OSAL instance.
 *
 * \details Initializes the generic OSAL base object, initializes the backend-owned
 *          resource mutex and binds the POSIX vtable. The resource mutex is POSIX
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
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid arguments

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid args
    }

    /* Initialize the generic OSAL base */
    osalStatus = template_osalInit(&osalPosix->base, name, parent);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: base initialization failed
    }

    /* Reset POSIX-specific instance state */
    osalPosix->param     = ((Template_osalPosixParam_s) {0});
    osalPosix->validFlag = false;

    if (param != NULL)
    {
        osalPosix->param = *param;
    }

    /* Create the backend-owned registry synchronization mutex */
    if (pthread_mutex_init(&osalPosix->resourceMutex, NULL) != 0)
    {
        (void)template_osalDeinit(&osalPosix->base);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Resource mutex creation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex creation failed
    }

    /* Bind the POSIX backend vtable */
    osalPosix->base.vtable = &template_osalPosixVtable;

    /* Mark the POSIX backend as valid */
    osalPosix->validFlag = true;

    /* Trace initialization result */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixInit -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: POSIX OSAL was initialized
}


/**
 * \brief Deinitialize the Template POSIX OSAL instance.
 *
 * \details Releases all registered POSIX resources on a best-effort basis,
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
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid arguments

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid args
    }

    /* Validate backend state */
    if (!template_osalPosixIsValid(osalPosix))
    {
        osalStatus = TEMPLATE_OSAL_NOT_INIT_ERR;  // Error: Backend is not initialized

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: backend is not initialized
    }

// BEGIN SOFTWARE_TIMER
    /* Delete registered software timers */
    for (size_t i = 0u; i < TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.softwareTimerObj[i].handle != NULL)
        {
            (void)template_osalPosixSoftwareTimerDelete(osalPosix,
                                                        osalPosix->base.softwareTimerObj[i].handle);
        }
    }

// END SOFTWARE_TIMER

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

// BEGIN STREAM_BUFFER
    /* Delete registered stream buffers */
    for (size_t i = 0u; i < TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.streamBufferObjHandle[i] != NULL)
        {
            (void)template_osalPosixStreamBufferDelete(osalPosix,
                                                       osalPosix->base.streamBufferObjHandle[i]);
        }
    }

// END STREAM_BUFFER

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

// BEGIN EVENT_FLAGS
    /* Delete registered event-flags objects */
    for (size_t i = 0u; i < TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM; ++i)
    {
        if (osalPosix->base.eventFlagsObjHandle[i] != NULL)
        {
            (void)template_osalPosixEventFlagsDelete(osalPosix,
                                                     osalPosix->base.eventFlagsObjHandle[i]);
        }
    }

// END EVENT_FLAGS

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
    osalPosix->param       = ((Template_osalPosixParam_s) {0});

    if (pthread_mutex_destroy(&osalPosix->resourceMutex) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Resource mutex destruction failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixDeinit -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex destruction failed
    }

    /* Deinitialize the generic OSAL base */
    osalStatus = template_osalDeinit(&osalPosix->base);

    /* Trace deinitialization result */
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
 *          pthread mutex. freeSlotsSmphr and busySlotsSmphr are POSIX semaphores
 *          owned by the queue control block; they are not generic OSAL semaphore
 *           objects and do not consume semaphore registry slots.
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueFreeSlotFind != NULL);

    /* Reject values that cannot be represented by POSIX semaphores or storage size */
    if ((queueItemSize > (SIZE_MAX / queueDepth)) ||
        (queueDepth > (size_t)SEM_VALUE_MAX))
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue dimensions are not representable by the POSIX backend

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue dimensions are not representable by the POSIX backend
    }

    /* Clear the output value */
    *queueHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free queue registry slot */
    const size_t queueId = port->base.ptable->queueFreeSlotFind(port);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;  // Error: No free queue slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free queue slot
    }

    /* Allocate the queue control block */
    Template_osalPosixQueue_s *const queue =
        (Template_osalPosixQueue_s *)calloc(1u, sizeof(Template_osalPosixQueue_s));
    if (queue == NULL)
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_MEM_ALLOCATION_ERR;  // Error: Queue control-block allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue control-block allocation failed
    }

    /* Allocate ring-buffer storage */
    queue->buffer = calloc(queueDepth, queueItemSize);
    if (queue->buffer == NULL)
    {
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_MEM_ALLOCATION_ERR;  // Error: Queue storage allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue storage allocation failed
    }

    /* Initialize the queue control block */
    queue->itemSize  = queueItemSize;
    queue->depth     = queueDepth;
    queue->readIdx   = 0u;
    queue->writeIdx  = 0u;
    queue->itemCount = 0u;

    /* Initialize the internal queue mutex */
    if (pthread_mutex_init(&queue->mutex, NULL) != 0)
    {
        free(queue->buffer);
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;  // Error: Queue mutex initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex initialization failed
    }

    /* Initialize the free-slots semaphore */
    if (sem_init(&queue->freeSlotsSmphr, 0, (unsigned int)queueDepth) != 0)
    {
        (void)pthread_mutex_destroy(&queue->mutex);
        free(queue->buffer);
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;  // Error: Free-slot semaphore initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore initialization failed
    }

    /* Initialize the busy-slots semaphore */
    if (sem_init(&queue->busySlotsSmphr, 0, 0u) != 0)
    {
        (void)sem_destroy(&queue->freeSlotsSmphr);
        (void)pthread_mutex_destroy(&queue->mutex);
        free(queue->buffer);
        free(queue);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_QUEUE_CREATE_ERR;  // Error: Occupied-slot semaphore initialization failed

        /* Trace returned value */
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
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
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
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete(%p, %p)",
                              osal, (void *)queueHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(queueHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Lock */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the queue handle within the OSAL instance registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        /* Unlock */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid queue handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Down-casting of the queue handle */
    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Destroy internal synchronization resources */
    const int busyRc  = sem_destroy(&queue->busySlotsSmphr);
    const int freeRc  = sem_destroy(&queue->freeSlotsSmphr);
    const int mutexRc = pthread_mutex_destroy(&queue->mutex);
    if ((busyRc != 0) ||
        (freeRc != 0) ||
        (mutexRc != 0))
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Release queue storage and clear the registry slot */
        free(queue->buffer);
        free(queue);
        port->base.queueObjHandle[queueId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

        /* Unlock */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native queue synchronization teardown failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX queue synchronization teardown failed
    }

    /* Release queue storage and clear the registry slot */
    free(queue->buffer);
    free(queue);
    port->base.queueObjHandle[queueId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Unlock */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueDelete -> %d",
                              (int)osalStatus);

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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Try to find the queue handle within the OSAL instance registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid queue handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Down-casting of the queue handle */
    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Reserve one free queue slot without waiting */
    if (template_osalPosixSemaphorePendNative(&queue->freeSlotsSmphr, 0u) != 0)
    {
        if (errno == EAGAIN)
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_IS_FULL_ERR;  // Error: Queue is full
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native free-slot semaphore wait failed
        }

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: queue has no immediately available free slot
    }

    /* Lock using the internal queue mutex */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        (void)sem_post(&queue->freeSlotsSmphr);
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex acquisition failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    /* Copy an item to the next free slot */
    void *const dst = (uint8_t *)queue->buffer + (queue->writeIdx * queue->itemSize);
    memcpy(dst, queueItemPtr, queue->itemSize);
    queue->writeIdx = (queue->writeIdx + 1u) % queue->depth;
    ++queue->itemCount;

    /* Unlock */
    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex release failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Publish one occupied queue slot */
    if (sem_post(&queue->busySlotsSmphr) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Occupied-slot semaphore update failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: occupied-slot semaphore update failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPut -> %d",
                              (int)osalStatus);

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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Try to find the queue handle within the OSAL instance registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid queue handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Down-casting of the queue handle */
    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Reserve one free queue slot using the requested timeout */
    if (template_osalPosixSemaphorePendNative(&queue->freeSlotsSmphr, timeoutMs) != 0)
    {
        if ((errno == EAGAIN) ||
            (errno == ETIMEDOUT))
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_OVERFLOW_ERR;  // Error: Queue is full
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Port-specific issue
        }

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue post timed out or wait failed
    }

    /* Lock using the internal queue mutex */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        (void)sem_post(&queue->freeSlotsSmphr);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex acquisition failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    /* Copy an item to the next free queue slot */
    TEMPLATE_OSAL_POSIX_ASSERT(queue->itemCount < queue->depth);
    void *const dst = (uint8_t *)queue->buffer + (queue->writeIdx * queue->itemSize);
    memcpy(dst, queueItemPtr, queue->itemSize);
    queue->writeIdx = (queue->writeIdx + 1u) % queue->depth;
    ++queue->itemCount;

    /* Unlock */
    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex release failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Publish one occupied queue slot */
    if (sem_post(&queue->busySlotsSmphr) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Occupied-slot semaphore update failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: occupied-slot semaphore update failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Try to find the queue handle within the OSAL instance registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid queue handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Down-casting of the queue handle */
    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Pend one occupied queue slot without waiting */
    if (template_osalPosixSemaphorePendNative(&queue->busySlotsSmphr, 0u) != 0)
    {
        if (errno == EAGAIN)
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR;  // Error: Queue is empty
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native busy-slot semaphore wait failed
        }

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue has no immediately available item
    }

    /* Lock using the internal queue mutex */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        (void)sem_post(&queue->busySlotsSmphr);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex acquisition failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    /* Extract an item from the queue */
    TEMPLATE_OSAL_POSIX_ASSERT(queue->itemCount > 0);
    void *const src = (uint8_t *)queue->buffer + (queue->readIdx * queue->itemSize);
    memcpy(queueItemPtr, src, queue->itemSize);
    queue->readIdx = (queue->readIdx + 1u) % queue->depth;
    --queue->itemCount;

    /* Unlock */
    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex release failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Publish one free queue slot */
    if (sem_post(&queue->freeSlotsSmphr) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Free-slot semaphore update failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore update failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Try to find the queue handle within the OSAL instance registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid queue handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Down-casting of the queue handle */
    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Wait indefinitely for one occupied queue slot */
    if (template_osalPosixSemaphorePendNative(&queue->busySlotsSmphr,
                                              TEMPLATE_OSAL_INFINITY_TOUT) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native infinite queue wait failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: infinite queue wait failed
    }

    /* Lock using the internal queue mutex */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* This branch is impossible under normal conditions */
        (void)sem_post(&queue->busySlotsSmphr);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex acquisition failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    /* Retrieve an item from the queue */
    TEMPLATE_OSAL_POSIX_ASSERT(queue->itemCount > 0);
    void *const src = (uint8_t *)queue->buffer + (queue->readIdx * queue->itemSize);
    memcpy(queueItemPtr, src, queue->itemSize);
    queue->readIdx = (queue->readIdx + 1u) % queue->depth;
    --queue->itemCount;

    /* Unlock */
    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        /* This branch is considered as unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex release failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Publish one free queue slot */
    if (sem_post(&queue->freeSlotsSmphr) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Free-slot semaphore update failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore update failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Try to find the queue handle within the OSAL instance registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid queue handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Down-casting of the queue handle */
    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Wait for one occupied queue slot using the requested timeout */
    if (template_osalPosixSemaphorePendNative(&queue->busySlotsSmphr, timeoutMs) != 0)
    {
        if ((errno == EAGAIN) ||
            (errno == ETIMEDOUT))
        {
            osalStatus = TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR;  // Error: Queue receive timed out
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native busy-slot semaphore wait failed
        }

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue receive timed out or wait failed
    }

    /* Lock using the internal queue mutex */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        (void)sem_post(&queue->busySlotsSmphr);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex acquisition failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    /* Extract a queue item into the destination buffer */
    TEMPLATE_OSAL_POSIX_ASSERT(queue->itemCount > 0);
    void *const src = (uint8_t *)queue->buffer + (queue->readIdx * queue->itemSize);
    memcpy(queueItemPtr, src, queue->itemSize);
    queue->readIdx = (queue->readIdx + 1u) % queue->depth;
    --queue->itemCount;

    /* Unlock */
    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex release failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Publish one free queue slot */
    if (sem_post(&queue->freeSlotsSmphr) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Free-slot semaphore update failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore update failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueItemPend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was retrieved
}


/**
 * \brief Reset a registered POSIX queue to its initial empty state.
 *
 * \details Resets ring-buffer indices, discards queued data, drains both POSIX
 *          slot semaphores and restores freeSlotsSmphr to queue depth.
 *
 * \note The caller must ensure no producer or consumer is blocked on or using
 *       the queue while reset is performed. This matches the resource-lifecycle
 *       expectation for POSIX synchronization objects in this backend.
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->queueHandleFind != NULL);

    /* Try to find the queue handle within the OSAL instance registry */
    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid queue handle was passed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue handle is not registered
    }

    /* Down-casting of the queue handle */
    Template_osalPosixQueue_s *const queue = (Template_osalPosixQueue_s *)queueHandle;

    /* Lock */
    if (pthread_mutex_lock(&queue->mutex) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Port-specific error

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex acquisition failed
    }

    /* Reset the ring-buffer state */
    queue->readIdx   = 0u;
    queue->writeIdx  = 0u;
    queue->itemCount = 0u;
    memset(queue->buffer, 0, queue->itemSize * queue->depth);

    /* Drain the occupied-slot semaphore */
    int rc = 0;

    do
    {
        rc = sem_trywait(&queue->busySlotsSmphr);
    } while((rc == 0) ||
            ((rc != 0) &&
             (errno == EINTR)));

    if (errno != EAGAIN)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Unlock the internal queue control block mutex */
        (void)pthread_mutex_unlock(&queue->mutex);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Occupied-slot semaphore reset failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: occupied-slot semaphore reset failed
    }

    /* Drain and restore the free-slot semaphore to queue depth */
    do
    {
        rc = sem_trywait(&queue->freeSlotsSmphr);
    } while((rc == 0) ||
            ((rc != 0) &&
             (errno == EINTR)));

    if (errno != EAGAIN)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Unlock */
        (void)pthread_mutex_unlock(&queue->mutex);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Free-slot semaphore drain failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: free-slot semaphore drain failed
    }

    for (size_t i = 0u; i < queue->depth; ++i)
    {
        if (sem_post(&queue->freeSlotsSmphr) != 0)
        {
            /* This branch is impossible under normal conditions */
            TEMPLATE_OSAL_POSIX_ASSERT(0);

            /* Unlock */
            (void)pthread_mutex_unlock(&queue->mutex);
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Free-slot semaphore restore failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: free-slot semaphore restore failed
        }
    }

    /* Unlock */
    if (pthread_mutex_unlock(&queue->mutex) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Queue mutex release failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixQueueReset -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue was reset
}

// END QUEUE

// BEGIN STREAM_BUFFER
/*----------------------------- Stream buffers -----------------------------*/

/**
 * \brief Create a POSIX stream buffer and register it in the OSAL instance.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param bufferSizeBytes     Stream-buffer capacity in bytes.
 * \param triggerLevelBytes   Trigger level in bytes.
 * \param streamBufferHandle  Output pointer receiving the stream-buffer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferCreate(void *const osal,
                                                               const size_t bufferSizeBytes,
                                                               const size_t triggerLevelBytes,
                                                               Template_osalStreamBufferHandle_t *const streamBufferHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate(%p, %lu, %lu, %p)",
                              osal,
                              (unsigned long)bufferSizeBytes,
                              (unsigned long)triggerLevelBytes,
                              (void *)streamBufferHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(bufferSizeBytes != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(triggerLevelBytes != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(triggerLevelBytes <= bufferSizeBytes);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferFreeSlotFind != NULL);

    /* Clear the output value */
    *streamBufferHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find a free stream-buffer slot within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferFreeSlotFind(port);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR;  // Error: No free stream-buffer registry slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free stream-buffer registry slot
    }

    /* Allocate the stream-buffer control block */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)calloc(1u, sizeof(Template_osalPosixStreamBuffer_s));
    if (streamBuffer == NULL)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_MEM_ALLOCATION_ERR;  // Error: Stream-buffer control-block allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer control-block allocation failed
    }

    /* Allocate byte ring-buffer storage */
    streamBuffer->buffer = (uint8_t *)calloc(bufferSizeBytes, sizeof(uint8_t));
    if (streamBuffer->buffer == NULL)
    {
        /* Release acquired resources */
        free(streamBuffer);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_MEM_ALLOCATION_ERR;  // Error: Stream-buffer storage allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer storage allocation failed
    }

    /* Initialize the stream-buffer control block */
    streamBuffer->capacity          = bufferSizeBytes;
    streamBuffer->triggerLevel      = triggerLevelBytes;
    streamBuffer->readIdx           = 0u;
    streamBuffer->writeIdx          = 0u;
    streamBuffer->dataCount         = 0u;
    streamBuffer->readAwaiterCount  = 0u;
    streamBuffer->writeAwaiterCount = 0u;

    /* Initialize the internally used stream buffer mutex */
    int rc = pthread_mutex_init(&streamBuffer->mutex, NULL);
    if (rc != 0)
    {
        /* Release acquired resources */
        free(streamBuffer->buffer);
        free(streamBuffer);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR;  // Error: Stream-buffer mutex initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex initialization failed
    }

    /* Initialize the reader condition variable */
    rc = pthread_cond_init(&streamBuffer->readCond, NULL);
    if (rc != 0)
    {
        /* Release acquired resources */
        (void)pthread_mutex_destroy(&streamBuffer->mutex);
        free(streamBuffer->buffer);
        free(streamBuffer);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR;  // Error: Reader condition initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: reader condition initialization failed
    }

    /* Initialize the writer condition variable */
    rc = pthread_cond_init(&streamBuffer->writeCond, NULL);
    if (rc != 0)
    {
        /* Release acquired resources */
        (void)pthread_cond_destroy(&streamBuffer->readCond);
        (void)pthread_mutex_destroy(&streamBuffer->mutex);
        free(streamBuffer->buffer);
        free(streamBuffer);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR;  // Error: Writer condition initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: writer condition initialization failed
    }

    /* Register the stream-buffer handle in the OSAL registry */
    port->base.streamBufferObjHandle[streamBufferId - 1u] = (Template_osalStreamBufferHandle_t)streamBuffer;
    *streamBufferHandle                                   = (Template_osalStreamBufferHandle_t)streamBuffer;

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream buffer was created and registered
}


/**
 * \brief Delete a registered POSIX stream buffer.
 *
 * \details The caller is responsible for object lifetime and shall ensure that
 *          no ordinary operation uses the object concurrently with deletion.
 *          Deletion is additionally rejected while one or more threads are
 *          blocked waiting for data or free capacity.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferDelete(void *const osal,
                                                               const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete(%p, %p)",
                              osal, (void *)streamBufferHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the stream-buffer handle within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid stream-buffer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Down-casting of the stream-buffer handle */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)streamBufferHandle;

    /* Lock internally used stream buffer mutex */
    int rc = pthread_mutex_lock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex locking failed
    }

    /* Reject deletion while blocked operations still use the object */
    if ((streamBuffer->readAwaiterCount != 0u) ||
        (streamBuffer->writeAwaiterCount != 0u))
    {
        /* Unlock internally used stream buffer mutex */
        (void)pthread_mutex_unlock(&streamBuffer->mutex);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer object still has active awaiters

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer object still has active awaiters
    }

    /* Unlock internally used stream buffer mutex */
    rc = pthread_mutex_unlock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex unlocking failed
    }

    /* Destroy backend-private synchronization resources */
    const int readCondRc  = pthread_cond_destroy(&streamBuffer->readCond);
    const int writeCondRc = pthread_cond_destroy(&streamBuffer->writeCond);
    const int mutexRc     = pthread_mutex_destroy(&streamBuffer->mutex);
    if ((readCondRc != 0) ||
        (writeCondRc != 0) ||
        (mutexRc != 0))
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Release acquired resources */
        free(streamBuffer->buffer);
        free(streamBuffer);
        port->base.streamBufferObjHandle[streamBufferId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer synchronization teardown failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer synchronization teardown failed
    }

    /* Release memory and clear the registry slot */
    free(streamBuffer->buffer);
    free(streamBuffer);
    port->base.streamBufferObjHandle[streamBufferId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream buffer was deleted and unregistered
}


/**
 * \brief Put bytes into a registered POSIX stream buffer without waiting.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Source data buffer.
 * \param dataLengthBytes     Number of bytes to write.
 * \param bytesPut            Output pointer receiving the number of bytes written.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferPut(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            const void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesPut)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut(%p, %p, %p, %lu, %p)",
                              osal,
                              (void *)streamBufferHandle,
                              data,
                              (unsigned long)dataLengthBytes,
                              (void *)bytesPut);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(data != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(bytesPut != NULL);

    /* Clear the output value */
    *bytesPut = 0u;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Try to find the stream-buffer handle within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid stream-buffer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Down-casting of the stream-buffer handle */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)streamBufferHandle;

    /* Lock internally used stream buffer mutex */
    int rc = pthread_mutex_lock(&streamBuffer->mutex);
    if (rc != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex locking failed
    }

    /* Put as many bytes as current free capacity allows */
    *bytesPut = template_osalPosixStreamBufferDataWrite(streamBuffer, data, dataLengthBytes);
    if ((*bytesPut != 0u) &&
        (streamBuffer->dataCount >= streamBuffer->triggerLevel))
    {
        rc = pthread_cond_broadcast(&streamBuffer->readCond);
        if (rc != 0)
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Reader condition notification failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: reader condition notification failed
        }
    }

    /* Unlock internally used stream buffer mutex */
    rc = pthread_mutex_unlock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex unlocking failed
    }

    if (*bytesPut == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_FULL_ERR;  // Error: Stream buffer is full

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer capacity was available
    }

    /* Trace output value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut: bytesPut = %lu",
                              (unsigned long)*bytesPut);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPut -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was put
}


/**
 * \brief Put bytes into a registered POSIX stream buffer using the requested timeout.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Source data buffer.
 * \param dataLengthBytes     Number of bytes to write.
 * \param timeoutMs           Maximum wait time in milliseconds.
 * \param bytesPut            Output pointer receiving the number of bytes written.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferPost(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             const void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesPut)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost(%p, %p, %p, %lu, %u, %p)",
                              osal,
                              (void *)streamBufferHandle,
                              data,
                              (unsigned long)dataLengthBytes,
                              (unsigned int)timeoutMs,
                              (void *)bytesPut);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(data != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(bytesPut != NULL);

    /* Clear the output value */
    *bytesPut = 0u;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Try to find the stream-buffer handle within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid stream-buffer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Down-casting of the stream-buffer handle */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)streamBufferHandle;

    /* Lock internally used stream buffer mutex */
    int rc = pthread_mutex_lock(&streamBuffer->mutex);
    if (rc != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex locking failed
    }

    const size_t requiredSpace =
        (dataLengthBytes < streamBuffer->capacity) ? dataLengthBytes : streamBuffer->capacity;

    size_t freeSpace = streamBuffer->capacity - streamBuffer->dataCount;
    int waitRc       = 0;
    int unlockRc     = 0;

    if ((timeoutMs != 0u) &&
        (freeSpace < requiredSpace))
    {
        struct timespec deadline = {0};
        if ((timeoutMs != TEMPLATE_OSAL_INFINITY_TOUT) &&
            !template_osalPosixRealtimeDeadlineGet(timeoutMs, &deadline))
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer post deadline creation failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: stream-buffer post deadline creation failed
        }

        ++streamBuffer->writeAwaiterCount;

        Template_osalPosixStreamBufferWaitCleanup_s cleanup =
        {
            .streamBuffer = streamBuffer,
            .writeAwaiter = true
        };

        /* Release blocked writer state and unlock the object if the POSIX thread is cancelled */
        pthread_cleanup_push(template_osalPosixStreamBufferWaitCleanup, &cleanup);

        while ((streamBuffer->capacity - streamBuffer->dataCount) < requiredSpace)
        {
            if (timeoutMs == TEMPLATE_OSAL_INFINITY_TOUT)
            {
                waitRc = pthread_cond_wait(&streamBuffer->writeCond, &streamBuffer->mutex);
            }
            else
            {
                waitRc = pthread_cond_timedwait(&streamBuffer->writeCond,
                                                &streamBuffer->mutex,
                                                &deadline);
            }

            if ((waitRc != 0) &&
                (waitRc != ETIMEDOUT))
            {
                break;
            }

            if (waitRc == ETIMEDOUT)
            {
                break;
            }
        }

        TEMPLATE_OSAL_POSIX_ASSERT(streamBuffer->writeAwaiterCount > 0u);
        --streamBuffer->writeAwaiterCount;

        pthread_cleanup_pop(0);

        if ((waitRc != 0) &&
            (waitRc != ETIMEDOUT))
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Writer condition wait failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: writer condition wait failed
        }
    }

    /* Put as many bytes as current free capacity allows */
    *bytesPut = template_osalPosixStreamBufferDataWrite(streamBuffer, data, dataLengthBytes);

    if ((*bytesPut != 0u) &&
        (streamBuffer->dataCount >= streamBuffer->triggerLevel))
    {
        rc = pthread_cond_broadcast(&streamBuffer->readCond);
        if (rc != 0)
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Reader condition notification failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: reader condition notification failed
        }
    }

    /* Unlock internally used stream buffer mutex */
    unlockRc = pthread_mutex_unlock(&streamBuffer->mutex);
    if (unlockRc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex unlocking failed
    }

    if (*bytesPut == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_FULL_ERR;  // Error: Stream buffer is full

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer data was put before timeout
    }

    /* Trace output value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost: bytesPut = %lu",
                              (unsigned long)*bytesPut);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPost -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was put
}


/**
 * \brief Get already available bytes from a registered POSIX stream buffer.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Destination data buffer.
 * \param dataLengthBytes     Maximum number of bytes to read.
 * \param bytesGet            Output pointer receiving the number of bytes read.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferGet(void *const osal,
                                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                            void *const data,
                                                            const size_t dataLengthBytes,
                                                            size_t *const bytesGet)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet(%p, %p, %p, %lu, %p)",
                              osal,
                              (void *)streamBufferHandle,
                              data,
                              (unsigned long)dataLengthBytes,
                              (void *)bytesGet);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(data != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(bytesGet != NULL);

    /* Clear the output value */
    *bytesGet = 0u;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Try to find the stream-buffer handle within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid stream-buffer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Down-casting of the stream-buffer handle */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)streamBufferHandle;

    /* Lock internally used stream buffer mutex */
    int rc = pthread_mutex_lock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex locking failed
    }

    /* Get as many currently available bytes as the destination can hold */
    *bytesGet = template_osalPosixStreamBufferDataRead(streamBuffer, data, dataLengthBytes);

    if (*bytesGet != 0u)
    {
        rc = pthread_cond_broadcast(&streamBuffer->writeCond);
        if (rc != 0)
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Writer condition notification failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: writer condition notification failed
        }
    }

    /* Unlock internally used stream buffer mutex */
    rc = pthread_mutex_unlock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex unlocking failed
    }

    if (*bytesGet == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR;  // Error: Stream buffer is empty

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer data was available
    }

    /* Trace output value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet: bytesGet = %lu",
                              (unsigned long)*bytesGet);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was retrieved
}


/**
 * \brief Wait indefinitely for bytes from a registered POSIX stream buffer.
 *
 * \details The configured trigger level is used only when the stream buffer is
 *          empty when this operation begins. Already available data is returned
 *          immediately,
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Destination data buffer.
 * \param dataLengthBytes     Maximum number of bytes to read.
 * \param bytesGet            Output pointer receiving the number of bytes read.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferWait(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             size_t *const bytesGet)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait(%p, %p, %p, %lu, %p)",
                              osal,
                              (void *)streamBufferHandle,
                              data,
                              (unsigned long)dataLengthBytes,
                              (void *)bytesGet);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(data != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(bytesGet != NULL);

    /* Clear the output value */
    *bytesGet = 0u;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Try to find the stream-buffer handle within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid stream-buffer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Down-casting of the stream-buffer handle */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)streamBufferHandle;

    /* Lock internally used stream buffer mutex */
    int rc = pthread_mutex_lock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex locking failed
    }

    int waitRc   = 0;
    int unlockRc = 0;

    if (streamBuffer->dataCount == 0u)
    {
        ++streamBuffer->readAwaiterCount;

        Template_osalPosixStreamBufferWaitCleanup_s cleanup =
        {
            .streamBuffer = streamBuffer,
            .writeAwaiter = false
        };

        /* Release blocked reader state and unlock the object if the POSIX thread is cancelled */
        pthread_cleanup_push(template_osalPosixStreamBufferWaitCleanup, &cleanup);

        while (streamBuffer->dataCount < streamBuffer->triggerLevel)
        {
            waitRc = pthread_cond_wait(&streamBuffer->readCond, &streamBuffer->mutex);
            if (waitRc != 0)
            {
                break;
            }
        }

        TEMPLATE_OSAL_POSIX_ASSERT(streamBuffer->readAwaiterCount > 0u);
        --streamBuffer->readAwaiterCount;

        pthread_cleanup_pop(0);

        if (waitRc != 0)
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Reader condition wait failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: reader condition wait failed
        }
    }

    /* Get as many available bytes as the destination can hold */
    *bytesGet = template_osalPosixStreamBufferDataRead(streamBuffer, data, dataLengthBytes);

    if (*bytesGet != 0u)
    {
        rc = pthread_cond_broadcast(&streamBuffer->writeCond);
        if (rc != 0)
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Writer condition notification failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: writer condition notification failed
        }
    }

    /* Unlock internally used stream buffer mutex */
    unlockRc = pthread_mutex_unlock(&streamBuffer->mutex);
    if (unlockRc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex unlocking failed
    }

    if (*bytesGet == 0u)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR;  // Error: Stream-buffer wait completed without data

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer wait completed without data
    }

    /* Trace output value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait: bytesGet = %lu",
                              (unsigned long)*bytesGet);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was retrieved
}


/**
 * \brief Get bytes from a registered POSIX stream buffer using the requested timeout.
 *
 * \details The configured trigger level is used only when the stream buffer is
 *          empty when this operation begins. When the timeout expires before the
 *          trigger level is reached, already accumulated bytes are still returned.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 * \param data                Destination data buffer.
 * \param dataLengthBytes     Maximum number of bytes to read.
 * \param timeoutMs           Maximum wait time in milliseconds.
 * \param bytesGet            Output pointer receiving the number of bytes read.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferPend(void *const osal,
                                                             const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                             void *const data,
                                                             const size_t dataLengthBytes,
                                                             const Template_osalTimeMs_t timeoutMs,
                                                             size_t *const bytesGet)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend(%p, %p, %p, %lu, %u, %p)",
                              osal,
                              (void *)streamBufferHandle,
                              data,
                              (unsigned long)dataLengthBytes,
                              (unsigned int)timeoutMs,
                              (void *)bytesGet);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(data != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(bytesGet != NULL);

    /* Clear the output value */
    *bytesGet = 0u;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Try to find the stream-buffer handle within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid stream-buffer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Down-casting of the stream-buffer handle */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)streamBufferHandle;

    /* Lock internally used stream buffer mutex */
    int rc = pthread_mutex_lock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex locking failed
    }

    int waitRc   = 0;
    int unlockRc = 0;

    if ((streamBuffer->dataCount == 0u) &&
        (timeoutMs != 0u))
    {
        struct timespec deadline = {0};
        if ((timeoutMs != TEMPLATE_OSAL_INFINITY_TOUT) &&
            !template_osalPosixRealtimeDeadlineGet(timeoutMs, &deadline))
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer pend deadline creation failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: stream-buffer pend deadline creation failed
        }

        ++streamBuffer->readAwaiterCount;

        Template_osalPosixStreamBufferWaitCleanup_s cleanup =
        {
            .streamBuffer = streamBuffer,
            .writeAwaiter = false
        };

        /* Release blocked reader state and unlock the object if the POSIX thread is cancelled */
        pthread_cleanup_push(template_osalPosixStreamBufferWaitCleanup, &cleanup);

        while (streamBuffer->dataCount < streamBuffer->triggerLevel)
        {
            if (timeoutMs == TEMPLATE_OSAL_INFINITY_TOUT)
            {
                waitRc = pthread_cond_wait(&streamBuffer->readCond, &streamBuffer->mutex);
            }
            else
            {
                waitRc = pthread_cond_timedwait(&streamBuffer->readCond,
                                                &streamBuffer->mutex,
                                                &deadline);
            }

            if ((waitRc != 0) &&
                (waitRc != ETIMEDOUT))
            {
                break;
            }

            if (waitRc == ETIMEDOUT)
            {
                break;
            }
        }

        TEMPLATE_OSAL_POSIX_ASSERT(streamBuffer->readAwaiterCount > 0u);
        --streamBuffer->readAwaiterCount;

        pthread_cleanup_pop(0);

        if ((waitRc != 0) &&
            (waitRc != ETIMEDOUT))
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Reader condition wait failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: reader condition wait failed
        }
    }

    /* Get as many available bytes as the destination can hold */
    *bytesGet = template_osalPosixStreamBufferDataRead(streamBuffer, data, dataLengthBytes);

    if (*bytesGet != 0u)
    {
        rc = pthread_cond_broadcast(&streamBuffer->writeCond);
        if (rc != 0)
        {
            /* Unlock internally used stream buffer mutex */
            (void)pthread_mutex_unlock(&streamBuffer->mutex);

            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Writer condition notification failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: writer condition notification failed
        }
    }

    /* Unlock internally used stream buffer mutex */
    unlockRc = pthread_mutex_unlock(&streamBuffer->mutex);
    if (unlockRc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Stream-buffer mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex unlocking failed
    }

    if (*bytesGet == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR;  // Error: Stream buffer is empty

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer data was retrieved before timeout
    }

    /* Trace output value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend: bytesGet = %lu",
                              (unsigned long)*bytesGet);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferPend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was retrieved
}


/**
 * \brief Reset a registered POSIX stream buffer.
 *
 * \details Reset is rejected while one or more threads are blocked waiting for
 *          data or free capacity,
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param streamBufferHandle  Registered stream-buffer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixStreamBufferReset(void *const osal,
                                                              const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferReset(%p, %p)",
                              osal, (void *)streamBufferHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(streamBufferHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Try to find the stream-buffer handle within the OSAL instance registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid stream-buffer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer handle is not registered
    }

    /* Down-casting of the stream-buffer handle */
    Template_osalPosixStreamBuffer_s *const streamBuffer =
        (Template_osalPosixStreamBuffer_s *)streamBufferHandle;

    /* Lock internally used stream buffer mutex */
    int rc = pthread_mutex_lock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_RESET_ERR;  // Error: Stream-buffer mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex locking failed
    }

    /* Reject reset while blocked readers or writers still use the object */
    if ((streamBuffer->readAwaiterCount != 0u) ||
        (streamBuffer->writeAwaiterCount != 0u))
    {
        /* Unlock internally used stream buffer mutex */
        (void)pthread_mutex_unlock(&streamBuffer->mutex);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_RESET_ERR;  // Error: Stream buffer still has active awaiters

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream buffer still has active awaiters
    }

    /* Reset the byte ring buffer to the empty state */
    streamBuffer->readIdx   = 0u;
    streamBuffer->writeIdx  = 0u;
    streamBuffer->dataCount = 0u;

    /* Unlock internally used stream buffer mutex */
    rc = pthread_mutex_unlock(&streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_RESET_ERR;  // Error: Stream-buffer mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer mutex unlocking failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixStreamBufferReset -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream buffer was reset
}


/**
 * \brief Copy bytes into a stream buffer while its internal mutex is held.
 *
 * \param streamBuffer    Stream-buffer object.
 * \param data            Source data buffer.
 * \param dataLengthBytes Maximum number of bytes to copy.
 *
 * \return Number of bytes copied into the stream buffer.
 */
static size_t template_osalPosixStreamBufferDataWrite(Template_osalPosixStreamBuffer_s *const streamBuffer,
                                                      const void *const data,
                                                      const size_t dataLengthBytes)
{
    TEMPLATE_OSAL_POSIX_ASSERT(streamBuffer != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(data != NULL);

    const size_t freeSpace =
        streamBuffer->capacity - streamBuffer->dataCount;
    const size_t bytesToWrite =
        (dataLengthBytes < freeSpace) ? dataLengthBytes : freeSpace;

    if (bytesToWrite == 0u)
    {
        return 0u;  // Exit: Success: no free stream-buffer capacity was available
    }

    const size_t firstPart =
        ((streamBuffer->capacity - streamBuffer->writeIdx) < bytesToWrite) ?
        (streamBuffer->capacity - streamBuffer->writeIdx) :
        bytesToWrite;

    memcpy(&streamBuffer->buffer[streamBuffer->writeIdx], data, firstPart);

    const size_t secondPart = bytesToWrite - firstPart;
    if (secondPart != 0u)
    {
        memcpy(&streamBuffer->buffer[0],
               &((const uint8_t *)data)[firstPart],
               secondPart);
    }

    streamBuffer->writeIdx =
        (streamBuffer->writeIdx + bytesToWrite) % streamBuffer->capacity;
    streamBuffer->dataCount += bytesToWrite;

    return bytesToWrite;  // Exit: Success: copied byte count returned
}


/**
 * \brief Copy bytes from a stream buffer while its internal mutex is held.
 *
 * \param streamBuffer    Stream-buffer object.
 * \param data            Destination data buffer.
 * \param dataLengthBytes Maximum number of bytes to copy.
 *
 * \return Number of bytes copied from the stream buffer.
 */
static size_t template_osalPosixStreamBufferDataRead(Template_osalPosixStreamBuffer_s *const streamBuffer,
                                                     void *const data,
                                                     const size_t dataLengthBytes)
{
    TEMPLATE_OSAL_POSIX_ASSERT(streamBuffer != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(data != NULL);

    const size_t bytesToRead =
        (dataLengthBytes < streamBuffer->dataCount) ? dataLengthBytes : streamBuffer->dataCount;

    if (bytesToRead == 0u)
    {
        return 0u;  // Exit: Success: no stream-buffer data was available
    }

    const size_t firstPart =
        ((streamBuffer->capacity - streamBuffer->readIdx) < bytesToRead) ?
        (streamBuffer->capacity - streamBuffer->readIdx) :
        bytesToRead;

    memcpy(data, &streamBuffer->buffer[streamBuffer->readIdx], firstPart);

    const size_t secondPart = bytesToRead - firstPart;
    if (secondPart != 0u)
    {
        memcpy(&((uint8_t *)data)[firstPart],
               &streamBuffer->buffer[0],
               secondPart);
    }

    streamBuffer->readIdx =
        (streamBuffer->readIdx + bytesToRead) % streamBuffer->capacity;
    streamBuffer->dataCount -= bytesToRead;

    return bytesToRead;  // Exit: Success: copied byte count returned
}


/**
 * \brief Release a cancelled blocked stream-buffer operation and unlock its internal mutex.
 *
 * \param context  Pointer to Template_osalPosixStreamBufferWaitCleanup_s.
 */
static void template_osalPosixStreamBufferWaitCleanup(void *const context)
{
    Template_osalPosixStreamBufferWaitCleanup_s *const cleanup =
        (Template_osalPosixStreamBufferWaitCleanup_s *)context;

    TEMPLATE_OSAL_POSIX_ASSERT(cleanup != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(cleanup->streamBuffer != NULL);

    if (cleanup->writeAwaiter)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(cleanup->streamBuffer->writeAwaiterCount > 0u);
        --cleanup->streamBuffer->writeAwaiterCount;
    }
    else
    {
        TEMPLATE_OSAL_POSIX_ASSERT(cleanup->streamBuffer->readAwaiterCount > 0u);
        --cleanup->streamBuffer->readAwaiterCount;
    }

    /* Unlock internally used stream buffer mutex */
    const int rc = pthread_mutex_unlock(&cleanup->streamBuffer->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);
    }
}

// END STREAM_BUFFER

// BEGIN MUTEX
/*-------------------------------- Mutexes ----------------------------------*/

/**
 * \brief Create a recursive POSIX mutex and register it in the OSAL instance.
 *
 * \details The generic KIWI OSAL mutex contract requires every public mutex to
 *          be recursive/reentrant. The POSIX backend therefore creates each
 *          POSIX mutex with PTHREAD_MUTEX_RECURSIVE semantics.
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

    /* Down-casting of the OSAL handle */
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
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free mutex registry slot */
    const size_t mutexId = port->base.ptable->mutexFreeSlotFind(port);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;  // Error: No free mutex slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free mutex slot
    }

    /* Allocate the mutex control block */
    Template_osalPosixMutex_s *const mutex =
        (Template_osalPosixMutex_s *)calloc(1u, sizeof(Template_osalPosixMutex_s));
    if (mutex == NULL)
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_MEM_ALLOCATION_ERR;  // Error: Mutex control-block allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex control-block allocation failed
    }

    /* Initialize POSIX mutex attributes */
    pthread_mutexattr_t attr;
    int rc = pthread_mutexattr_init(&attr);
    if (rc != 0)
    {
        free(mutex);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;  // Error: Native mutex attributes initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX mutex attributes initialization failed
    }

    /* Set the recursive mutex type */
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (rc != 0)
    {
        (void)pthread_mutexattr_destroy(&attr);
        free(mutex);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;  // Error: Recursive mutex attribute configuration failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: recursive mutex attribute configuration failed
    }

    /* Initialize the POSIX recursive mutex */
    rc = pthread_mutex_init(&mutex->mutex, &attr);
    (void)pthread_mutexattr_destroy(&attr);
    if (rc != 0)
    {
        free(mutex);
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_MUTEX_CREATE_ERR;  // Error: Recursive mutex initialization failed

        /* Trace returned value */
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
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Lock */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the mutex handle within the OSAL instance registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid mutex handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Down-casting of the mutex handle */
    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    /* Destroy the POSIX recursive mutex */
    if (pthread_mutex_destroy(&mutex->mutex) != 0)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Port-specific error

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX mutex destruction failed
    }

    /* Release mutex storage and clear the registry slot */
    free(mutex);
    port->base.mutexHandle[mutexId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Unlock */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Try to find the mutex handle within the OSAL instance registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid mutex handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Down-casting of the mutex handle */
    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    /* Lock */
    if (pthread_mutex_lock(&mutex->mutex) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Port-specific error

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX recursive mutex operation failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Try to find the mutex handle within the OSAL instance registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid mutex handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexTryLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Down-casting of the mutex handle */
    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    /* Try to lock without waiting */
    const int rc = pthread_mutex_trylock(&mutex->mutex);
    if (rc != 0)
    {
        if (rc == EBUSY)
        {
            osalStatus = TEMPLATE_OSAL_MUTEX_LOCK_ERR;  // Error: Mutex is not immediately available
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: POSIX mutex try-lock failed
        }

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexTryLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex is not immediately available or POSIX lock failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Try to find the mutex handle within the OSAL instance registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid mutex handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Down-casting of the mutex handle */
    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    /* Lock using the requested timeout */
    int rc = 0;

    if (timeoutMs == TEMPLATE_OSAL_INFINITY_TOUT)
    {
        rc = pthread_mutex_lock(&mutex->mutex);
    }
    else if (timeoutMs == 0u)
    {
        rc = pthread_mutex_trylock(&mutex->mutex);
    }
    else
    {
        struct timespec deadline;
        if (!template_osalPosixRealtimeDeadlineGet(timeoutMs, &deadline))
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Timed-lock deadline creation failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: timed-lock deadline creation failed
        }

        rc = pthread_mutex_timedlock(&mutex->mutex, &deadline);
    }

    if (rc != 0)
    {
        if ((rc == EBUSY) ||
            (rc == ETIMEDOUT))
        {
            osalStatus = TEMPLATE_OSAL_MUTEX_LOCK_ERR;  // Error: Mutex lock timed out
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: POSIX timed mutex lock failed
        }

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexPendLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex lock timed out or lock failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->mutexHandleFind != NULL);

    /* Try to find the mutex handle within the OSAL instance registry */
    const size_t mutexId = port->base.ptable->mutexHandleFind(port, mutexHandle);
    if ((mutexId == 0u) ||
        (mutexId > TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid mutex handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: mutex handle is not registered
    }

    /* Down-casting of the mutex handle */
    Template_osalPosixMutex_s *const mutex = (Template_osalPosixMutex_s *)mutexHandle;

    /* Unlock */
    if (pthread_mutex_unlock(&mutex->mutex) != 0)
    {
        osalStatus = TEMPLATE_OSAL_MUTEX_UNLOCK_ERR;  // Error: Couldn't unlock the mutex

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX recursive mutex unlock failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMutexUnlock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: mutex was unlocked
}

// END MUTEX

// BEGIN SEMAPHORE
/*--------------------------- Counting semaphores --------------------------*/

/**
 * \brief Create a bounded POSIX counting semaphore and register it in the OSAL instance.
 *
 * \details The implementation uses two backend-private POSIX semaphores. The
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreFreeSlotFind != NULL);

    /* Reject counts that cannot be represented by POSIX semaphores */
    if ((maxCount == 0u) ||
        (initialCount > maxCount) ||
        (maxCount > (Template_osalSemaphoreCount_t)SEM_VALUE_MAX))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid semaphore handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore count range is invalid for the POSIX backend
    }

    /* Clear the output value */
    *semaphoreHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free semaphore registry slot */
    const size_t semaphoreId = port->base.ptable->semaphoreFreeSlotFind(port);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR;  // Error: No free counting-semaphore slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free counting-semaphore slot
    }

    /* Allocate the semaphore control block */
    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)calloc(1u, sizeof(Template_osalPosixSemaphore_s));
    if (semaphore == NULL)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_SEMAPHORE_MEM_ALLOCATION_ERR;  // Error: Couldn't allocate memory for semaphore control block

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore control-block allocation failed
    }

    /* Initialize the available-count semaphore */
    if (sem_init(&semaphore->availableCountSmphr, 0, (unsigned int)initialCount) != 0)
    {
        /* Release acquired resources */
        free(semaphore);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR;  // Error: Available-count semaphore initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: available-count semaphore initialization failed
    }

    /* Initialize the free-count semaphore */
    const Template_osalSemaphoreCount_t freeCount = maxCount - initialCount;
    if (sem_init(&semaphore->freeCountSmphr, 0, (unsigned int)freeCount) != 0)
    {
        /* Release acquired resources */
        (void)sem_destroy(&semaphore->availableCountSmphr);
        free(semaphore);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR;  // Error: Couldn't create POSIX semaphore control block

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: capacity semaphore initialization failed
    }

    /* Max count of semaphore */
    semaphore->maxCount = maxCount;

    /* Register the semaphore handle */
    port->base.semaphoreObjHandle[semaphoreId - 1u] = (Template_osalSemaphoreHandle_t)semaphore;
    *semaphoreHandle                                = (Template_osalSemaphoreHandle_t)semaphore;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the semaphore handle within the OSAL instance registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid semaphore handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    /* Down-casting of the semaphore handle */
    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Destroy backend-private POSIX semaphores */
    const int availableRc = sem_destroy(&semaphore->availableCountSmphr);
    const int freeRc      = sem_destroy(&semaphore->freeCountSmphr);
    if ((availableRc != 0) ||
        (freeRc != 0))
    {
        /* This branch is very unlikely */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: POSIX counting-semaphore teardown failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX counting-semaphore teardown failed
    }

    /* Release memory for semaphore control block */
    free(semaphore);
    port->base.semaphoreObjHandle[semaphoreId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Try to find the semaphore handle within the OSAL instance registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid semaphore handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    /* Down-casting of the semaphore handle */
    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Consume one available count */
    if (template_osalPosixSemaphorePendNative(&semaphore->availableCountSmphr,
                                              TEMPLATE_OSAL_INFINITY_TOUT) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_WAIT_ERR;  // Error: Native counting-semaphore wait failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX counting-semaphore wait failed
    }

    /* Return one unit of configured capacity */
    if (sem_post(&semaphore->freeCountSmphr) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Port-specific error

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore capacity update failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Try to find the semaphore handle within the OSAL instance registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid semaphore handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    /* Casting of the semaphore handle */
    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Consume one available count using the requested timeout */
    if (template_osalPosixSemaphorePendNative(&semaphore->availableCountSmphr, timeoutMs) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_WAIT_ERR;  // Error: Semaphore wait timed out

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore wait timed out or POSIX wait failed
    }

    /* Return one unit of configured capacity */
    if (sem_post(&semaphore->freeCountSmphr) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Semaphore capacity update failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore capacity update failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Try to find the semaphore handle within the OSAL instance registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid semaphore handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    /* Down-casting of the semaphore handle */
    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Reserve one unit of configured capacity without waiting */
    if (template_osalPosixSemaphorePendNative(&semaphore->freeCountSmphr, 0u) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_POST_ERR;  // Error: Counting semaphore is already at maxCount

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting semaphore is already at maxCount
    }

    /* Publish one available count */
    if (sem_post(&semaphore->availableCountSmphr) != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        (void)sem_post(&semaphore->freeCountSmphr);
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_POST_ERR;  // Error: Native counting-semaphore post failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphorePost -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX counting-semaphore post failed
    }

    /* Trace returned value */
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state and ownership */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->semaphoreHandleFind != NULL);

    /* Try to find the semaphore handle within the OSAL instance registry */
    const size_t semaphoreId = port->base.ptable->semaphoreHandleFind(port, semaphoreHandle);
    if ((semaphoreId == 0u) ||
        (semaphoreId > TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid semaphore handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: counting-semaphore handle is not registered
    }

    /* Down-casting of the semaphore handle */
    Template_osalPosixSemaphore_s *const semaphore =
        (Template_osalPosixSemaphore_s *)semaphoreHandle;

    /* Get the current semaphore value */
    int semCount = 0;
    if (sem_getvalue(&semaphore->availableCountSmphr, &semCount) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native semaphore count query failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX semaphore count query failed
    }

    /*
     * POSIX permits sem_getvalue() to report a negative value when one or
     * more threads are blocked in sem_wait(). In that case, the absolute
     * value may represent the number of waiting threads instead of the
     * number of available semaphore counts.
     *
     * The OSAL API exposes only the number of currently available counts,
     * therefore any negative POSIX-specific value is normalized to zero.
     */
    if (semCount < 0)
    {
        semCount = 0;
    }

    /* Return current semaphore count to a caller */
    *semaphoreCount = (Template_osalSemaphoreCount_t)semCount;

    /* Trace returned value and semaphore count */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSemaphoreCountGet -> %d, count = %u",
                              (int)osalStatus,
                              (unsigned int)*semaphoreCount);

    return osalStatus;  // Exit: Success: semaphore count was returned
}

// END SEMAPHORE

// BEGIN EVENT_FLAGS
/*-------------------------------- Event flags ------------------------------*/

/**
 * \brief Create a POSIX event-flags object and register it in the OSAL instance.
 *
 * \details The event-flags state is protected by a backend-private POSIX mutex.
 *          A backend-private condition variable is used to block and wake
 *          threads waiting for event-flag conditions.
 *
 * \param osal              Opaque pointer to the initialized POSIX OSAL instance.
 * \param eventFlagsHandle  Output pointer receiving the event-flags handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixEventFlagsCreate(void *const osal,
                                                             Template_osalEventFlagsHandle_t *const eventFlagsHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate(%p, %p)",
                              osal, (void *)eventFlagsHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(eventFlagsHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->eventFlagsFreeSlotFind != NULL);

    /* Clear the output value before any operations */
    *eventFlagsHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find a free event-flags slot within the OSAL instance registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsFreeSlotFind(port);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_CREATE_ERR;  // Error: No free event-flags registry slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free event-flags registry slot
    }

    /* Allocate the event-flags control block */
    Template_osalPosixEventFlags_s *const eventFlags =
        (Template_osalPosixEventFlags_s *)calloc(1u, sizeof(Template_osalPosixEventFlags_s));
    if (eventFlags == NULL)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_MEM_ALLOCATION_ERR;  // Error: Event-flags control-block allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags control-block allocation failed
    }

    /* Initialize the native event-flags mutex */
    int rc = pthread_mutex_init(&eventFlags->mutex, NULL);
    if (rc != 0)
    {
        /* Release acquired resources */
        free(eventFlags);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_CREATE_ERR;  // Error: event-flags mutex initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags mutex initialization failed
    }

    /* Initialize the awaiter condition variable */
    rc = pthread_cond_init(&eventFlags->cond, NULL);
    if (rc != 0)
    {
        /* Release acquired resources */
        (void)pthread_mutex_destroy(&eventFlags->mutex);
        free(eventFlags);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_CREATE_ERR;  // Error: event-flags condition initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags condition initialization failed
    }

    /* Register the event-flags handle in the OSAL registry */
    port->base.eventFlagsObjHandle[eventFlagsId - 1u] = (Template_osalEventFlagsHandle_t)eventFlags;
    *eventFlagsHandle                                 = (Template_osalEventFlagsHandle_t)eventFlags;

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event-flags object was created and registered
}


/**
 * \brief Delete a registered POSIX event-flags object.
 *
 * \details The caller is responsible for object lifetime and shall ensure that
 *          no ordinary operation uses the object concurrently with deletion.
 *          Deletion is additionally rejected while one or more threads are
 *          awaiting an event-flags condition on the object.
 *
 * \param osal              Opaque pointer to the initialized POSIX OSAL instance.
 * \param eventFlagsHandle  Registered event-flags handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixEventFlagsDelete(void *const osal,
                                                             const Template_osalEventFlagsHandle_t eventFlagsHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete(%p, %p)",
                              osal, (void *)eventFlagsHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(eventFlagsHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the event-flags handle within the OSAL instance registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid event-flags handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags handle is not registered
    }

    /* Down-casting of the event-flags handle */
    Template_osalPosixEventFlags_s *const eventFlags =
        (Template_osalPosixEventFlags_s *)eventFlagsHandle;

    /* Lock internally used event flags mutex */
    int rc = pthread_mutex_lock(&eventFlags->mutex);
    if (rc != 0)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native event-flags mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native event-flags mutex locking failed
    }

    /* Reject deletion while awaiters are still linked to the object */
    if (eventFlags->awaiter != NULL)
    {
        /* Unlock internally used event flags mutex */
        (void)pthread_mutex_unlock(&eventFlags->mutex);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Event-flags object still has active awaiters

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags object still has active awaiters
    }

    /* Unlock internally used event flags mutex */
    rc = pthread_mutex_unlock(&eventFlags->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native event-flags mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native event-flags mutex unlocking failed
    }

    /* Destroy backend-private synchronization resources */
    const int condRc  = pthread_cond_destroy(&eventFlags->cond);
    const int mutexRc = pthread_mutex_destroy(&eventFlags->mutex);
    if ((condRc != 0) ||
        (mutexRc != 0))
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        /* Release acquired resources */
        free(eventFlags);
        port->base.eventFlagsObjHandle[eventFlagsId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native event-flags synchronization teardown failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native event-flags synchronization teardown failed
    }

    /* Release memory and clear the registry slot */
    free(eventFlags);
    port->base.eventFlagsObjHandle[eventFlagsId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event-flags object was deleted and unregistered
}


/**
 * \brief Set one or more bits in a registered POSIX event-flags object.
 *
 * \details All pending wait conditions are evaluated against the same flag
 *          snapshot before any clear-on-exit bits are removed. Therefore one
 *          Set operation may satisfy multiple waiting threads.
 *
 * \param osal              Opaque pointer to the initialized POSIX OSAL instance.
 * \param eventFlagsHandle  Registered event-flags handle.
 * \param flags             Non-zero bit mask to set.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixEventFlagsSet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          const uint32_t flags)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsSet(%p, %p, 0x%08lX)",
                              osal, (void *)eventFlagsHandle, (unsigned long)flags);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(flags != 0u);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);


    /* Try to find the event-flags handle within the OSAL instance registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid event-flags handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsSet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags handle is not registered
    }

    /* Down-casting of the event-flags handle */
    Template_osalPosixEventFlags_s *const eventFlags =
        (Template_osalPosixEventFlags_s *)eventFlagsHandle;

    /* Lock internally used event flags mutex */
    int rc = pthread_mutex_lock(&eventFlags->mutex);
    if (rc != 0)
    {
        /* This branch is considered as very unlikelly */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_SET_ERR;  // Error: Native event-flags mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsSet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native event-flags mutex locking failed
    }

    /* Set the requested event bits */
    eventFlags->flags |= flags;

    const uint32_t eventSnapshot = eventFlags->flags;
    uint32_t clearMask           = 0u;
    bool wakeRequired            = false;

    /* Evaluate every pending awaiter against the same pre-clear snapshot */
    for (Template_osalPosixEventFlagsAwaiter_s *awaiter = eventFlags->awaiter;
         awaiter != NULL;
         awaiter = awaiter->next)
    {
        if (!awaiter->satisfied &&
            template_osalPosixEventFlagsConditionCheck(eventSnapshot,
                                                       awaiter->flags,
                                                       awaiter->waitAll))
        {
            awaiter->actualFlags = eventSnapshot;
            awaiter->satisfied   = true;
            wakeRequired         = true;

            if (!awaiter->noClear)
            {
                clearMask |= awaiter->flags;
            }
        }
    }

    /* Apply clear-on-exit only after all awaiters have observed the same snapshot */
    eventFlags->flags &= ~clearMask;

    if (wakeRequired)
    {
        rc = pthread_cond_broadcast(&eventFlags->cond);
        if (rc != 0)
        {
            /* Unlock internally used event flags mutex */
            (void)pthread_mutex_unlock(&eventFlags->mutex);

            osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_SET_ERR;  // Error: event-flags awaiter notification failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsSet -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: awaiter notification failed
        }
    }

    /* Unlock internally used event flags mutex */
    rc = pthread_mutex_unlock(&eventFlags->mutex);
    if (rc != 0)
    {
        /* This branch is considered as very unlikelly */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_SET_ERR;  // Error: event-flags mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsSet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags mutex unlocking failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsSet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags were set
}


/**
 * \brief Clear one or more bits in a registered POSIX event-flags object.
 *
 * \param osal              Opaque pointer to the initialized POSIX OSAL instance.
 * \param eventFlagsHandle  Registered event-flags handle.
 * \param flags             Non-zero bit mask to clear.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixEventFlagsClear(void *const osal,
                                                            const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                            const uint32_t flags)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsClear(%p, %p, 0x%08lX)",
                              osal, (void *)eventFlagsHandle, (unsigned long)flags);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(flags != 0u);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);


    /* Try to find the event-flags handle within the OSAL instance registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid event-flags handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsClear -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags handle is not registered
    }

    /* Down-casting of the event-flags handle */
    Template_osalPosixEventFlags_s *const eventFlags =
        (Template_osalPosixEventFlags_s *)eventFlagsHandle;

    /* Lock internally used event flags mutex */
    int rc = pthread_mutex_lock(&eventFlags->mutex);
    if (rc != 0)
    {
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_CLEAR_ERR;  // Error: Native event-flags mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsClear -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags mutex locking failed
    }

    /* Clear the requested event bits */
    eventFlags->flags &= ~flags;

    /* Unlock internally used event flags mutex */
    rc = pthread_mutex_unlock(&eventFlags->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_CLEAR_ERR;  // Error: Native event-flags mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsClear -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags mutex unlocking failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsClear -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags were cleared
}


/**
 * \brief Read the current bits of a registered POSIX event-flags object.
 *
 * \param osal              Opaque pointer to the initialized POSIX OSAL instance.
 * \param eventFlagsHandle  Registered event-flags handle.
 * \param flags             Output pointer receiving the current bit mask.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixEventFlagsGet(void *const osal,
                                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                          uint32_t *const flags)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsGet(%p, %p, %p)",
                              osal, (void *)eventFlagsHandle, (void *)flags);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(flags != NULL);

    /* Clear the output value */
    *flags = 0u;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);


    /* Try to find the event-flags handle within the OSAL instance registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid event-flags handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags handle is not registered
    }

    /* Down-casting of the event-flags handle */
    Template_osalPosixEventFlags_s *const eventFlags =
        (Template_osalPosixEventFlags_s *)eventFlagsHandle;

    /* Lock internally used event flags mutex */
    int rc = pthread_mutex_lock(&eventFlags->mutex);
    if (rc != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native event-flags mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags mutex locking failed
    }

    /* Read the current event-flags snapshot */
    *flags = eventFlags->flags;

    /* Unlock internally used event flags mutex */
    rc = pthread_mutex_unlock(&eventFlags->mutex);
    if (rc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native event-flags mutex unlocking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags mutex unlocking failed
    }

    /* Trace output value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsGet: flags = 0x%08lX",
                              (unsigned long)*flags);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsGet -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event flags were read
}


/**
 * \brief Wait for any or all requested bits in a registered POSIX event-flags object.
 *
 * \details A zero timeout checks the current flag state without blocking.
 *          TEMPLATE_OSAL_INFINITY_TOUT waits indefinitely. Finite waits use one
 *          absolute timeout so spurious wake-ups do not extend the requested wait.
 *          When a Set operation satisfies multiple awaiters, each awaiter receives
 *          the same pre-clear event snapshot.
 *
 * \param osal              Opaque pointer to the initialized POSIX OSAL instance.
 * \param eventFlagsHandle  Registered event-flags handle.
 * \param flags             Non-zero bit mask to wait for.
 * \param options           WAIT_ANY/WAIT_ALL and optional NO_CLEAR behavior.
 * \param timeoutMs         Maximum wait time in milliseconds.
 * \param actualFlags       Output pointer receiving the observed flags snapshot.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixEventFlagsWait(void *const osal,
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
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait(%p, %p, 0x%08lX, 0x%08lX, %u, %p)",
                              osal,
                              (void *)eventFlagsHandle,
                              (unsigned long)flags,
                              (unsigned long)options,
                              (unsigned int)timeoutMs,
                              (void *)actualFlags);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(eventFlagsHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(flags != 0u);
    TEMPLATE_OSAL_POSIX_ASSERT(actualFlags != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(((uint32_t)options & ~validOptions) == 0u);

    /* Clear the output value */
    *actualFlags = 0u;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->eventFlagsHandleFind != NULL);

    /* Try to find the event-flags handle within the OSAL instance registry */
    const size_t eventFlagsId = port->base.ptable->eventFlagsHandleFind(port, eventFlagsHandle);
    if ((eventFlagsId == 0u) ||
        (eventFlagsId > TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid event-flags handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags handle is not registered
    }

    /* Down-casting of the event-flags handle */
    Template_osalPosixEventFlags_s *const eventFlags =
        (Template_osalPosixEventFlags_s *)eventFlagsHandle;

    /* Lock internally used event flags mutex */
    int rc = pthread_mutex_lock(&eventFlags->mutex);
    if (rc != 0)
    {
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ERR;  // Error: Native event-flags mutex locking failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags mutex locking failed
    }

    const bool waitAll = (((uint32_t)options & (uint32_t)TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ALL) != 0u);
    const bool noClear = (((uint32_t)options & (uint32_t)TEMPLATE_OSAL_EVENT_FLAGS_NO_CLEAR) != 0u);

    /* Check whether the wait condition is already satisfied */
    if (template_osalPosixEventFlagsConditionCheck(eventFlags->flags, flags, waitAll))
    {
        *actualFlags = eventFlags->flags;

        if (!noClear)
        {
            eventFlags->flags &= ~flags;
        }

        /* Unlock internally used event flags mutex */
        rc = pthread_mutex_unlock(&eventFlags->mutex);
        if (rc != 0)
        {
            /* This branch is impossible under normal conditions */
            TEMPLATE_OSAL_POSIX_ASSERT(0);
            osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ERR;  // Error: Native event-flags mutex unlocking failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: event-flags mutex unlocking failed
        }

        /* Trace output value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait: actualFlags = 0x%08lX",
                                  (unsigned long)*actualFlags);

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Success: event-flags condition was already satisfied
    }

    /* Return immediately when no blocking time was requested */
    if (timeoutMs == 0u)
    {
        *actualFlags = eventFlags->flags;
        /* Unlock internally used event flags mutex */
        (void)pthread_mutex_unlock(&eventFlags->mutex);
        osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ERR;  // Error: Event-flags condition was not satisfied

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags condition was not immediately satisfied
    }

    struct timespec deadline = {0};
    if ((timeoutMs != TEMPLATE_OSAL_INFINITY_TOUT) &&
        !template_osalPosixRealtimeDeadlineGet(timeoutMs, &deadline))
    {
        /* Unlock internally used event flags mutex */
        (void)pthread_mutex_unlock(&eventFlags->mutex);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Event-flags wait deadline creation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags wait deadline creation failed
    }

    /* Link the caller-owned awaiter while holding the event-flags mutex */
    Template_osalPosixEventFlagsAwaiter_s awaiter =
    {
        .next        = eventFlags->awaiter,
        .flags       = flags,
        .actualFlags = 0u,
        .waitAll     = waitAll,
        .noClear     = noClear,
        .satisfied   = false
    };
    eventFlags->awaiter = &awaiter;

    Template_osalPosixEventFlagsWaitCleanup_s cleanup =
    {
        .eventFlags = eventFlags,
        .awaiter    = &awaiter
    };

    int waitRc   = 0;
    int unlockRc = 0;

    /* Remove the awaiter and unlock the object if the POSIX thread is cancelled */
    pthread_cleanup_push(template_osalPosixEventFlagsWaitCleanup, &cleanup);

    while (!awaiter.satisfied)
    {
        if (timeoutMs == TEMPLATE_OSAL_INFINITY_TOUT)
        {
            waitRc = pthread_cond_wait(&eventFlags->cond, &eventFlags->mutex);
        }
        else
        {
            waitRc = pthread_cond_timedwait(&eventFlags->cond,
                                            &eventFlags->mutex,
                                            &deadline);
        }

        if ((waitRc != 0) &&
            (waitRc != ETIMEDOUT))
        {
            break;
        }

        if (waitRc == ETIMEDOUT)
        {
            break;
        }
    }

    if (awaiter.satisfied)
    {
        *actualFlags = awaiter.actualFlags;
        osalStatus   = TEMPLATE_OSAL_NO_ERR;
    }
    else
    {
        *actualFlags = eventFlags->flags;

        if (waitRc == ETIMEDOUT)
        {
            osalStatus = TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ERR;  // Error: Event-flags wait timed out
        }
        else
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native event-flags wait failed
        }
    }

    /* Unlink the caller-owned awaiter before releasing the event-flags mutex */
    template_osalPosixEventFlagsAwaiterRemove(eventFlags, &awaiter);

    /* Unlock internally used event flags mutex */
    unlockRc = pthread_mutex_unlock(&eventFlags->mutex);
    if (unlockRc != 0)
    {
        /* This branch is impossible under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native event-flags mutex unlocking failed
    }

    pthread_cleanup_pop(0);

    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: event-flags wait did not complete successfully
    }

    /* Trace output value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait: actualFlags = 0x%08lX",
                              (unsigned long)*actualFlags);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixEventFlagsWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: event-flags condition was satisfied
}


/**
 * \brief Check whether an event-flags snapshot satisfies one wait condition.
 *
 * \param currentFlags    Current event-flags snapshot.
 * \param requestedFlags  Requested non-zero event-flags mask.
 * \param waitAll         true to require all requested bits; false to require any bit.
 *
 * \return true when the requested wait condition is satisfied; false otherwise.
 */
static inline bool template_osalPosixEventFlagsConditionCheck(const uint32_t currentFlags,
                                                              const uint32_t requestedFlags,
                                                              const bool waitAll)
{
    TEMPLATE_OSAL_POSIX_ASSERT(requestedFlags != 0u);

    return waitAll
         ? ((currentFlags & requestedFlags) == requestedFlags)
         : ((currentFlags & requestedFlags) != 0u);
}


/**
 * \brief Remove one awaiter descriptor from an event-flags awaiter list.
 *
 * \details The caller shall hold the event-flags mutex while modifying
 *          the awaiter list.
 *
 * \param eventFlags  Event-flags object owning the awaiter list.
 * \param awaiter      Awaiter descriptor to remove.
 */
static void template_osalPosixEventFlagsAwaiterRemove(Template_osalPosixEventFlags_s *const eventFlags,
                                                      Template_osalPosixEventFlagsAwaiter_s *const awaiter)
{
    TEMPLATE_OSAL_POSIX_ASSERT(eventFlags != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(awaiter != NULL);

    Template_osalPosixEventFlagsAwaiter_s **awaiterLink = &eventFlags->awaiter;

    while ((*awaiterLink != NULL) &&
           (*awaiterLink != awaiter))
    {
        awaiterLink = &(*awaiterLink)->next;
    }

    /* The awaiter must remain linked until its wait operation completes */
    TEMPLATE_OSAL_POSIX_ASSERT(*awaiterLink == awaiter);

    if (*awaiterLink == awaiter)
    {
        *awaiterLink  = awaiter->next;
        awaiter->next = NULL;
    }
}


/**
 * \brief Remove a cancelled awaiter and release the event-flags mutex.
 *
 * \details POSIX condition waits reacquire the supplied mutex before executing
 *          cancellation cleanup handlers. The awaiter is therefore removed while
 *          the awaiter list is still protected, then the mutex is released.
 *
 * \param context  Pointer to Template_osalPosixEventFlagsWaitCleanup_s.
 */
static void template_osalPosixEventFlagsWaitCleanup(void *const context)
{
    TEMPLATE_OSAL_POSIX_ASSERT(context != NULL);

    Template_osalPosixEventFlagsWaitCleanup_s *const cleanup =
        (Template_osalPosixEventFlagsWaitCleanup_s *)context;

    TEMPLATE_OSAL_POSIX_ASSERT(cleanup->eventFlags != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(cleanup->awaiter != NULL);

    template_osalPosixEventFlagsAwaiterRemove(cleanup->eventFlags, cleanup->awaiter);

    /* Unlock internally used event flags mutex */
    const int rc = pthread_mutex_unlock(&cleanup->eventFlags->mutex);
    TEMPLATE_OSAL_POSIX_ASSERT(rc == 0);
}

// END EVENT_FLAGS

// BEGIN THREAD
/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a POSIX thread and register it in the OSAL instance.
 *
 * \details The OSAL worker is launched through a pthread-compatible thunk. The
 *          requested stack size is applied through pthread attributes. Generic
 *          OSAL priority levels are mapped to the active POSIX scheduler on a
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadFreeSlotFind != NULL);

    if (!template_osalPosixThreadAttrValidate(&threadAttr))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid thread attributes

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid thread attributes
    }

    /* Clear the output value */
    *threadHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free thread registry slot */
    const size_t threadId = port->base.ptable->threadFreeSlotFind(port);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_THREAD_CREATE_ERR;  // Error: No free thread slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free thread slot
    }

    /* Thread registry slot index */
    const size_t threadIdx = threadId - 1u;

    /* Allocate memory for the thread control block */
    Template_osalPosixThread_s *const thread =
        (Template_osalPosixThread_s *)calloc(1u, sizeof(Template_osalPosixThread_s));
    if (thread == NULL)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_MEM_ALLOCATION_ERR;  // Error: Thread control-block allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread control-block allocation failed
    }

    /* Initialize pthread attributes */
    pthread_attr_t semAttr;
    if (pthread_attr_init(&semAttr) != 0)
    {
        /* Release thread control block */
        free(thread);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_CREATE_ERR;  // Error: Pthread attributes initialization failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread attributes initialization failed
    }

    /* Adjust the requested stack size to the host minimum if required */
    size_t threadStackSize      = threadAttr.stackSize;
    const long minimumStackSize = sysconf(_SC_THREAD_STACK_MIN);
    if ((minimumStackSize > 0L) &&
        (threadStackSize < (size_t)minimumStackSize))
    {
        threadStackSize = (size_t)minimumStackSize;
    }

    if (pthread_attr_setstacksize(&semAttr, threadStackSize) != 0)
    {
        /* Release previously allocated resources */
        (void)pthread_attr_destroy(&semAttr);
        free(thread);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Requested pthread stack size is not supported

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: requested pthread stack size is not supported
    }

    /* Prepare pthread thunk function arguments */
    thread->arg.worker     = threadAttr.worker;
    thread->arg.workerArgs = threadAttr.args;

    /* Register the thread handle before starting the worker */
    port->base.threadObjHandle[threadIdx].attr   = threadAttr;
    port->base.threadObjHandle[threadIdx].handle = (Template_osalThreadHandle_t)thread;

    /* Create the POSIX thread */
    const int rc = pthread_create(&thread->thread,
                                  &semAttr,
                                  template_osalPosixThreadThunk,
                                  (void *)&thread->arg);
    (void)pthread_attr_destroy(&semAttr);

    if (rc != 0)
    {
        port->base.ptable->threadSlotClear(port, threadIdx);
        free(thread);

        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_THREAD_CREATE_ERR;  // Error: Pthread creation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread creation failed
    }

    /* Casring of POSIX thread control block pointer to the generic thread handle type */
    *threadHandle = (Template_osalThreadHandle_t)thread;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Apply the requested thread priority on a best-effort basis */
    int policy                    = SCHED_OTHER;
    struct sched_param schedParam = {0};
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

            /* Apply scheduling parameters */
            const int schedStatus = pthread_setschedparam(thread->thread, policy, &schedParam);
            if (schedStatus != 0)
            {
                TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate: priority mapping skipped (%d)",
                                          schedStatus);
            }
        }
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was created and registered
}


/**
 * \brief Delete a registered POSIX thread synchronously.
 *
 * \details Requests pthread cancellation, joins the POSIX thread and releases
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadSlotClear != NULL);

    /* Try to find the thread handle within the OSAL instance registry */
    const size_t threadId = port->base.ptable->threadHandleFind(port, threadHandle);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid thread handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread handle is not registered
    }

    /* Down-casting of the thread handle */
    Template_osalPosixThread_s *const thread = (Template_osalPosixThread_s *)threadHandle;

    /* Reject self-delete */
    if (pthread_equal(thread->thread, pthread_self()) != 0)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: A thread cannot synchronously delete itself

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: a thread cannot synchronously delete itself
    }

    /* Request thread cancellation */
    const int cancelStatus = pthread_cancel(thread->thread);
    if ((cancelStatus != 0) &&
        (cancelStatus != ESRCH))
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Pthread cancellation request failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread cancellation request failed
    }

    /* Join the POSIX thread */
    const int joinStatus = pthread_join(thread->thread, NULL);
    if (joinStatus != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Pthread join failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: pthread join failed
    }

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the thread handle within the OSAL instance registry */
    const size_t currentThreadId = port->base.ptable->threadHandleFind(port, threadHandle);
    if ((currentThreadId == 0u) ||
        (currentThreadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid thread handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread registry changed unexpectedly during deletion
    }

    /* Clear the registry slot and release thread storage */
    port->base.ptable->threadSlotClear(port, currentThreadId - 1u);
    free(thread);

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was deleted and unregistered
}


/**
 * \brief Suspend a registered POSIX thread.
 *
 * \details Portable pthreads provide no direct suspend primitive with semantics
 *          equivalent to the generic OSAL contract. The operation therefore fails
 *          explicitly instead of reporting success without suspending the thread.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param threadHandle  Registered thread handle.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR when portable thread suspension is unavailable.
 */
static Template_osalErr_e template_osalPosixThreadSuspend(void *const osal,
                                                          const Template_osalThreadHandle_t threadHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadSuspend(%p, %p)", osal, (void *)threadHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(threadHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind != NULL);

    /* Try to find the thread handle within the OSAL instance registry */
    const size_t threadId = port->base.ptable->threadHandleFind(port, threadHandle);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid thread handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadSuspend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread handle is not registered
    }

    osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Portable POSIX thread suspend is not supported

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadSuspend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Error: portable POSIX thread suspend is not supported
}


/**
 * \brief Resume a registered POSIX thread.
 *
 * \details Portable pthreads provide no direct resume primitive with semantics
 *          equivalent to the generic OSAL contract. The operation therefore fails
 *          explicitly instead of reporting success without resuming the thread.
 *
 * \param osal          Opaque pointer to the initialized POSIX OSAL instance.
 * \param threadHandle  Registered thread handle.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR when portable thread resumption is unavailable.
 */
static Template_osalErr_e template_osalPosixThreadResume(void *const osal,
                                                         const Template_osalThreadHandle_t threadHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadResume(%p, %p)", osal, (void *)threadHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(threadHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->threadHandleFind != NULL);

    /* Try to find the thread handle within the OSAL instance registry */
    const size_t threadId = port->base.ptable->threadHandleFind(port, threadHandle);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid thread handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadResume -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: thread handle is not registered
    }

    osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Portable POSIX thread resume is not supported

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadResume -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Error: portable POSIX thread resume is not supported
}


/**
 * \brief Yield execution of the calling POSIX thread.
 *
 * \param osal  Opaque pointer to the initialized POSIX OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixThreadYield(void *const osal)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadYield(%p)", osal);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));

    /* Yield execution to the scheduler */
    if (sched_yield() != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: POSIX scheduler yield failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadYield -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX scheduler yield failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadYield -> %d",
                              (int)osalStatus);

    return osalStatus;  // Exit: Success: execution was yielded
}


/**
 * \brief Delay the calling POSIX thread.
 *
 * \details A zero delay returns immediately without blocking the calling thread.
 *          A finite non-zero delay blocks the calling thread for the requested
 *          interval. TEMPLATE_OSAL_INFINITY_TOUT is not accepted by this
 *          function.
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
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay(%p, %u)",
                              osal, (unsigned int)delayMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));

    /* Reject an infinite delay */
    if (delayMs == TEMPLATE_OSAL_INFINITY_TOUT)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Infinite thread delay is not permitted

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: delay duration must be finite
    }

    /* Return immediately for a zero-duration delay */
    if (delayMs == 0u)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Success: no delay was requested
    }

    /* Build an absolute monotonic deadline to avoid accumulated EINTR drift */
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Monotonic clock query failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: monotonic clock query failed
    }

    /* Calculate the deadline time */
    template_osalPosixTimespecAddMs(&deadline, delayMs);

    int rc = 0;

    do
    {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    } while(rc == EINTR);

    if (rc != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: POSIX thread delay failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: POSIX thread delay failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelay -> %d",
                              (int)osalStatus);

    return osalStatus;  // Exit: Success: thread delay completed
}


/**
 * \brief Delay the calling POSIX thread until the next periodic wake-up point.
 *
 * \details A zero period returns immediately without blocking the calling thread
 *          or modifying the wake-up reference. A finite non-zero period advances
 *          the caller-owned wake reference arithmetically and uses CLOCK_MONOTONIC
 *          with TIMER_ABSTIME for the actual wait so repeated periods do not
 *          accumulate drift. TEMPLATE_OSAL_INFINITY_TOUT is not accepted by this
 *          function.
 *
 * \param osal                Opaque pointer to the initialized POSIX OSAL instance.
 * \param previousWakeTimeMs  In/out periodic wake reference in OSAL milliseconds.
 * \param periodMs            Period in milliseconds.
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

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));

    /* Reject an infinite period */
    if (periodMs == TEMPLATE_OSAL_INFINITY_TOUT)
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Infinite thread delay period is not permiited

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: delay period must be finite
    }

    /* Return immediately for a zero-duration period */
    if (periodMs == 0u)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Success: no delay was requested
    }

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Monotonic clock query failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d",
                                  (int)osalStatus);

        return osalStatus;  // Exit: Error: monotonic clock query failed
    }

    const uint64_t nowMs64 = ((uint64_t)now.tv_sec * 1000u) +
                             ((uint64_t)now.tv_nsec / 1000000u);
    const Template_osalTimeMs_t nowMs          = (Template_osalTimeMs_t)nowMs64;
    const Template_osalTimeMs_t nextWakeTimeMs = *previousWakeTimeMs + periodMs;
    const int32_t waitMs                       = (int32_t)(nextWakeTimeMs - nowMs);

    if (waitMs > 0)
    {
        struct timespec deadline = now;
        template_osalPosixTimespecAddMs(&deadline, (Template_osalTimeMs_t)waitMs);

        int rc = 0;

        do
        {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        } while(rc == EINTR);

        if (rc != 0)
        {
            osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Periodic POSIX thread delay failed

            /* Trace returned value */
            TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d",
                                      (int)osalStatus);

            return osalStatus;  // Exit: Error: periodic POSIX thread delay failed
        }
    }

    /* Advance the caller-owned periodic reference even when the deadline was already due */
    *previousWakeTimeMs = nextWakeTimeMs;

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadDelayUntil -> %d",
                              (int)osalStatus);

    return osalStatus;  // Exit: Success: periodic delay completed
}


/**
 * \brief Terminate the calling POSIX thread.
 *
 * \details The current registered thread slot is cleared before POSIX thread
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

    /* Validate input args */
    if (osal == NULL)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> invalid OSAL");

        return;  // Exit: Error: invalid OSAL instance
    }

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */

    if (!template_osalPosixIsValid(port) ||
        (port->base.ptable == NULL) ||
        (port->base.ptable->threadSlotClear == NULL))
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> invalid backend state");

        return;  // Exit: Error: backend invariant is not satisfied
    }

    /* Lock */
    Template_osalErr_e osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> resource lock failed: %d",
                                  (int)osalStatus);

        return;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the current thread within the OSAL instance registry */
    const pthread_t currentThread          = pthread_self();
    Template_osalPosixThread_s *currentTcb = NULL;
    size_t currentThreadIdx                = TEMPLATE_OSAL_THREAD_SLOTS_NUM;

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

    /* Clear the current thread registry slot */
    port->base.ptable->threadSlotClear(port, currentThreadIdx);

    /* Unlock */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_POSIX_ASSERT(0);
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadExit -> resource unlock failed: %d",
                                  (int)osalStatus);

        return;  // Exit: Error: resource mutex release failed
    }

    /* Release thread bookkeeping before terminating the calling pthread */
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

    /* Trace returned value */
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
    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadThunk(%p)", context);

    /* Context should be provided for redirecting to OSAL instance thread worker */
    TEMPLATE_OSAL_POSIX_ASSERT(context != NULL);

    /* Extract thread worker from the passed contecxt */
    Template_osalPosixThreadArg_s *const arg = (Template_osalPosixThreadArg_s *)context;
    TEMPLATE_OSAL_POSIX_ASSERT(arg->worker != NULL);

    /* Run the component thread worker */
    arg->worker(arg->workerArgs);

    /* Keep the joinable thread registered after a natural worker return */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixThreadThunk -> natural worker return");

    return NULL;  // Exit: Success: worker returned naturally
}

// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ------------------------*/

/**
 * \brief Report that system-level critical sections are unavailable on the POSIX backend.
 *
 * \details Portable POSIX user space cannot provide the interrupt-masking semantics
 *          represented by the generic deprecated critical-section primitive.
 *
 * \param osal  Opaque pointer to the initialized POSIX OSAL instance.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR because the operation is unsupported.
 */
static Template_osalErr_e template_osalPosixCriticalSectionEnter(void *const osal)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixCriticalSectionEnter(%p)", osal);

    /* Validate input args and POSIX backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));

    osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: System-level critical sections are not supported by the POSIX backend

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixCriticalSectionEnter -> %d",
                              (int)osalStatus);

    return osalStatus;  // Exit: Error: system-level critical sections are not supported
}


/**
 * \brief Report that system-level critical sections are unavailable on the POSIX backend.
 *
 * \details Portable POSIX user space cannot provide the interrupt-masking semantics
 *          represented by the generic deprecated critical-section primitive.
 *
 * \param osal  Opaque pointer to the initialized POSIX OSAL instance.
 *
 * \return TEMPLATE_OSAL_PORT_SPECIFIC_ERR because the operation is unsupported.
 */
static Template_osalErr_e template_osalPosixCriticalSectionExit(void *const osal)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixCriticalSectionExit(%p)", osal);

    /* Validate input args and POSIX backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));

    osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: System-level critical sections are not supported by the POSIX backend

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixCriticalSectionExit -> %d",
                              (int)osalStatus);

    return osalStatus;  // Exit: Error: system-level critical sections are not supported
}
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------------- Software timers -------------------------*/

/**
 * \brief Create a POSIX software timer and register it in the OSAL instance.
 *
 * \details The native timer uses CLOCK_MONOTONIC and SIGEV_THREAD notification.
 *          The timer is created disarmed. Start or Reset arms it using the
 *          configured period and auto-reload mode.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Output pointer receiving the software-timer handle.
 * \param timerAttr    Software-timer attributes.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerCreate(void *const osal,
                                                                Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                Template_osalSoftwareTimerAttr_s timerAttr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate(%p, %p, {%s, %p, %p, %d, %u})",
                              osal,
                              (void *)timerHandle,
                              (timerAttr.name != NULL) ? timerAttr.name : "(null)",
                              timerAttr.timerParam,
                              (void *)(uintptr_t)timerAttr.timerExpiredCb,
                              (int)timerAttr.autoReload,
                              (unsigned int)timerAttr.periodMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerAttr.timerExpiredCb != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerAttr.periodMs != 0u);

    /* Clear the output value */
    *timerHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->softwareTimerFreeSlotFind != NULL);

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free software-timer registry slot */
    const size_t timerId = port->base.ptable->softwareTimerFreeSlotFind(port);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_CREATE_ERR;  // Error: No free software-timer slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free software-timer slot
    }

    /* Allocate the software-timer control block */
    Template_osalPosixSoftwareTimer_s *const timer =
        (Template_osalPosixSoftwareTimer_s *)calloc(1u, sizeof(Template_osalPosixSoftwareTimer_s));
    if (timer == NULL)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_MEM_ALLOCATION_ERR;  // Error: Software-timer control-block allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: software-timer control-block allocation failed
    }

    Template_osalSoftwareTimer_s *const timerObj = &port->base.softwareTimerObj[timerId - 1u];
    timerObj->attr = timerAttr;

    struct sigevent event = {0};
    event.sigev_notify            = SIGEV_THREAD;
    event.sigev_value.sival_ptr   = timerObj;
    event.sigev_notify_function   = template_osalPosixSoftwareTimerCallback;
    event.sigev_notify_attributes = NULL;

    /* Create the native POSIX timer in the disarmed state */
    if (timer_create(CLOCK_MONOTONIC, &event, &timer->nativeTimer) != 0)
    {
        const int createErr = errno;

        timerObj->attr.name           = NULL;
        timerObj->attr.timerParam     = NULL;
        timerObj->attr.timerExpiredCb = NULL;
        timerObj->attr.autoReload     = false;
        timerObj->attr.periodMs       = 0u;
        free(timer);
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = ((createErr == EAGAIN) ||
                      (createErr == ENOMEM)) ?
                     TEMPLATE_OSAL_SOFTWARE_TIMER_MEM_ALLOCATION_ERR :
                     TEMPLATE_OSAL_SOFTWARE_TIMER_CREATE_ERR;  // Error: Native POSIX timer creation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native POSIX timer creation failed
    }

    /* Register the software-timer handle */
    timerObj->handle = (Template_osalSoftwareTimerHandle_t)timer;
    *timerHandle     = (Template_osalSoftwareTimerHandle_t)timer;

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was created and registered
}


/**
 * \brief Delete a registered POSIX software timer.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Registered software-timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerDelete(void *const osal,
                                                                const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerDelete(%p, %p)",
                              osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the software-timer handle within the OSAL instance registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid software-timer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: software-timer handle is not registered
    }

    /* Down-casting of the software-timer handle */
    Template_osalPosixSoftwareTimer_s *const timer =
        (Template_osalPosixSoftwareTimer_s *)timerHandle;

    /* Delete the native POSIX timer */
    if (timer_delete(timer->nativeTimer) != 0)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Native POSIX timer deletion failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native POSIX timer deletion failed
    }

    Template_osalSoftwareTimer_s *const timerObj = &port->base.softwareTimerObj[timerId - 1u];
    timerObj->handle              = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;
    timerObj->attr.name           = NULL;
    timerObj->attr.timerParam     = NULL;
    timerObj->attr.timerExpiredCb = NULL;
    timerObj->attr.autoReload     = false;
    timerObj->attr.periodMs       = 0u;

    free(timer);

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was deleted and unregistered
}


/**
 * \brief Start a registered POSIX software timer.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Registered software-timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStart(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStart(%p, %p)",
                              osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Try to find the software-timer handle within the OSAL instance registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid software-timer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStart -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: software-timer handle is not registered
    }

    /* Down-casting of the software-timer handle */
    Template_osalPosixSoftwareTimer_s *const timer =
        (Template_osalPosixSoftwareTimer_s *)timerHandle;
    Template_osalSoftwareTimer_s *const timerObj = &port->base.softwareTimerObj[timerId - 1u];

    /* Arm the native POSIX timer */
    if (template_osalPosixSoftwareTimerArm(timer, &timerObj->attr) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_START_ERR;  // Error: Native POSIX timer start failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStart -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native POSIX timer start failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStart -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was started
}


/**
 * \brief Stop a registered POSIX software timer.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Registered software-timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerStop(void *const osal,
                                                              const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStop(%p, %p)",
                              osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Try to find the software-timer handle within the OSAL instance registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid software-timer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStop -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: software-timer handle is not registered
    }

    /* Down-casting of the software-timer handle */
    Template_osalPosixSoftwareTimer_s *const timer =
        (Template_osalPosixSoftwareTimer_s *)timerHandle;

    /* Disarm the native POSIX timer */
    if (template_osalPosixSoftwareTimerDisarm(timer) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_STOP_ERR;  // Error: Native POSIX timer stop failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStop -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native POSIX timer stop failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerStop -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was stopped
}


/**
 * \brief Reset a registered POSIX software timer.
 *
 * \details The configured period is restarted from the time of this call.
 *
 * \param osal         Opaque pointer to the initialized POSIX OSAL instance.
 * \param timerHandle  Registered software-timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalPosixSoftwareTimerReset(void *const osal,
                                                               const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerReset(%p, %p)",
                              osal, (void *)timerHandle);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerHandle != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->softwareTimerHandleFind != NULL);

    /* Try to find the software-timer handle within the OSAL instance registry */
    const size_t timerId = port->base.ptable->softwareTimerHandleFind(port, timerHandle);
    if ((timerId == 0u) ||
        (timerId > TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Invalid software-timer handle

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: software-timer handle is not registered
    }

    /* Down-casting of the software-timer handle */
    Template_osalPosixSoftwareTimer_s *const timer =
        (Template_osalPosixSoftwareTimer_s *)timerHandle;
    Template_osalSoftwareTimer_s *const timerObj = &port->base.softwareTimerObj[timerId - 1u];

    /* Restart the native POSIX timer period */
    if (template_osalPosixSoftwareTimerArm(timer, &timerObj->attr) != 0)
    {
        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_RESET_ERR;  // Error: Native POSIX timer reset failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native POSIX timer reset failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerReset -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: software timer was reset
}


/**
 * \brief Arm a native POSIX software timer using the configured OSAL period.
 *
 * \param timer      POSIX software-timer control block.
 * \param timerAttr  Software-timer attributes.
 *
 * \return Zero on success; otherwise -1 with errno preserved from timer_settime().
 */
static inline int template_osalPosixSoftwareTimerArm(const Template_osalPosixSoftwareTimer_s *const timer,
                                                     const Template_osalSoftwareTimerAttr_s *const timerAttr)
{
    TEMPLATE_OSAL_POSIX_ASSERT(timer != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerAttr != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(timerAttr->periodMs != 0u);

    struct itimerspec timerSpec = {0};

    timerSpec.it_value.tv_sec  = (time_t)(timerAttr->periodMs / 1000u);
    timerSpec.it_value.tv_nsec = (long)((timerAttr->periodMs % 1000u) * 1000000u);

    if (timerAttr->autoReload)
    {
        timerSpec.it_interval = timerSpec.it_value;
    }

    return timer_settime(timer->nativeTimer, 0, &timerSpec, NULL);
}


/**
 * \brief Disarm a native POSIX software timer.
 *
 * \param timer  POSIX software-timer control block.
 *
 * \return Zero on success; otherwise -1 with errno preserved from timer_settime().
 */
static inline int template_osalPosixSoftwareTimerDisarm(const Template_osalPosixSoftwareTimer_s *const timer)
{
    TEMPLATE_OSAL_POSIX_ASSERT(timer != NULL);

    const struct itimerspec timerSpec = {0};

    return timer_settime(timer->nativeTimer, 0, &timerSpec, NULL);
}


/**
 * \brief Dispatch a POSIX SIGEV_THREAD notification to the component callback.
 *
 * \param value  POSIX notification value carrying the generic software-timer object.
 */
static void template_osalPosixSoftwareTimerCallback(union sigval value)
{
    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCallback(%p)", value.sival_ptr);

    Template_osalSoftwareTimer_s *const timerObj =
        (Template_osalSoftwareTimer_s *)value.sival_ptr;

    TEMPLATE_OSAL_POSIX_ASSERT(timerObj != NULL);
    if (timerObj == NULL)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCallback -> invalid timer object");

        return;  // Exit: Error: timer callback object is invalid
    }

    TEMPLATE_OSAL_POSIX_ASSERT(timerObj->attr.timerExpiredCb != NULL);
    if (timerObj->attr.timerExpiredCb == NULL)
    {
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCallback -> invalid timer state");

        return;  // Exit: Error: timer callback state is invalid
    }

    timerObj->attr.timerExpiredCb(timerObj->attr.timerParam);

    /* Trace returned value */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixSoftwareTimerCallback -> ok");
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
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixTimeMsGet(%p, %p)",
                              osal, (void *)osTimeMs);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(osTimeMs != NULL);

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(osal));
    (void)osal;

    /* Get the current monotonic time */
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        /* This branch is considered as very unlikely */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Error: Monotonic clock read failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixTimeMsGet -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: monotonic clock read failed
    }

    const uint64_t timeMs = ((uint64_t)now.tv_sec * 1000u) +
                            ((uint64_t)now.tv_nsec / 1000000u);
    *osTimeMs = (Template_osalTimeMs_t)timeMs;

    /* Trace returned value */
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

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc(%p, %lu, %p)",
                              osal, (unsigned long)size, (void *)memPtr);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(memPtr != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(size != 0u);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->memFreeSlotFind != NULL);

    /* Clear the output value */
    *memPtr = NULL;

    /* Acquire the resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free memory registry slot */
    const size_t memoryId = port->base.ptable->memFreeSlotFind(port);
    if ((memoryId == 0u) ||
        (memoryId > TEMPLATE_OSAL_MEM_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_MEM_ALLOCATION_ERR;  // Error: No free memory registry slot

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free memory registry slot
    }

    /* Allocate memory */
    void *const allocatedPtr = malloc(size);
    if (allocatedPtr == NULL)
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_MEM_ALLOCATION_ERR;  // Error: Host heap allocation failed

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: host heap allocation failed
    }

    /* Register the memory pointer */
    port->base.memPtr[memoryId - 1u] = allocatedPtr;
    *memPtr                          = allocatedPtr;

    /* Release the resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemAlloc -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
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

    /* Trace input args */
    TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree(%p, %p)", osal, memPtr);

    /* Validate input args */
    TEMPLATE_OSAL_POSIX_ASSERT(osal != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(memPtr != NULL);

    /* Down-casting of the OSAL handle */
    Template_osalPosix_s *const port = (Template_osalPosix_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_POSIX_ASSERT(template_osalPosixIsValid(port));
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_POSIX_ASSERT(port->base.ptable->memPtrFind != NULL);

    /* Lock resource mutex */
    osalStatus = template_osalPosixResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Try to find the memory pointer within the OSAL instance registry */
    const size_t memoryId = port->base.ptable->memPtrFind(port, memPtr);
    if ((memoryId == 0u) ||
        (memoryId > TEMPLATE_OSAL_MEM_SLOTS_NUM))
    {
        /* Unlock resource mutex */
        (void)template_osalPosixResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Error: Memory pointer is not registered

        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: memory pointer is not registered
    }

    /* Clear the registry slot and release the memory block */
    port->base.memPtr[memoryId - 1u] = NULL;
    free(memPtr);

    /* Unlock resource mutex */
    osalStatus = template_osalPosixResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_POSIX_TRACE("template_osalPosixMemFree -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
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

    return(osalPosix->validFlag &&
           (osalPosix->base.vtable == &template_osalPosixVtable));   // Exit: Success: backend validity returned
}


/*-------------------------- Resource synchronization ---------------------*/

/**
 * \brief Acquire the backend-owned POSIX resource mutex.
 *
 * \details This POSIX mutex protects OSAL registry updates only. It is not a
 *          generic Template_osalMutexHandle_t and consumes no user mutex slot.
 *
 * \param osalPosix  POSIX OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static inline Template_osalErr_e template_osalPosixResourceLock(Template_osalPosix_s *const osalPosix)
{
    TEMPLATE_OSAL_POSIX_ASSERT(osalPosix != NULL);

    /* Lock internal OSAL insatnce resource mutex */
    if (pthread_mutex_lock(&osalPosix->resourceMutex) != 0)
    {
        /* This branch is unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX resource mutex lock failed
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

    /* Unlock internal OSAL insatnce resource mutex */
    if (pthread_mutex_unlock(&osalPosix->resourceMutex) != 0)
    {
        /* This branch is unlikely under normal conditions */
        TEMPLATE_OSAL_POSIX_ASSERT(0);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: POSIX resource mutex unlock failed
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

    timeSpec->tv_sec  += (time_t)(timeMs / 1000u);
    timeSpec->tv_nsec += (long)((timeMs % 1000u) * 1000000u);

    if (timeSpec->tv_nsec >= 1000000000L)
    {
        timeSpec->tv_sec  += (time_t)(timeSpec->tv_nsec / 1000000000L);
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
 * \brief Wait on a POSIX semaphore using the generic OSAL timeout semantics.
 *
 * \details The helper retries waits interrupted by signals. Immediate waits use
 *          sem_trywait(), infinite waits use sem_wait(), and finite waits use
 *          sem_timedwait() with one absolute CLOCK_REALTIME deadline. Queue and
 *          generic semaphore objects use this helper independently; POSIX sem_t
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
        } while((result != 0) &&
                (errno == EINTR));

        return result;  // Exit: Success/Error: immediate POSIX semaphore result returned
    }

    if (timeoutMs == TEMPLATE_OSAL_INFINITY_TOUT)
    {
        int result = 0;

        do
        {
            result = sem_wait(semaphore);
        } while((result != 0) &&
                (errno == EINTR));

        return result;  // Exit: Success/Error: infinite POSIX semaphore result returned
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
    } while((result != 0) &&
            (errno == EINTR));

    return result;  // Exit: Success/Error: timed POSIX semaphore result returned
}
