/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef TEMPLATE_OSAL_H_
#define TEMPLATE_OSAL_H_

#ifdef __cplusplus
    extern "C" {
#endif

/*================================================================[INCLUDE]=================================================*/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*===========================================================[MACRO DEFINITIONS]============================================*/

/**
 * \brief Stringization helper macro
 */
#define TEMPLATE_OSAL_STR2(x)    #x
#define TEMPLATE_OSAL_STR(x)     TEMPLATE_OSAL_STR2(x)

/* Include config file if it is defined at compilation time */
#ifdef TEMPLATE_CONFIG_FILE
    #include TEMPLATE_OSAL_STR(TEMPLATE_CONFIG_FILE)
#endif

// BEGIN QUEUE
/**
 * \brief Template OSAL queue slots number.
 */
#ifndef TEMPLATE_OSAL_QUEUE_SLOTS_NUM
    #define TEMPLATE_OSAL_QUEUE_SLOTS_NUM    (2u)
#endif
// END QUEUE

// BEGIN STREAM_BUFFER
/**
 * \brief Template OSAL stream buffer slots number.
 */
#ifndef TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM
    #define TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM    (2u)
#endif
// END STREAM_BUFFER

// BEGIN LOCK
/**
 * \brief Template OSAL lock objects number.
 */
#ifndef TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM
    #define TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM    (2u)
#endif
// END LOCK

// BEGIN SEMAPHORE
/**
 * \brief Template OSAL counting semaphore slots number.
 */
#ifndef TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM
    #define TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM    (2u)
#endif
// END SEMAPHORE

// BEGIN THREAD
/**
 * \brief Template OSAL threads number.
 */
#ifndef TEMPLATE_OSAL_THREAD_SLOTS_NUM
    #define TEMPLATE_OSAL_THREAD_SLOTS_NUM    (2u)
#endif
// END THREAD

// BEGIN SOFTWARE_TIMER
/**
 * \brief Template OSAL software timer slots number.
 */
#ifndef TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM
    #define TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM    (2u)
#endif
// END SOFTWARE_TIMER

// BEGIN MEMORY
/**
 * \brief Template OSAL memory registry slots number.
 */
#ifndef TEMPLATE_OSAL_MEM_SLOTS_NUM
    #define TEMPLATE_OSAL_MEM_SLOTS_NUM    (2u)
#endif
// END MEMORY

/**
 * \brief Template indefinite timeout definition used by timeout-aware primitives.
 */
#define TEMPLATE_OSAL_INFINITY_TOUT    ((Template_osalTimeMs_t)-1)

/**
 * \def    TEMPLATE_OSAL_OBJ_HANDLE_INVALID
 * \brief  Sentinel value for an invalid/empty OSAL object handle.
 *
 * \details Used to denote an uninitialized or released handle in registries
 *          (queues, stream buffers, locks, semaphores, timers, threads and memory). This equals NULL by design and is checked
 *          against before any dereference or registry lookup.
 */
#ifndef TEMPLATE_OSAL_OBJ_HANDLE_INVALID
    #define TEMPLATE_OSAL_OBJ_HANDLE_INVALID    (NULL)
#endif


/*========================================================[DATA TYPES DEFINITIONS]==========================================*/

/**
 * \enum    Template_osalErr_e
 * \brief   Error codes for the Template Operating System Abstraction Layer (OSAL).
 * \details These codes reflect failures in OS services (threads, queues, locks, timing, memory).
 */
typedef enum
{
    TEMPLATE_OSAL_NO_ERR = 0x00,                  //!< No error occurred; operation was successful.
    TEMPLATE_OSAL_INVALID_ARGS_ERR,               //!< Invalid arguments passed to an OSAL function.
    TEMPLATE_OSAL_NOT_INIT_ERR,                   //!< OSAL object/service not properly initialized.
    TEMPLATE_OSAL_CALL_FROM_ISR_ERR,              //!< Function incorrectly invoked from an ISR.

    // BEGIN QUEUE
    TEMPLATE_OSAL_QUEUE_CREATE_ERR,               //!< Failed to create a queue (resources).
    TEMPLATE_OSAL_QUEUE_MEM_ALLOCATION_ERR,       //!< Memory allocation failure during queue creation.
    TEMPLATE_OSAL_QUEUE_OVERFLOW_ERR,             //!< Enqueue into a full queue (overflow).
    TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR,             //!< Dequeue attempted on an empty queue.
    TEMPLATE_OSAL_QUEUE_IS_FULL_ERR,              //!< Predicate/operation indicates full queue.
    // END QUEUE

    // BEGIN STREAM_BUFFER
    TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR,          //!< Failed to create a stream buffer or reserve a registry slot.
    TEMPLATE_OSAL_STREAM_BUFFER_MEM_ALLOCATION_ERR,  //!< Backend memory allocation failed during stream buffer creation.
    TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR,        //!< No data was available before receive completed.
    TEMPLATE_OSAL_STREAM_BUFFER_IS_FULL_ERR,         //!< No bytes could be written because no capacity was available.
    TEMPLATE_OSAL_STREAM_BUFFER_RESET_ERR,           //!< Failed to reset a stream buffer to its empty state.
    // END STREAM_BUFFER

    // BEGIN LOCK
    TEMPLATE_OSAL_LOCK_OBJ_CREATE_ERR,            //!< Failed to create a lock object (mutex/semaphore).
    TEMPLATE_OSAL_LOCK_OBJ_MEM_ALLOCATION_ERR,    //!< Memory allocation failure for lock object.
    // END LOCK

    // BEGIN SEMAPHORE
    TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR,              //!< Failed to create a counting semaphore or reserve a registry slot.
    TEMPLATE_OSAL_SEMAPHORE_MEM_ALLOCATION_ERR,      //!< Backend memory allocation failed during semaphore creation.
    TEMPLATE_OSAL_SEMAPHORE_ACQUIRE_ERR,             //!< Counting semaphore could not be acquired.
    TEMPLATE_OSAL_SEMAPHORE_RELEASE_ERR,             //!< Counting semaphore could not be released.
    // END SEMAPHORE

    // BEGIN THREAD
    TEMPLATE_OSAL_THREAD_CREATE_ERR,              //!< Failed to create a thread.
    TEMPLATE_OSAL_THREAD_MEM_ALLOCATION_ERR,      //!< Memory allocation failure during thread creation.
    // END THREAD

    // BEGIN SOFTWARE_TIMER
    TEMPLATE_OSAL_SOFTWARE_TIMER_CREATE_ERR,         //!< Failed to create a software timer or reserve a registry slot.
    TEMPLATE_OSAL_SOFTWARE_TIMER_MEM_ALLOCATION_ERR, //!< Backend memory allocation failed during software timer creation.
    TEMPLATE_OSAL_SOFTWARE_TIMER_START_ERR,          //!< Failed to start a software timer.
    TEMPLATE_OSAL_SOFTWARE_TIMER_STOP_ERR,           //!< Failed to stop a software timer.
    TEMPLATE_OSAL_SOFTWARE_TIMER_RESET_ERR,          //!< Failed to reset/restart a software timer period.
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    TEMPLATE_OSAL_MEM_ALLOCATION_ERR,             //!< Backend failed to allocate memory.
    // END MEMORY

    TEMPLATE_OSAL_PORT_SPECIFIC_ERR               //!< Port/RTOS-specific error.
} Template_osalErr_e;

/**
 * \brief Template OSAL time in milliseconds.
 */
typedef uint32_t Template_osalTimeMs_t;

// BEGIN QUEUE
/**
 * \brief Template OSAL Queue handle type definition.
 */
typedef void *Template_osalQueueHandle_t;
// END QUEUE

// BEGIN STREAM_BUFFER
/**
 * \brief Template OSAL stream buffer handle type definition.
 */
typedef void *Template_osalStreamBufferHandle_t;
// END STREAM_BUFFER

// BEGIN LOCK
/**
 * \brief Template OSAL lock object type definition.
 */
typedef void *Template_osalLockObjHandle_t;
// END LOCK

// BEGIN SEMAPHORE
/**
 * \brief Template OSAL counting semaphore handle type definition.
 */
typedef void *Template_osalSemaphoreHandle_t;

/**
 * \brief Template OSAL counting semaphore counter type.
 */
typedef uint32_t Template_osalSemaphoreCount_t;
// END SEMAPHORE

// BEGIN THREAD
/**
 * \brief Template OSAL run-time Thread handle type definition.
 */
typedef void *Template_osalThreadHandle_t;
// END THREAD

// BEGIN MEMORY
/**
 * \brief Template OSAL memory handle type definition.
 */
typedef void *Template_osalMemHandle_t;
// END MEMORY

// BEGIN SOFTWARE_TIMER
/**
 * \brief Template OSAL software timer handle type definition.
 */
typedef void *Template_osalSoftwareTimerHandle_t;

/**
 * \brief Software timer expiration callback.
 */
typedef void (*Template_osalSoftwareTimerExpiredCb_f)(void *const timerParam);

/**
 * \brief Template OSAL software timer configuration.
 */
typedef struct
{
    const char                            *name;            /*!< Optional timer name. */
    void                                  *timerParam;      /*!< User parameter passed to callback; may be NULL. */
    Template_osalSoftwareTimerExpiredCb_f timerExpiredCb;  /*!< Expiration callback; must not be NULL. */
    bool                                   autoReload;       /*!< true = periodic, false = one-shot. */
    Template_osalTimeMs_t                  periodMs;         /*!< Timer period in milliseconds; must be non-zero. */
} Template_osalSoftwareTimerCfg_s;

/**
 * \brief Template OSAL software timer registry object.
 */
typedef struct
{
    Template_osalSoftwareTimerCfg_s    cfg;     /*!< Creation configuration snapshot. */
    Template_osalSoftwareTimerHandle_t handle;  /*!< RTOS-native timer handle. */
} Template_osalSoftwareTimer_s;
// END SOFTWARE_TIMER

// BEGIN THREAD
/**
 * \typedef  Template_osalThreadWorker_f
 * \brief    Function prototype for the Template OSAL thread worker.
 * \param   args  Parameter assigned in Template_osalThreadCfg_s on thread
 *                creation and passed to a thread worker as an arg
 */
typedef void (*Template_osalThreadWorker_f)(void *const args);

/**
 * \enum    Template_osalThreadPrio_e
 * \brief   Required thread priority levels for OSAL implementations.
 * \warning TEMPLATE_OSAL_THREAD_PRIORITY_THE_LAST_ONE is not a valid runtime priority.
 */
typedef enum
{
    TEMPLATE_OSAL_THREAD_PRIORITY_LOW = 0,        //!< Background tasks.
    TEMPLATE_OSAL_THREAD_PRIORITY_MIDDLE,         //!< Standard operational tasks.
    TEMPLATE_OSAL_THREAD_PRIORITY_HIGH,           //!< Time-sensitive tasks.
    TEMPLATE_OSAL_THREAD_PRIORITY_ULTRA,          //!< Critical real-time tasks.
    TEMPLATE_OSAL_THREAD_PRIORITY_THE_LAST_ONE    //!< For array sizing only.
} Template_osalThreadPrio_e;

/**
 * \brief Template OSAL thread parameters structure.
 */
typedef struct
{
    Template_osalThreadWorker_f worker;       /*!< Worker entry function. */
    const char                  *name;           /*!< Optional thread name.  */
    size_t                      stackSize;       /*!< Expressed in bytes */
    void                        *args;           /*!< Worker arg parameter (passed as arg to worker). */
    Template_osalThreadPrio_e   prio;         /*!< Thread priority.*/
} Template_osalThreadCfg_s;

/**
 * \brief Template OSAL thread object structure.
 */
typedef struct
{
    Template_osalThreadCfg_s    cfg;      /*!< Creation config (snapshot). */
    Template_osalThreadHandle_t handle;   /*!< RTOS-native handle.         */
} Template_osalThread_s;
// END THREAD

/**
 * \struct  Template_osalVtable_s
 * \brief   OS Abstraction Layer (OSAL) vtable methods for a specific RTOS port.
 * \details Function pointers for OS-specific operations: queues, locks, threads, time, memory,
 *          plus a state predicate. All pointers must be assigned by the port.
 */
typedef struct
{
    // BEGIN QUEUE
    /*------------------------------------ Queues ------------------------------------*/

    /**
     * \brief  Creates a message queue.
     * \param  osal           OSAL instance.
     * \param  queueItemSize  Size of a single queue item in bytes.
     * \param  queueDepth     Maximum number of items the queue can hold.
     * \param  queueHandle    Pointer to store the created queue handle.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*queueCreate)(void * const osal,
                                      const size_t queueItemSize,
                                      const size_t queueDepth,
                                      Template_osalQueueHandle_t * const queueHandle);

    /**
     * \brief  Deletes a message queue.
     * \param  osal         OSAL instance.
     * \param  queueHandle  Handle to the queue to be deleted.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*queueDelete)(void * const osal,
                                      const Template_osalQueueHandle_t queueHandle);

    /**
     * \brief  Inserts an item into a queue.
     * \param  osal          OSAL instance.
     * \param  queueHandle   Handle to the target queue.
     * \param  queueItemPtr  Pointer to the item to be inserted into the queue.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*queueItemPut)(void * const osal,
                                       const Template_osalQueueHandle_t queueHandle,
                                       const void * const queueItemPtr);

    /**
     * \brief  Inserts an item into a queue, waiting up to the requested timeout for capacity.
     * \param  osal          OSAL instance.
     * \param  queueHandle   Handle to the target queue.
     * \param  queueItemPtr  Pointer to the item to be inserted into the queue.
     * \param  timeoutMs     Maximum time in milliseconds to wait for free queue capacity.
     * \return Template_osalErr_e (0 on success, timeout/error otherwise).
     */
    Template_osalErr_e (*queueItemPost)(void * const osal,
                                        const Template_osalQueueHandle_t queueHandle,
                                        const void * const queueItemPtr,
                                        const Template_osalTimeMs_t timeoutMs);

    /**
     * \brief  Retrieves an already available item without waiting.
     * \param  osal          OSAL instance.
     * \param  queueHandle   Handle to the source queue.
     * \param  queueItemPtr  Pointer to the destination buffer for the retrieved item.
     * \return Template_osalErr_e (0 on success, empty/error otherwise).
     */
    Template_osalErr_e (*queueItemGet)(void * const osal,
                                       const Template_osalQueueHandle_t queueHandle,
                                       void * const queueItemPtr);

    /**
     * \brief  Waits indefinitely for an item and retrieves it.
     * \param  osal          OSAL instance.
     * \param  queueHandle   Handle to the source queue.
     * \param  queueItemPtr  Pointer to the destination buffer for the retrieved item.
     * \return Template_osalErr_e (0 on success, error otherwise).
     */
    Template_osalErr_e (*queueItemWait)(void * const osal,
                                        const Template_osalQueueHandle_t queueHandle,
                                        void * const queueItemPtr);

    /**
     * \brief  Retrieves an item from a queue with blocking and timeout.
     * \param  osal           OSAL instance.
     * \param  queueHandle    Handle to the target queue.
     * \param  queueItemPtr   Pointer to the buffer for the retrieved item.
     * \param  timeoutMs      Maximum time (in milliseconds) to wait for an item.
     * \return Template_osalErr_e (0 on success, timeout/error otherwise).
     */
    Template_osalErr_e (*queueItemPend)(void * const osal,
                                        const Template_osalQueueHandle_t queueHandle,
                                        void * const queueItemPtr,
                                        const Template_osalTimeMs_t timeoutMs);

    /**
     * \brief  Resets a queue by discarding all items.
     * \param  osal         OSAL instance.
     * \param  queueHandle  Handle to the queue to be reset.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*queueReset)(void * const osal,
                                     const Template_osalQueueHandle_t queueHandle);
    // END QUEUE

    // BEGIN STREAM_BUFFER
    /*-------------------------------- Stream buffers --------------------------------*/

    /**
     * \brief  Create a byte stream buffer.
     * \param  osal                OSAL instance.
     * \param  bufferSizeBytes     Total stream-buffer capacity in bytes.
     * \param  triggerLevelBytes   Receive trigger level in bytes.
     * \param  streamBufferHandle  Output pointer receiving the created handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferCreate)(void * const osal,
                                              const size_t bufferSizeBytes,
                                              const size_t triggerLevelBytes,
                                              Template_osalStreamBufferHandle_t * const streamBufferHandle);

    /**
     * \brief  Delete a byte stream buffer.
     * \param  osal                OSAL instance.
     * \param  streamBufferHandle  Registered stream-buffer handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferDelete)(void * const osal,
                                              const Template_osalStreamBufferHandle_t streamBufferHandle);

    /**
     * \brief  Send bytes without waiting for capacity.
     * \param  osal                OSAL instance.
     * \param  streamBufferHandle  Registered stream-buffer handle.
     * \param  data                Source byte buffer.
     * \param  dataLengthBytes     Number of bytes requested for transmission.
     * \param  bytesSent           Output number of bytes accepted by the backend.
     * \return Template_osalErr_e, zero value means the operation was valid; inspect bytesSent for transfer length.
     */
    Template_osalErr_e (*streamBufferSend)(void * const osal,
                                            const Template_osalStreamBufferHandle_t streamBufferHandle,
                                            const void * const data,
                                            const size_t dataLengthBytes,
                                            size_t * const bytesSent);

    /**
     * \brief  Receive bytes using an explicit timeout.
     * \param  osal                OSAL instance.
     * \param  streamBufferHandle  Registered stream-buffer handle.
     * \param  data                Destination byte buffer.
     * \param  dataLengthBytes     Maximum number of bytes to receive.
     * \param  timeoutMs           Maximum wait in milliseconds.
     * \param  bytesReceived       Output number of bytes received.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferReceive)(void * const osal,
                                               const Template_osalStreamBufferHandle_t streamBufferHandle,
                                               void * const data,
                                               const size_t dataLengthBytes,
                                               const Template_osalTimeMs_t timeoutMs,
                                               size_t * const bytesReceived);

    /**
     * \brief  Reset a byte stream buffer to the empty state.
     * \param  osal                OSAL instance.
     * \param  streamBufferHandle  Registered stream-buffer handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferReset)(void * const osal,
                                             const Template_osalStreamBufferHandle_t streamBufferHandle);
    // END STREAM_BUFFER

    // BEGIN LOCK
    /*------------------------------------ Locks -------------------------------------*/

    /**
     * \brief  Creates a lock object such as a mutex or semaphore.
     * \param  osal           OSAL instance.
     * \param  lockObjHandle  Pointer to store the created lock object handle.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*lockObjCreate)(void * const osal,
                                        Template_osalLockObjHandle_t * const lockObjHandle);

    /**
     * \brief  Deletes a lock object.
     * \param  osal           OSAL instance.
     * \param  lockObjHandle  Handle to the lock object to be deleted.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*lockObjDelete)(void * const osal,
                                        const Template_osalLockObjHandle_t lockObjHandle);

    /**
     * \brief  Acquires a lock (blocking).
     * \param  osal           OSAL instance.
     * \param  lockObjHandle  Handle to the lock object to acquire.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*lock)(void * const osal,
                               const Template_osalLockObjHandle_t lockObjHandle);

    /**
     * \brief  Releases a previously acquired lock.
     * \param  osal           OSAL instance.
     * \param  lockObjHandle  Handle to the lock object to release.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*unlock)(void * const osal,
                                 const Template_osalLockObjHandle_t lockObjHandle);

    // END LOCK

    // BEGIN SEMAPHORE
    /*------------------------------ Counting semaphores -----------------------------*/

    /**
     * \brief  Create a counting semaphore.
     * \param  osal             OSAL instance.
     * \param  maxCount         Maximum semaphore count.
     * \param  initialCount     Initial semaphore count.
     * \param  semaphoreHandle  Output pointer receiving the created handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreCreate)(void * const osal,
                                           const Template_osalSemaphoreCount_t maxCount,
                                           const Template_osalSemaphoreCount_t initialCount,
                                           Template_osalSemaphoreHandle_t * const semaphoreHandle);

    /**
     * \brief  Delete a counting semaphore.
     * \param  osal             OSAL instance.
     * \param  semaphoreHandle  Registered semaphore handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreDelete)(void * const osal,
                                           const Template_osalSemaphoreHandle_t semaphoreHandle);

    /**
     * \brief  Acquire one semaphore count without waiting.
     * \param  osal             OSAL instance.
     * \param  semaphoreHandle  Registered semaphore handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreAcquire)(void * const osal,
                                            const Template_osalSemaphoreHandle_t semaphoreHandle);

    /**
     * \brief  Acquire one semaphore count using an explicit timeout.
     * \param  osal             OSAL instance.
     * \param  semaphoreHandle  Registered semaphore handle.
     * \param  timeoutMs        Maximum wait in milliseconds.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreAcquireWait)(void * const osal,
                                                const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                const Template_osalTimeMs_t timeoutMs);

    /**
     * \brief  Release one count to a semaphore.
     * \param  osal             OSAL instance.
     * \param  semaphoreHandle  Registered semaphore handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreRelease)(void * const osal,
                                            const Template_osalSemaphoreHandle_t semaphoreHandle);

    /**
     * \brief  Read the current semaphore count.
     * \param  osal             OSAL instance.
     * \param  semaphoreHandle  Registered semaphore handle.
     * \param  semaphoreCount   Output pointer receiving the current count.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreCountGet)(void * const osal,
                                             const Template_osalSemaphoreHandle_t semaphoreHandle,
                                             Template_osalSemaphoreCount_t * const semaphoreCount);
    // END SEMAPHORE

    // BEGIN THREAD
    /*----------------------------------- Threads ------------------------------------*/

    /**
     * \brief  Creates a new thread with specified configuration.
     * \param  osal          OSAL instance.
     * \param  threadHandle  Pointer to store the created thread handle.
     * \param  threadCfg     Configuration parameters for the thread.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*threadCreate)(void * const osal,
                                       Template_osalThreadHandle_t * const threadHandle,
                                       Template_osalThreadCfg_s threadCfg);

    /**
     * \brief  Deletes a thread.
     * \param  osal          OSAL instance.
     * \param  threadHandle  Handle to the thread to be deleted.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*threadDelete)(void * const osal,
                                       const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief  Suspends a running thread.
     * \param  osal          OSAL instance.
     * \param  threadHandle  Handle to the thread to be suspended.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*threadSuspend)(void * const osal,
                                        const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief  Resumes a suspended thread.
     * \param  osal          OSAL instance.
     * \param  threadHandle  Handle to the thread to be resumed.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*threadResume)(void * const osal,
                                       const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief  Delays execution of the current thread.
     * \param  osal      OSAL instance.
     * \param  delayMs   Delay duration in milliseconds.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*threadDelay)(void * const osal,
                                      const Template_osalTimeMs_t delayMs);

    /**
     * \brief  Terminate the calling thread (does not return).
     * \note   This function never returns control to the caller.
     * \param  osal  OSAL instance (must be valid).
     */
    void (*threadExit)(void *const osal);
    // END THREAD

    // BEGIN CRITICAL_SECTION
    /*------------------------------- Critical section ------------------------------*/

    /**
     * \brief  Enter a short OS critical section.
     * \param  osal  OSAL instance.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*criticalSectionEnter)(void * const osal);

    /**
     * \brief  Exit a previously entered OS critical section.
     * \param  osal  OSAL instance.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*criticalSectionExit)(void * const osal);
    // END CRITICAL_SECTION

    // BEGIN SOFTWARE_TIMER
    /*-------------------------------- Software timers -------------------------------*/

    /**
     * \brief  Create a one-shot or auto-reload software timer.
     * \param  osal         OSAL instance.
     * \param  timerHandle  Output pointer receiving the created timer handle.
     * \param  timerCfg     Timer callback, user parameter, reload policy and period.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerCreate)(void * const osal,
                                               Template_osalSoftwareTimerHandle_t * const timerHandle,
                                               Template_osalSoftwareTimerCfg_s timerCfg);

    /**
     * \brief  Delete a software timer.
     * \param  osal         OSAL instance.
     * \param  timerHandle  Registered software-timer handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerDelete)(void * const osal,
                                               const Template_osalSoftwareTimerHandle_t timerHandle);

    /**
     * \brief  Start a software timer.
     * \param  osal         OSAL instance.
     * \param  timerHandle  Registered software-timer handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerStart)(void * const osal,
                                              const Template_osalSoftwareTimerHandle_t timerHandle);

    /**
     * \brief  Stop a software timer.
     * \param  osal         OSAL instance.
     * \param  timerHandle  Registered software-timer handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerStop)(void * const osal,
                                             const Template_osalSoftwareTimerHandle_t timerHandle);

    /**
     * \brief  Reset and restart a software-timer period.
     * \param  osal         OSAL instance.
     * \param  timerHandle  Registered software-timer handle.
     * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerReset)(void * const osal,
                                              const Template_osalSoftwareTimerHandle_t timerHandle);
    // END SOFTWARE_TIMER

    // BEGIN TIME
    /*------------------------------------- Time -------------------------------------*/

    /**
     * \brief  Retrieves the current system time in milliseconds.
     * \param  osal       OSAL instance.
     * \param  osTimeMs   Pointer to store the current time in milliseconds.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*timeMsGet)(void * const osal,
                                    Template_osalTimeMs_t * const osTimeMs);
    // END TIME

    // BEGIN MEMORY
    /*------------------------------------- Memory -----------------------------------*/

    /**
     * \brief  Allocates a memory block in the backend.
     * \param  osal    OSAL instance.
     * \param  size    Allocation size in bytes.
     * \param  memPtr  Pointer to store the allocated memory address.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*memAlloc)(void * const osal,
                                   const size_t size,
                                   void ** const memPtr);

    /**
     * \brief  Frees a previously allocated memory block in the backend.
     * \param  osal  OSAL instance.
     * \param  ptr   Pointer to a block previously returned by memAlloc.
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*memFree)(void * const osal,
                                  void * const ptr);
    // END MEMORY

    /*---------------------------------- Predicate -----------------------------------*/

    /**
     * \brief  Validates the OSAL instance.
     * \param  osal  OSAL instance to validate.
     * \return true if the instance is valid and initialized, false otherwise.
     */
    bool (*isValid)(const void * const osal);
} Template_osalVtable_s;

/*-------------------------------- Protected  -------------------------------*/

/**
 * \struct  Template_osalPtable_s
 * \brief   Protected OSAL helpers for backend ports (registry utilities etc).
 * \details Internal slot/handle lookup helpers for every registry-backed primitive group.
 *          Returned IDs are 1-based (index + 1). Zero value indicates "not found"/"no free slot".
 * \note    This is a ptable API intended for OSAL backends only.
 */
typedef struct
{
    // BEGIN QUEUE
    /*------------------------------- Queues --------------------------------*/

    /**
     * \brief  Find a free queue slot in the internal registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*queueFreeSlotFind)(void * const osalPort);

    /**
     * \brief  Find a queue handle in the internal registry.
     * \param  osalPort     Derived OSAL instance pointer (opaque in base).
     * \param  queueHandle  Queue handle to search.
     * \return size_t       Queue ID (index + 1) if found; 0 otherwise.
     */
    size_t (*queueHandleFind)(void * const osalPort,
                              const Template_osalQueueHandle_t queueHandle);

    // END QUEUE

    // BEGIN STREAM_BUFFER
    /*----------------------------- Stream buffers ----------------------------*/

    /**
     * \brief  Find a free stream-buffer slot in the internal registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \return size_t    Slot ID (index + 1) if a free slot exists; 0 otherwise.
     */
    size_t (*streamBufferFreeSlotFind)(void * const osalPort);

    /**
     * \brief  Find a stream-buffer handle in the internal registry.
     * \param  osalPort           Derived OSAL instance pointer (opaque in base).
     * \param  streamBufferHandle Stream-buffer handle to search.
     * \return size_t             Stream-buffer ID (index + 1) if found; 0 otherwise.
     */
    size_t (*streamBufferHandleFind)(void * const osalPort,
                                     const Template_osalStreamBufferHandle_t streamBufferHandle);
    // END STREAM_BUFFER

    // BEGIN LOCK
    /*-------------------------------- Locks --------------------------------*/

    /**
     * \brief  Find a free lock object slot in the internal registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*lockObjFreeSlotFind)(void * const osalPort);

    /**
     * \brief  Find a lock object handle in the internal registry.
     * \param  osalPort      Derived OSAL instance pointer (opaque in base).
     * \param  lockObjHandle Lock object handle to search.
     * \return size_t        Lock object ID (index + 1) if found; 0 otherwise.
     */
    size_t (*lockObjHandleFind)(void * const osalPort,
                                const Template_osalLockObjHandle_t lockObjHandle);
    // END LOCK

    // BEGIN SEMAPHORE
    /*--------------------------- Counting semaphores --------------------------*/

    /**
     * \brief  Find a free counting-semaphore slot in the internal registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \return size_t    Slot ID (index + 1) if a free slot exists; 0 otherwise.
     */
    size_t (*semaphoreFreeSlotFind)(void * const osalPort);

    /**
     * \brief  Find a counting-semaphore handle in the internal registry.
     * \param  osalPort        Derived OSAL instance pointer (opaque in base).
     * \param  semaphoreHandle Counting-semaphore handle to search.
     * \return size_t          Semaphore ID (index + 1) if found; 0 otherwise.
     */
    size_t (*semaphoreHandleFind)(void * const osalPort,
                                  const Template_osalSemaphoreHandle_t semaphoreHandle);
    // END SEMAPHORE

    // BEGIN THREAD
    /*------------------------------- Threads -------------------------------*/

    /**
     * \brief  Find a free thread slot in the internal registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*threadFreeSlotFind)(void * const osalPort);

    /**
     * \brief  Find a thread handle in the internal registry.
     * \param  osalPort     Derived OSAL instance pointer (opaque in base).
     * \param  threadHandle Thread handle to search.
     * \return size_t       Thread ID (index + 1) if found; 0 otherwise.
     */
    size_t (*threadHandleFind)(void * const osalPort,
                               const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief  Clear a thread registry slot.
     * \param  osalPort   Derived OSAL instance pointer (opaque in base).
     * \param  threadIdx  Zero-based thread registry index.
     * \return None.
     */
    void (*threadSlotClear)(void * const osalPort,
                            const size_t threadIdx);
    // END THREAD

    // BEGIN SOFTWARE_TIMER
    /*----------------------------- Software timers ----------------------------*/

    /**
     * \brief  Find a free software-timer slot in the internal registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \return size_t    Slot ID (index + 1) if a free slot exists; 0 otherwise.
     */
    size_t (*softwareTimerFreeSlotFind)(void * const osalPort);

    /**
     * \brief  Find a software-timer handle in the internal registry.
     * \param  osalPort    Derived OSAL instance pointer (opaque in base).
     * \param  timerHandle Software-timer handle to search.
     * \return size_t      Software-timer ID (index + 1) if found; 0 otherwise.
     */
    size_t (*softwareTimerHandleFind)(void * const osalPort,
                                      const Template_osalSoftwareTimerHandle_t timerHandle);
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    /*-------------------------------- Memory --------------------------------*/

    /**
     * \brief  Find a free memory slot in the internal registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*memFreeSlotFind)(void * const osalPort);

    /**
     * \brief  Find an allocated pointer in the memory registry.
     * \param  osalPort  Derived OSAL instance pointer (opaque in base).
     * \param  ptr       Pointer to search.
     * \return size_t    Memory ID (index + 1) if found; 0 otherwise.
     */
    size_t (*memHandleFind)(void * const osalPort,
                            const void * const ptr);
    // END MEMORY

    uint8_t reserved; /*!< Keeps the protected table valid when no registry-backed primitive is selected. */
} Template_osalPtable_s;


/**
 * \struct  Template_osal_s
 * \brief   OS Abstraction Layer (OSAL) interface descriptor.
 * \details Descriptor for a particular OS port: component-scoped resource registries, vtable, protected ptable and validation state.
 */
typedef struct
{
    /* Metadata */
    const void *parent;
    const char *name;

    // BEGIN QUEUE
    /* Queues handles */
    Template_osalQueueHandle_t queueObjHandle[TEMPLATE_OSAL_QUEUE_SLOTS_NUM];
    // END QUEUE

    // BEGIN STREAM_BUFFER
    /* Stream buffer handles */
    Template_osalStreamBufferHandle_t streamBufferObjHandle[TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM];
    // END STREAM_BUFFER

    // BEGIN LOCK
    /* Lock objects handles */
    Template_osalLockObjHandle_t lockObjHandle[TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM];
    // END LOCK

    // BEGIN SEMAPHORE
    /* Counting semaphore handles */
    Template_osalSemaphoreHandle_t semaphoreObjHandle[TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM];
    // END SEMAPHORE

    // BEGIN THREAD
    /* Threads handles */
    Template_osalThread_s threadObjHandle[TEMPLATE_OSAL_THREAD_SLOTS_NUM];
    // END THREAD

    // BEGIN SOFTWARE_TIMER
    /* Software timer objects */
    Template_osalSoftwareTimer_s softwareTimerObj[TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM];
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    /* Memory registry handles (opaque) */
    Template_osalMemHandle_t memObjHandle[TEMPLATE_OSAL_MEM_SLOTS_NUM];
    // END MEMORY

    /* OS port methods table */
    const Template_osalVtable_s *vtable;

    /* Protected methods for backend usage */
    const Template_osalPtable_s *ptable;

    /* Validation */
    bool validFlag;
} Template_osal_s;

/*===========================================================[PUBLIC INTERFACE]=============================================*/

/*---------------------------------- Lifecycle --------------------------------*/

/**
 * \brief Initialize Template OSAL instance, sets name, parent, and clears all internal objects.
 * \param osal    Pointer to OSAL instance.
 * \param name    Pointer to the name of the OSAL instance.
 * \param parent  Pointer to a parent object.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalInit(Template_osal_s *const osal,
                                     const char *name,
                                     void *const parent);

/**
 * \brief Deinitialize Template OSAL instance.
 * \param osal  Pointer to OSAL instance.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalDeinit(Template_osal_s *const osal);

/**
 * \brief Checks if the given OSAL instance is valid including the vtable layer.
 * \param osal  Pointer to the OSAL instance.
 * \return true if valid, false otherwise.
 */
bool template_osalIsValid(const Template_osal_s *const osal);

/*----------------------------------- Metadata --------------------------------*/

/**
 * \brief Get pointer to a parent of the given OSAL object.
 * \param osal     Pointer to OSAL instance which parent object will be returned.
 * \param parent   Pointer to an object into which the current osal parent pointer will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalParentGet(Template_osal_s *const osal,
                                          void **const parent);

/**
 * \brief Set the parent object for the given OSAL instance.
 * \param osal   Pointer to OSAL instance being modified.
 * \param parent Pointer to parent to be set (may be NULL).
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalParentSet(Template_osal_s *const osal,
                                          const void *const parent);

/**
 * \brief Get pointer to the name field of the given OSAL instance.
 * \param osal Pointer to OSAL instance.
 * \param name Pointer to an object into which the current osal name will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalNameGet(Template_osal_s *const osal,
                                        const char **const name);

/**
 * \brief Set name for the given OSAL instance.
 * \param osal Pointer to OSAL instance being modified.
 * \param name Pointer to name string being set.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalNameSet(Template_osal_s *const osal,
                                        const char *const name);

// BEGIN QUEUE
/*------------------------------------ Queues ---------------------------------*/

/**
 * \brief Create the queue.
 * \param osal           Pointer to OSAL instance.
 * \param queueItemSize  The size of the queue item.
 * \param queueDepth     The queue depth.
 * \param queueHandle    Pointer to the created queue handle.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueCreate(Template_osal_s *const osal,
                                            const size_t queueItemSize,
                                            const size_t queueDepth,
                                            Template_osalQueueHandle_t *const queueHandle);

/**
 * \brief Delete the queue.
 * \param osal        Pointer to OSAL instance.
 * \param queueHandle The queue handle to delete.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueDelete(Template_osal_s *const osal,
                                            const Template_osalQueueHandle_t queueHandle);

/**
 * \brief Put item to the queue.
 * \param osal         Pointer to OSAL instance.
 * \param queueHandle  The queue handle in which to put the item.
 * \param queueItemPtr Pointer to the item source buffer.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueItemPut(Template_osal_s *const osal,
                                             const Template_osalQueueHandle_t queueHandle,
                                             const void *const queueItemPtr);

/**
 * \brief Post an item to the queue, waiting up to the requested timeout for free capacity.
 * \param osal          Pointer to OSAL instance.
 * \param queueHandle   Queue handle in which to put the item.
 * \param queueItemPtr  Pointer to the item source buffer.
 * \param timeoutMs     Maximum wait time in milliseconds.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueItemPost(Template_osal_s *const osal,
                                              const Template_osalQueueHandle_t queueHandle,
                                              const void *const queueItemPtr,
                                              const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Get an already available item from the queue without waiting.
 * \param osal          Pointer to OSAL instance.
 * \param queueHandle   Queue handle from which to get the item.
 * \param queueItemPtr  Pointer to the destination buffer for the item.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueItemGet(Template_osal_s *const osal,
                                             const Template_osalQueueHandle_t queueHandle,
                                             void *const queueItemPtr);

/**
 * \brief Wait indefinitely for an item and retrieve it from the queue.
 * \param osal          Pointer to OSAL instance.
 * \param queueHandle   Queue handle from which to get the item.
 * \param queueItemPtr  Pointer to the destination buffer for the item.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueItemWait(Template_osal_s *const osal,
                                              const Template_osalQueueHandle_t queueHandle,
                                              void *const queueItemPtr);

/**
 * \brief Get item from the queue.
 * \note  Blocking call with specified wait.
 * \param osal          Pointer to OSAL instance.
 * \param queueHandle   The queue handle from which to get the item.
 * \param queueItemPtr  Pointer to the destination buffer for the item.
 * \param timeoutMs     Timeout in milliseconds to wait for the item.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueItemPend(Template_osal_s *const osal,
                                              const Template_osalQueueHandle_t queueHandle,
                                              void *const queueItemPtr,
                                              const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Reset the queue.
 * \param osal         Pointer to OSAL instance.
 * \param queueHandle  The queue handle to reset.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueReset(Template_osal_s *const osal,
                                           const Template_osalQueueHandle_t queueHandle);

/**
 * \brief Get a queue handle of the given OSAL object.
 * \param osal          Pointer to OSAL instance.
 * \param queueSlotInd  Index of queue slot.
 * \param queueHandle   Pointer to the current queue handle.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalQueueHandleGet(Template_osal_s *const osal,
                                               const size_t queueSlotInd,
                                               Template_osalQueueHandle_t *const queueHandle);

// END QUEUE

// BEGIN STREAM_BUFFER
/*-------------------------------- Stream buffers -------------------------------*/

/**
 * \brief Create a byte stream buffer.
 * \param osal               Pointer to OSAL instance.
 * \param bufferSizeBytes    Stream buffer capacity in bytes.
 * \param triggerLevelBytes  Minimum number of bytes that should unblock a waiting receiver.
 * \param streamBufferHandle Output pointer receiving the created stream buffer handle.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalStreamBufferCreate(Template_osal_s *const osal,
                                                    const size_t bufferSizeBytes,
                                                    const size_t triggerLevelBytes,
                                                    Template_osalStreamBufferHandle_t *const streamBufferHandle);

/**
 * \brief Delete a stream buffer.
 * \param osal                Pointer to OSAL instance.
 * \param streamBufferHandle  Handle of the stream buffer to delete.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalStreamBufferDelete(Template_osal_s *const osal,
                                                    const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Send bytes to a stream buffer without waiting for capacity.
 * \param osal                Pointer to OSAL instance.
 * \param streamBufferHandle  Target stream buffer handle.
 * \param data                Pointer to source bytes.
 * \param dataLengthBytes     Number of bytes requested for transfer.
 * \param bytesSent           Output pointer receiving the number of bytes actually written.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalStreamBufferSend(Template_osal_s *const osal,
                                                  const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                  const void *const data,
                                                  const size_t dataLengthBytes,
                                                  size_t *const bytesSent);

/**
 * \brief Receive bytes from a stream buffer with an explicit timeout.
 * \param osal                Pointer to OSAL instance.
 * \param streamBufferHandle  Source stream buffer handle.
 * \param data                Destination buffer.
 * \param dataLengthBytes     Maximum number of bytes to receive.
 * \param timeoutMs           Maximum wait in milliseconds; zero means non-blocking.
 * \param bytesReceived       Output pointer receiving the number of bytes actually read.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalStreamBufferReceive(Template_osal_s *const osal,
                                                     const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                     void *const data,
                                                     const size_t dataLengthBytes,
                                                     const Template_osalTimeMs_t timeoutMs,
                                                     size_t *const bytesReceived);

/**
 * \brief Reset a stream buffer to its initial empty state.
 * \param osal                Pointer to OSAL instance.
 * \param streamBufferHandle  Stream buffer handle to reset.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalStreamBufferReset(Template_osal_s *const osal,
                                                   const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Get a stream buffer handle from a stable registry slot.
 * \param osal                 Pointer to OSAL instance.
 * \param streamBufferSlotInd  Zero-based stream buffer registry slot index.
 * \param streamBufferHandle   Output pointer receiving the current slot handle.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalStreamBufferHandleGet(Template_osal_s *const osal,
                                                       const size_t streamBufferSlotInd,
                                                       Template_osalStreamBufferHandle_t *const streamBufferHandle);
// END STREAM_BUFFER

// BEGIN LOCK
/*------------------------------------- Locks --------------------------------*/

/**
 * \brief Create a lock object.
 * \param  osal          Pointer to the OSAL instance.
 * \param  lockObjHandle Pointer to the location where the lock object handle will be created.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalLockObjCreate(Template_osal_s *const osal,
                                              Template_osalLockObjHandle_t *const lockObjHandle);

/**
 * \brief Delete a lock object.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object to delete.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalLockObjDelete(Template_osal_s *const osal,
                                              const Template_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Lock access to the resource.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalLock(Template_osal_s *const osal,
                                     const Template_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Unlock access to the resource.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalUnlock(Template_osal_s *const osal,
                                       const Template_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Get a lock object handle of the given OSAL object.
 * \param osal            Pointer to the OSAL instance.
 * \param lockObjSlotInd  Index of the lock object slots.
 * \param lockObjHandle   Pointer where the lock object handle will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalLockObjHandleGet(Template_osal_s *const osal,
                                                 const size_t lockObjSlotInd,
                                                 Template_osalLockObjHandle_t *const lockObjHandle);
// END LOCK

// BEGIN SEMAPHORE
/*------------------------------ Counting semaphores ---------------------------*/

/**
 * \brief Create a counting semaphore.
 * \param osal             Pointer to OSAL instance.
 * \param maxCount         Maximum semaphore count; must be greater than zero.
 * \param initialCount     Initial count; must not exceed \p maxCount.
 * \param semaphoreHandle  Output pointer receiving the created semaphore handle.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSemaphoreCreate(Template_osal_s *const osal,
                                                 const Template_osalSemaphoreCount_t maxCount,
                                                 const Template_osalSemaphoreCount_t initialCount,
                                                 Template_osalSemaphoreHandle_t *const semaphoreHandle);

/**
 * \brief Delete a counting semaphore.
 * \param osal             Pointer to OSAL instance.
 * \param semaphoreHandle  Handle of the semaphore to delete.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSemaphoreDelete(Template_osal_s *const osal,
                                                 const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Acquire a counting semaphore without waiting.
 * \param osal             Pointer to OSAL instance.
 * \param semaphoreHandle  Semaphore handle to acquire.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSemaphoreAcquire(Template_osal_s *const osal,
                                                  const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Acquire a counting semaphore with an explicit timeout.
 * \param osal             Pointer to OSAL instance.
 * \param semaphoreHandle  Semaphore handle to acquire.
 * \param timeoutMs        Maximum wait in milliseconds; zero means non-blocking.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSemaphoreAcquireWait(Template_osal_s *const osal,
                                                      const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                      const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Release one count to a counting semaphore.
 * \param osal             Pointer to OSAL instance.
 * \param semaphoreHandle  Semaphore handle to release.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSemaphoreRelease(Template_osal_s *const osal,
                                                  const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Read the current counting semaphore value.
 * \param osal             Pointer to OSAL instance.
 * \param semaphoreHandle  Semaphore handle to query.
 * \param semaphoreCount   Output pointer receiving the current count.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSemaphoreCountGet(Template_osal_s *const osal,
                                                   const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                   Template_osalSemaphoreCount_t *const semaphoreCount);

/**
 * \brief Get a semaphore handle from a stable registry slot.
 * \param osal             Pointer to OSAL instance.
 * \param semaphoreSlotInd Zero-based semaphore registry slot index.
 * \param semaphoreHandle  Output pointer receiving the current slot handle.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSemaphoreHandleGet(Template_osal_s *const osal,
                                                    const size_t semaphoreSlotInd,
                                                    Template_osalSemaphoreHandle_t *const semaphoreHandle);
// END SEMAPHORE

// BEGIN THREAD
/*------------------------------------ Threads -------------------------------*/

/**
 * \brief Create a new thread.
 * \param osal         Pointer to the OSAL instance.
 * \param threadHandle Pointer to store the handle of the created thread.
 * \param threadCfg    Configuration parameters for the thread.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadCreate(Template_osal_s *const osal,
                                             Template_osalThreadHandle_t *const threadHandle,
                                             Template_osalThreadCfg_s threadCfg);

/**
 * \brief Delete the thread.
 * \note The operation must be stopped before deleting the thread to avoid system damage.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread being deleted.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadDelete(Template_osal_s *const osal,
                                             const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Suspend the thread.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to suspend.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadSuspend(Template_osal_s *const osal,
                                              const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Resume the thread.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to resume.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadResume(Template_osal_s *const osal,
                                             const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Delay the execution of the current thread.
 * \param osal     Pointer to OSAL instance.
 * \param delayMs  Delay duration in milliseconds.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadDelay(Template_osal_s *const osal,
                                            const Template_osalTimeMs_t delayMs);

/**
 * \brief Terminate the calling thread (does not return).
 * \param osal  Pointer to OSAL instance (must be valid).
 * \note  This function never returns control to the caller.
 */
void template_osalThreadExit(Template_osal_s *const osal);

/**
 * \brief Get a thread handle of the given OSAL object.
 * \param osal           Pointer to OSAL instance.
 * \param threadSlotInd  Index of thread slots.
 * \param threadHandle   Pointer where the thread handle will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadHandleGet(Template_osal_s *const osal,
                                                const size_t threadSlotInd,
                                                Template_osalThreadHandle_t *const threadHandle);
// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ----------------------------*/

/**
 * \brief Enter a short OS critical section.
 * \details The matching exit call shall be made from the same execution context according to the backend contract.
 * \param osal Pointer to OSAL instance.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalCriticalSectionEnter(Template_osal_s *const osal);

/**
 * \brief Exit a previously entered OS critical section.
 * \param osal Pointer to OSAL instance.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalCriticalSectionExit(Template_osal_s *const osal);
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*-------------------------------- Software timers -----------------------------*/

/**
 * \brief Create a one-shot or auto-reload software timer.
 * \param osal        Pointer to OSAL instance.
 * \param timerHandle Output pointer receiving the created timer handle.
 * \param timerCfg    Timer name, callback, callback parameter, reload policy and period.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSoftwareTimerCreate(Template_osal_s *const osal,
                                                     Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                     Template_osalSoftwareTimerCfg_s timerCfg);

/**
 * \brief Delete a software timer.
 * \param osal        Pointer to OSAL instance.
 * \param timerHandle Handle of the timer to delete.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSoftwareTimerDelete(Template_osal_s *const osal,
                                                     const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Start a software timer using its configured period.
 * \param osal        Pointer to OSAL instance.
 * \param timerHandle Timer handle to start.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSoftwareTimerStart(Template_osal_s *const osal,
                                                    const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Stop a software timer.
 * \param osal        Pointer to OSAL instance.
 * \param timerHandle Timer handle to stop.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSoftwareTimerStop(Template_osal_s *const osal,
                                                   const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Reset a software timer and restart its configured period.
 * \param osal        Pointer to OSAL instance.
 * \param timerHandle Timer handle to reset.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSoftwareTimerReset(Template_osal_s *const osal,
                                                    const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Get a software timer handle from a stable registry slot.
 * \param osal         Pointer to OSAL instance.
 * \param timerSlotInd Zero-based software timer registry slot index.
 * \param timerHandle  Output pointer receiving the current slot handle.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalSoftwareTimerHandleGet(Template_osal_s *const osal,
                                                        const size_t timerSlotInd,
                                                        Template_osalSoftwareTimerHandle_t *const timerHandle);
// END SOFTWARE_TIMER

// BEGIN TIME
/*-------------------------------------- Time --------------------------------*/

/**
 * \brief Retrieve the current system time in milliseconds.
 * \param osal       Pointer to OSAL instance.
 * \param osTimeMs   Pointer to store the current time in milliseconds.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalTimeMsGet(Template_osal_s *const osal,
                                          Template_osalTimeMs_t *const osTimeMs);
// END TIME

// BEGIN MEMORY
/*------------------------------------- Memory --------------------------------*/

/**
 * \brief Allocate memory via the OSAL backend and register the pointer internally.
 * \param osal    Pointer to OSAL instance.
 * \param size    Allocation size in bytes.
 * \param memPtr  Pointer to the allocated memory (must not be NULL).
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalMalloc(Template_osal_s *const osal,
                                       const size_t size,
                                       void **const memPtr);

/**
 * \brief Free memory via the OSAL backend and unregister the pointer internally.
 * \param osal  Pointer to OSAL instance.
 * \param ptr   Pointer to the memory block to free (must not be NULL).
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalFree(Template_osal_s *const osal,
                                     void *const ptr);

/**
 * \brief Get a memory handle of the given OSAL object.
 * \param osal          Pointer to OSAL instance.
 * \param memSlotInd    Index of memory slot.
 * \param memHandle     Pointer where the memory handle will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalMemHandleGet(Template_osal_s *const osal,
                                             const size_t memSlotInd,
                                             Template_osalMemHandle_t *const memHandle);
// END MEMORY

#ifdef __cplusplus
    }
#endif

#endif /* TEMPLATE_OSAL_H_ */
