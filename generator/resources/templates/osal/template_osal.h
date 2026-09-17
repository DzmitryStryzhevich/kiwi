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

// BEGIN MUTEX
/**
 * \brief Template OSAL mutex slots number.
 */
#ifndef TEMPLATE_OSAL_MUTEX_SLOTS_NUM
    #define TEMPLATE_OSAL_MUTEX_SLOTS_NUM    (2u)
#endif
// END MUTEX

// BEGIN SEMAPHORE
/**
 * \brief Template OSAL counting semaphore slots number.
 */
#ifndef TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM
    #define TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM    (2u)
#endif
// END SEMAPHORE

// BEGIN EVENT_FLAGS
/**
 * \brief Template OSAL event flags slots number.
 */
#ifndef TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM
    #define TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM    (2u)
#endif
// END EVENT_FLAGS

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
 * \brief Sentinel value for an invalid/empty OSAL object handle.
 *
 * \details Used to denote an uninitialized or released handle in registries
 *          (queues, stream buffers, mutexes, semaphores, event flags, timers, threads and memory).
 *          This equals NULL by design and is checked against before any dereference or registry lookup.
 */
#ifndef TEMPLATE_OSAL_OBJ_HANDLE_INVALID
    #define TEMPLATE_OSAL_OBJ_HANDLE_INVALID    (NULL)
#endif


/*========================================================[DATA TYPES DEFINITIONS]==========================================*/

/**
 * \enum    Template_osalErr_e
 * \brief   Error codes for the Template Operating System Abstraction Layer (OSAL).
 * \details Error codes use fixed sequential decimal values.
 *          Existing values shall never be changed or reused to preserve backward compatibility.
 *          New errors shall always receive the next available value.
 *          This allows error codes to be used directly as lookup-table indexes.
 */
typedef enum
{
    TEMPLATE_OSAL_NO_ERR            = 0,                   //!< No error occurred; operation was successful.
    TEMPLATE_OSAL_INVALID_ARGS_ERR  = 1,                   //!< Invalid arguments passed to an OSAL function.
    TEMPLATE_OSAL_NOT_INIT_ERR      = 2,                   //!< OSAL instance or required service is not initialized.
    TEMPLATE_OSAL_CALL_FROM_ISR_ERR = 3,                   //!< Function was called from an ISR where this operation is not allowed.

    // BEGIN QUEUE
    TEMPLATE_OSAL_QUEUE_CREATE_ERR         = 4,            //!< Failed to create a queue or reserve a queue registry slot.
    TEMPLATE_OSAL_QUEUE_MEM_ALLOCATION_ERR = 5,            //!< Memory allocation failed during queue creation.
    TEMPLATE_OSAL_QUEUE_OVERFLOW_ERR       = 6,            //!< Queue item could not be inserted because the queue is full.
    TEMPLATE_OSAL_QUEUE_IS_EMPTY_ERR       = 7,            //!< Queue item could not be retrieved because the queue is empty.
    TEMPLATE_OSAL_QUEUE_IS_FULL_ERR        = 8,            //!< Queue is currently full.
    // END QUEUE

    // BEGIN STREAM_BUFFER
    TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR         = 9,    //!< Failed to create a stream buffer or reserve a registry slot.
    TEMPLATE_OSAL_STREAM_BUFFER_MEM_ALLOCATION_ERR = 10,   //!< Memory allocation failed during stream buffer creation.
    TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR       = 11,   //!< Stream buffer does not contain data available for reading.
    TEMPLATE_OSAL_STREAM_BUFFER_IS_FULL_ERR        = 12,   //!< Stream buffer does not have capacity available for writing.
    TEMPLATE_OSAL_STREAM_BUFFER_RESET_ERR          = 13,   //!< Failed to reset the stream buffer to its empty state.
    // END STREAM_BUFFER

    // BEGIN MUTEX
    TEMPLATE_OSAL_MUTEX_CREATE_ERR         = 14,           //!< Failed to create a mutex or reserve a mutex registry slot.
    TEMPLATE_OSAL_MUTEX_MEM_ALLOCATION_ERR = 15,           //!< Memory allocation failed during mutex creation.
    TEMPLATE_OSAL_MUTEX_LOCK_ERR           = 16,           //!< Mutex could not be locked within the requested wait condition.
    TEMPLATE_OSAL_MUTEX_UNLOCK_ERR         = 17,           //!< Mutex could not be unlocked.
    // END MUTEX

    // BEGIN SEMAPHORE
    TEMPLATE_OSAL_SEMAPHORE_CREATE_ERR         = 18,       //!< Failed to create a counting semaphore or reserve a registry slot.
    TEMPLATE_OSAL_SEMAPHORE_MEM_ALLOCATION_ERR = 19,       //!< Memory allocation failed during semaphore creation.
    TEMPLATE_OSAL_SEMAPHORE_WAIT_ERR           = 20,       //!< Semaphore wait or pend operation did not complete successfully.
    TEMPLATE_OSAL_SEMAPHORE_POST_ERR           = 21,       //!< Failed to post a count to the semaphore.
    // END SEMAPHORE

    // BEGIN THREAD
    TEMPLATE_OSAL_THREAD_CREATE_ERR         = 22,          //!< Failed to create a thread or reserve a thread registry slot.
    TEMPLATE_OSAL_THREAD_MEM_ALLOCATION_ERR = 23,          //!< Memory allocation failed during thread creation.
    // END THREAD

    // BEGIN EVENT_FLAGS
    TEMPLATE_OSAL_EVENT_FLAGS_CREATE_ERR         = 24,     //!< Failed to create an event flags object or reserve a registry slot.
    TEMPLATE_OSAL_EVENT_FLAGS_MEM_ALLOCATION_ERR = 25,     //!< Memory allocation failed during event flags creation.
    TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ERR           = 26,     //!< Requested event flags wait condition was not satisfied.
    TEMPLATE_OSAL_EVENT_FLAGS_SET_ERR            = 27,     //!< Failed to set one or more event flags bits.
    TEMPLATE_OSAL_EVENT_FLAGS_CLEAR_ERR          = 28,     //!< Failed to clear one or more event flags bits.
    // END EVENT_FLAGS

    // BEGIN SOFTWARE_TIMER
    TEMPLATE_OSAL_SOFTWARE_TIMER_CREATE_ERR         = 29,  //!< Failed to create a software timer or reserve a registry slot.
    TEMPLATE_OSAL_SOFTWARE_TIMER_MEM_ALLOCATION_ERR = 30,  //!< Memory allocation failed during software timer creation.
    TEMPLATE_OSAL_SOFTWARE_TIMER_START_ERR          = 31,  //!< Failed to start a software timer.
    TEMPLATE_OSAL_SOFTWARE_TIMER_STOP_ERR           = 32,  //!< Failed to stop a software timer.
    TEMPLATE_OSAL_SOFTWARE_TIMER_RESET_ERR          = 33,  //!< Failed to reset or restart a software timer.
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    TEMPLATE_OSAL_MEM_ALLOCATION_ERR = 34,                 //!< Backend memory allocation failed.
    // END MEMORY

    TEMPLATE_OSAL_PORT_SPECIFIC_ERR = 35                   //!< Port-specific or RTOS-specific operation failed.
} Template_osalErr_e;

/**
 * \brief Number of reserved OSAL error-code values.
 *
 * \details Error codes occupy the fixed range from 0 through
 *          TEMPLATE_OSAL_PORT_SPECIFIC_ERR and may therefore be
 *          used directly as lookup-table indexes.
 */
#define TEMPLATE_OSAL_ERR_CODES_NUM    (36u)

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

// BEGIN MUTEX
/**
 * \brief Template OSAL mutex type definition.
 */
typedef void *Template_osalMutexHandle_t;
// END MUTEX

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

// BEGIN EVENT_FLAGS
/**
 * \brief Template OSAL event flags handle type definition.
 */
typedef void *Template_osalEventFlagsHandle_t;

/**
 * \enum   Template_osalEventFlagsOptions_e
 * \brief  Event flags wait options.
 * \details WAIT_ANY is the default selection mode. WAIT_ALL and NO_CLEAR may be combined by bitwise OR.
 */
typedef enum
{
    TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ANY = 0x00,  //!< Wait until any requested flag bit is set.
    TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ALL = 0x01,  //!< Wait until all requested flag bits are set.
    TEMPLATE_OSAL_EVENT_FLAGS_NO_CLEAR = 0x02   //!< Keep satisfied flag bits set after a successful wait.
} Template_osalEventFlagsOptions_e;
// END EVENT_FLAGS

// BEGIN THREAD
/**
 * \brief Template OSAL run-time Thread handle type definition.
 */
typedef void *Template_osalThreadHandle_t;
// END THREAD

// BEGIN SOFTWARE_TIMER
/**
 * \brief Template OSAL software timer handle type definition.
 */
typedef void *Template_osalSoftwareTimerHandle_t;

/**
 * \brief Software timer expiration callback.
 *
 * \param timerParam  User parameter configured for the software timer.
 */
typedef void (*Template_osalSoftwareTimerExpiredCb_f)(void *const timerParam);

/**
 * \brief Template OSAL software timer attributes.
 */
typedef struct
{
    const char                            *name;           /*!< Optional timer name. */
    void                                  *timerParam;     /*!< User parameter passed to callback; may be NULL. */
    Template_osalSoftwareTimerExpiredCb_f timerExpiredCb;  /*!< Expiration callback; must not be NULL. */
    bool                                  autoReload;      /*!< true = periodic, false = one-shot. */
    Template_osalTimeMs_t                 periodMs;        /*!< Timer period in milliseconds; must be non-zero. */
} Template_osalSoftwareTimerAttr_s;

/**
 * \brief Template OSAL software timer registry object.
 */
typedef struct
{
    Template_osalSoftwareTimerAttr_s   attr;      /*!< Creation attributes snapshot. */
    Template_osalSoftwareTimerHandle_t handle;  /*!< RTOS-native timer handle. */
} Template_osalSoftwareTimer_s;
// END SOFTWARE_TIMER

// BEGIN THREAD
/**
 * \typedef  Template_osalThreadWorker_f
 * \brief Function prototype for the Template OSAL thread worker.
 *
 * \param args  Parameter assigned in Template_osalThreadAttr_s on thread
 *              creation and passed to a thread worker as an arg
 */
typedef void (*Template_osalThreadWorker_f)(void *const args);

/**
 * \enum    Template_osalThreadPrio_e
 * \brief   Required thread priority levels for OSAL implementations.
 */
typedef enum
{
    TEMPLATE_OSAL_THREAD_PRIO_LOW      = 0,  //!< Background tasks.
    TEMPLATE_OSAL_THREAD_PRIO_NORMAL   = 1,  //!< Standard operational tasks.
    TEMPLATE_OSAL_THREAD_PRIO_HIGH     = 2,  //!< Time-sensitive tasks.
    TEMPLATE_OSAL_THREAD_PRIO_CRITICAL = 3   //!< Critical real-time tasks.
} Template_osalThreadPrio_e;

/**
 * \brief Number of supported OSAL thread priority levels.
 */
#define TEMPLATE_OSAL_THREAD_PRIO_MAX_COUNT    (4u)

/**
 * \brief Template OSAL thread attributes structure.
 */
typedef struct
{
    Template_osalThreadWorker_f worker;     /*!< Worker entry function. */
    const char                  *name;      /*!< Optional thread name. */
    size_t                      stackSize;  /*!< Expressed in bytes. */
    void                        *args;      /*!< Worker arg parameter (passed as arg to worker). */
    Template_osalThreadPrio_e   prio;       /*!< Thread priority. */
} Template_osalThreadAttr_s;

/**
 * \brief Template OSAL thread object structure.
 */
typedef struct
{
    Template_osalThreadAttr_s   attr;       /*!< Creation attributes (snapshot). */
    Template_osalThreadHandle_t handle;   /*!< RTOS-native handle.         */
} Template_osalThread_s;
// END THREAD

/**
 * \struct  Template_osalVtable_s
 * \brief OS Abstraction Layer (OSAL) vtable methods for a specific RTOS port.
 * \details Function pointers for OS-specific operations: queues, mutexes, semaphores, event flags, threads, time, memory,
 *          plus a state predicate. All pointers must be assigned by the port.
 */
typedef struct
{
    // BEGIN QUEUE
    /*------------------------------------ Queues ------------------------------------*/

    /**
     * \brief Create a message queue.
     *
     * \param osal           OSAL instance pointer.
     * \param queueItemSize  Size of a single queue item in bytes.
     * \param queueDepth     Maximum number of items the queue can hold.
     * \param queueHandle    Output: created queue handle (must not be NULL).
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueCreate)(void *const osal,
                                      const size_t queueItemSize,
                                      const size_t queueDepth,
                                      Template_osalQueueHandle_t *const queueHandle);

    /**
     * \brief Delete a message queue.
     *
     * \param osal         OSAL instance pointer.
     * \param queueHandle  Queue handle to delete.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueDelete)(void *const osal,
                                      const Template_osalQueueHandle_t queueHandle);

    /**
     * \brief Put an item into a queue.
     *
     * \param osal          OSAL instance pointer.
     * \param queueHandle   Queue handle.
     * \param queueItemPtr  Pointer to the item to enqueue.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueItemPut)(void *const osal,
                                       const Template_osalQueueHandle_t queueHandle,
                                       const void *const queueItemPtr);

    /**
     * \brief Post an item to a queue, waiting up to the requested timeout for free capacity.
     *
     * \param osal          OSAL instance pointer.
     * \param queueHandle   Queue handle.
     * \param queueItemPtr  Pointer to the item to enqueue.
     * \param timeoutMs     Maximum wait time in milliseconds.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueItemPost)(void *const osal,
                                        const Template_osalQueueHandle_t queueHandle,
                                        const void *const queueItemPtr,
                                        const Template_osalTimeMs_t timeoutMs);

    /**
     * \brief Retrieve an already available item from a queue without waiting.
     *
     * \param osal          OSAL instance pointer.
     * \param queueHandle   Queue handle.
     * \param queueItemPtr  Destination buffer for the item.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueItemGet)(void *const osal,
                                       const Template_osalQueueHandle_t queueHandle,
                                       void *const queueItemPtr);

    /**
     * \brief Wait indefinitely for an item and retrieve it from a queue.
     *
     * \param osal          OSAL instance pointer.
     * \param queueHandle   Queue handle.
     * \param queueItemPtr  Destination buffer for the item.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueItemWait)(void *const osal,
                                        const Template_osalQueueHandle_t queueHandle,
                                        void *const queueItemPtr);

    /**
     * \brief Get an item from a queue (blocking with timeout).
     *
     * \param osal          OSAL instance pointer.
     * \param queueHandle   Queue handle.
     * \param queueItemPtr  Destination buffer for the item.
     * \param timeoutMs     Timeout in milliseconds.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueItemPend)(void *const osal,
                                        const Template_osalQueueHandle_t queueHandle,
                                        void *const queueItemPtr,
                                        const Template_osalTimeMs_t timeoutMs);

    /**
     * \brief Reset a queue (discard all items).
     *
     * \param osal         OSAL instance pointer.
     * \param queueHandle  Queue handle to reset.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*queueReset)(void *const osal,
                                     const Template_osalQueueHandle_t queueHandle);
    // END QUEUE

    // BEGIN STREAM_BUFFER
    /*-------------------------------- Stream buffers --------------------------------*/

    /**
     * \brief Create a byte stream buffer.
     *
     * \param osal                OSAL instance pointer.
     * \param bufferSizeBytes     Stream-buffer capacity in bytes.
     * \param triggerLevelBytes   Trigger level in bytes used by the backend read operation.
     * \param streamBufferHandle  Output pointer receiving the created stream-buffer handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferCreate)(void *const osal,
                                             const size_t bufferSizeBytes,
                                             const size_t triggerLevelBytes,
                                             Template_osalStreamBufferHandle_t *const streamBufferHandle);

    /**
     * \brief Delete a byte stream buffer.
     *
     * \param osal                OSAL instance pointer.
     * \param streamBufferHandle  Stream-buffer handle to delete.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferDelete)(void *const osal,
                                             const Template_osalStreamBufferHandle_t streamBufferHandle);

    /**
     * \brief Put bytes into a stream buffer without waiting for free capacity.
     *
     * \param osal                OSAL instance pointer.
     * \param streamBufferHandle  Target stream-buffer handle.
     * \param data                Pointer to the source byte buffer.
     * \param dataLengthBytes     Number of bytes requested for transfer; must be non-zero.
     * \param bytesPut            Output pointer receiving the number of bytes actually written.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferPut)(void *const osal,
                                          const Template_osalStreamBufferHandle_t streamBufferHandle,
                                          const void *const data,
                                          const size_t dataLengthBytes,
                                          size_t *const bytesPut);

    /**
     * \brief Put bytes into a stream buffer, waiting up to the requested timeout for free capacity.
     *
     * \param osal                OSAL instance pointer.
     * \param streamBufferHandle  Target stream-buffer handle.
     * \param data                Pointer to the source byte buffer.
     * \param dataLengthBytes     Number of bytes requested for transfer; must be non-zero.
     * \param timeoutMs           Maximum wait time in milliseconds.
     * \param bytesPut            Output pointer receiving the number of bytes actually written.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferPost)(void *const osal,
                                           const Template_osalStreamBufferHandle_t streamBufferHandle,
                                           const void *const data,
                                           const size_t dataLengthBytes,
                                           const Template_osalTimeMs_t timeoutMs,
                                           size_t *const bytesPut);

    /**
     * \brief Get already available bytes from a stream buffer without waiting.
     *
     * \param osal                OSAL instance pointer.
     * \param streamBufferHandle  Source stream-buffer handle.
     * \param data                Destination byte buffer.
     * \param dataLengthBytes     Maximum number of bytes to transfer; must be non-zero.
     * \param bytesGet            Output pointer receiving the number of bytes actually read.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferGet)(void *const osal,
                                          const Template_osalStreamBufferHandle_t streamBufferHandle,
                                          void *const data,
                                          const size_t dataLengthBytes,
                                          size_t *const bytesGet);

    /**
     * \brief Wait indefinitely for bytes and get them from a stream buffer.
     *
     * \param osal                OSAL instance pointer.
     * \param streamBufferHandle  Source stream-buffer handle.
     * \param data                Destination byte buffer.
     * \param dataLengthBytes     Maximum number of bytes to transfer; must be non-zero.
     * \param bytesGet            Output pointer receiving the number of bytes actually read.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferWait)(void *const osal,
                                           const Template_osalStreamBufferHandle_t streamBufferHandle,
                                           void *const data,
                                           const size_t dataLengthBytes,
                                           size_t *const bytesGet);

    /**
     * \brief Get bytes from a stream buffer, waiting up to the requested timeout for data.
     *
     * \param osal                OSAL instance pointer.
     * \param streamBufferHandle  Source stream-buffer handle.
     * \param data                Destination byte buffer.
     * \param dataLengthBytes     Maximum number of bytes to transfer; must be non-zero.
     * \param timeoutMs           Maximum wait time in milliseconds.
     * \param bytesGet            Output pointer receiving the number of bytes actually read.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferPend)(void *const osal,
                                           const Template_osalStreamBufferHandle_t streamBufferHandle,
                                           void *const data,
                                           const size_t dataLengthBytes,
                                           const Template_osalTimeMs_t timeoutMs,
                                           size_t *const bytesGet);

    /**
     * \brief Reset a stream buffer to its initial empty state.
     *
     * \param osal                OSAL instance pointer.
     * \param streamBufferHandle  Stream-buffer handle to reset.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*streamBufferReset)(void *const osal,
                                            const Template_osalStreamBufferHandle_t streamBufferHandle);
    // END STREAM_BUFFER

    // BEGIN MUTEX
    /*------------------------------------ Mutexes -------------------------------------*/

    /**
     * \brief Create a mutex.
     *
     * \param osal         OSAL instance pointer.
     * \param mutexHandle  Output pointer receiving the created mutex handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*mutexCreate)(void *const osal,
                                      Template_osalMutexHandle_t *const mutexHandle);

    /**
     * \brief Delete a mutex.
     *
     * \param osal         OSAL instance pointer.
     * \param mutexHandle  Mutex handle to delete.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*mutexDelete)(void *const osal,
                                      const Template_osalMutexHandle_t mutexHandle);

    /**
     * \brief Lock a mutex and wait indefinitely until it becomes available.
     *
     * \param osal         OSAL instance pointer.
     * \param mutexHandle  Mutex handle to lock.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*mutexLock)(void *const osal,
                                    const Template_osalMutexHandle_t mutexHandle);

    /**
     * \brief Try to lock a mutex without waiting.
     *
     * \param osal         OSAL instance pointer.
     * \param mutexHandle  Mutex handle to lock.
     *
     * \return Template_osalErr_e, zero value = success; otherwise the mutex was not acquired or an error occurred.
     */
    Template_osalErr_e (*mutexTryLock)(void *const osal,
                                       const Template_osalMutexHandle_t mutexHandle);

    /**
     * \brief Lock a mutex, waiting up to the requested timeout for it to become available.
     *
     * \param osal         OSAL instance pointer.
     * \param mutexHandle  Mutex handle to lock.
     * \param timeoutMs    Maximum wait time in milliseconds.
     *
     * \return Template_osalErr_e, zero value = success; otherwise the mutex was not acquired or an error occurred.
     */
    Template_osalErr_e (*mutexPendLock)(void *const osal,
                                        const Template_osalMutexHandle_t mutexHandle,
                                        const Template_osalTimeMs_t timeoutMs);

    /**
     * \brief Unlock a previously locked mutex.
     *
     * \param osal         OSAL instance pointer.
     * \param mutexHandle  Mutex handle to unlock.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*mutexUnlock)(void *const osal,
                                      const Template_osalMutexHandle_t mutexHandle);
    // END MUTEX

    // BEGIN SEMAPHORE
    /*------------------------------ Counting semaphores -----------------------------*/

    /**
     * \brief Create a counting semaphore.
     *
     * \param osal             OSAL instance pointer.
     * \param maxCount         Maximum semaphore count.
     * \param initialCount     Initial semaphore count.
     * \param semaphoreHandle  Output pointer receiving the created semaphore handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreCreate)(void *const osal,
                                          const Template_osalSemaphoreCount_t maxCount,
                                          const Template_osalSemaphoreCount_t initialCount,
                                          Template_osalSemaphoreHandle_t *const semaphoreHandle);

    /**
     * \brief Delete a counting semaphore.
     *
     * \param osal             OSAL instance pointer.
     * \param semaphoreHandle  Counting-semaphore handle to delete.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreDelete)(void *const osal,
                                          const Template_osalSemaphoreHandle_t semaphoreHandle);

    /**
     * \brief Wait indefinitely for one counting-semaphore count.
     *
     * \param osal             OSAL instance pointer.
     * \param semaphoreHandle  Counting-semaphore handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreWait)(void *const osal,
                                        const Template_osalSemaphoreHandle_t semaphoreHandle);

    /**
     * \brief Wait for one counting-semaphore count up to an explicit timeout.
     *
     * \param osal             OSAL instance pointer.
     * \param semaphoreHandle  Counting-semaphore handle.
     * \param timeoutMs        Maximum wait time in milliseconds.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphorePend)(void *const osal,
                                        const Template_osalSemaphoreHandle_t semaphoreHandle,
                                        const Template_osalTimeMs_t timeoutMs);

    /**
     * \brief Post one count to a counting semaphore.
     *
     * \param osal             OSAL instance pointer.
     * \param semaphoreHandle  Counting-semaphore handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphorePost)(void *const osal,
                                        const Template_osalSemaphoreHandle_t semaphoreHandle);

    /**
     * \brief Read the current counting-semaphore count.
     *
     * \param osal             OSAL instance pointer.
     * \param semaphoreHandle  Counting-semaphore handle.
     * \param semaphoreCount   Output pointer receiving the current semaphore count.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*semaphoreCountGet)(void *const osal,
                                            const Template_osalSemaphoreHandle_t semaphoreHandle,
                                            Template_osalSemaphoreCount_t *const semaphoreCount);
    // END SEMAPHORE

    // BEGIN EVENT_FLAGS
    /*-------------------------------- Event flags --------------------------------*/

    /**
     * \brief Create an event flags object and register its opaque handle.
     *
     * \param osal              OSAL instance pointer.
     * \param eventFlagsHandle  Output pointer receiving the created event flags handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*eventFlagsCreate)(void *const osal,
                                           Template_osalEventFlagsHandle_t *const eventFlagsHandle);

    /**
     * \brief Delete an event flags object and release its registry slot.
     *
     * \param osal              OSAL instance pointer.
     * \param eventFlagsHandle  Event flags handle to delete.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*eventFlagsDelete)(void *const osal,
                                           const Template_osalEventFlagsHandle_t eventFlagsHandle);

    /**
     * \brief Atomically set one or more event flag bits.
     *
     * \param osal              OSAL instance pointer.
     * \param eventFlagsHandle  Event flags handle.
     * \param flags             Non-zero bit mask of flags to set.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*eventFlagsSet)(void *const osal,
                                        const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                        const uint32_t flags);

    /**
     * \brief Atomically clear one or more event flag bits.
     *
     * \param osal              OSAL instance pointer.
     * \param eventFlagsHandle  Event flags handle.
     * \param flags             Non-zero bit mask of flags to clear.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*eventFlagsClear)(void *const osal,
                                          const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                          const uint32_t flags);

    /**
     * \brief Read the currently set event flag bits.
     *
     * \param osal              OSAL instance pointer.
     * \param eventFlagsHandle  Event flags handle.
     * \param flags             Output pointer receiving the current event flags snapshot.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*eventFlagsGet)(void *const osal,
                                        const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                        uint32_t *const flags);

    /**
     * \brief Wait for any or all requested event flag bits.
     *
     * \param osal              OSAL instance pointer.
     * \param eventFlagsHandle  Event flags handle.
     * \param flags             Non-zero bit mask of flags to wait for.
     * \param options           WAIT_ANY or WAIT_ALL, optionally combined with NO_CLEAR.
     * \param timeoutMs         Maximum wait time in milliseconds.
     * \param actualFlags       Output pointer receiving the event flags snapshot observed when the wait completes.
     *
     * \return Template_osalErr_e, zero value = success, otherwise the wait condition was not satisfied or an error occurred.
     */
    Template_osalErr_e (*eventFlagsWait)(void *const osal,
                                         const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                         const uint32_t flags,
                                         const Template_osalEventFlagsOptions_e options,
                                         const Template_osalTimeMs_t timeoutMs,
                                         uint32_t *const actualFlags);
    // END EVENT_FLAGS

    // BEGIN THREAD
    /*----------------------------------- Threads ------------------------------------*/

    /**
     * \brief Create a new thread.
     *
     * \param osal          Pointer to the OSAL instance.
     * \param threadHandle  Pointer to store the handle of the created thread.
     * \param threadAttr    Configuration parameters for the thread.
     *
     * \return Template_osalErr_e error code, non-zero indicates error.
     */
    Template_osalErr_e (*threadCreate)(void *const osal,
                                       Template_osalThreadHandle_t *const threadHandle,
                                       Template_osalThreadAttr_s threadAttr);

    /**
     * \brief Delete the thread.
     *
     * \note The operation must be stopped before deleting the thread to avoid system damage.
     *
     * \param osal          Pointer to OSAL instance.
     * \param threadHandle  Handle of the thread being deleted.
     *
     * \return Template_osalErr_e error code, non-zero indicates error.
     */
    Template_osalErr_e (*threadDelete)(void *const osal,
                                       const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief Suspend the thread.
     *
     * \param osal          Pointer to OSAL instance.
     * \param threadHandle  Handle of the thread to suspend.
     *
     * \return Template_osalErr_e error code, non-zero indicates error.
     */
    Template_osalErr_e (*threadSuspend)(void *const osal,
                                        const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief Resume the thread.
     *
     * \param osal          Pointer to OSAL instance.
     * \param threadHandle  Handle of the thread to resume.
     *
     * \return Template_osalErr_e error code, non-zero indicates error.
     */
    Template_osalErr_e (*threadResume)(void *const osal,
                                       const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief Delay the execution of the current thread.
     *
     * \param osal     Pointer to OSAL instance.
     * \param delayMs  Delay duration in milliseconds.
     *
     * \return Template_osalErr_e error code, non-zero indicates error.
     */
    Template_osalErr_e (*threadDelay)(void *const osal,
                                      const Template_osalTimeMs_t delayMs);

    /**
     * \brief Delay the current thread until the next periodic wake-up point.
     *
     * \param osal                OSAL instance pointer.
     * \param previousWakeTimeMs  In/out scheduled wake reference in milliseconds; updated to the next reference point.
     * \param periodMs            Period in milliseconds; must be non-zero.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*threadDelayUntil)(void *const osal,
                                           Template_osalTimeMs_t *const previousWakeTimeMs,
                                           const Template_osalTimeMs_t periodMs);

    /**
     * \brief Terminate the calling thread (does not return).
     *
     * \param osal  Pointer to OSAL instance (must be valid).
     *
     * \note This function never returns control to the caller.
     */
    void (*threadExit)(void *const osal);
    // END THREAD

    // BEGIN CRITICAL_SECTION
    /*------------------------------- Critical section ------------------------------*/

    /**
     * \brief Enter a short OS critical section.
     *
     * \param osal  OSAL instance pointer.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*criticalSectionEnter)(void *const osal);

    /**
     * \brief Exit a previously entered OS critical section.
     *
     * \param osal  OSAL instance pointer.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*criticalSectionExit)(void *const osal);
    // END CRITICAL_SECTION

    // BEGIN SOFTWARE_TIMER
    /*-------------------------------- Software timers -------------------------------*/

    /**
     * \brief Create a one-shot or auto-reload software timer.
     *
     * \param osal         OSAL instance pointer.
     * \param timerHandle  Output pointer receiving the created timer handle.
     * \param timerAttr    Software timer configuration.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerCreate)(void *const osal,
                                              Template_osalSoftwareTimerHandle_t *const timerHandle,
                                              Template_osalSoftwareTimerAttr_s timerAttr);

    /**
     * \brief Delete a software timer.
     *
     * \param osal         OSAL instance pointer.
     * \param timerHandle  Software timer handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerDelete)(void *const osal,
                                              const Template_osalSoftwareTimerHandle_t timerHandle);

    /**
     * \brief Start a software timer using its configured period.
     *
     * \param osal         OSAL instance pointer.
     * \param timerHandle  Software timer handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerStart)(void *const osal,
                                             const Template_osalSoftwareTimerHandle_t timerHandle);

    /**
     * \brief Stop a software timer.
     *
     * \param osal         OSAL instance pointer.
     * \param timerHandle  Software timer handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerStop)(void *const osal,
                                            const Template_osalSoftwareTimerHandle_t timerHandle);

    /**
     * \brief Reset a software timer and restart its configured period.
     *
     * \param osal         OSAL instance pointer.
     * \param timerHandle  Software timer handle.
     *
     * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
     */
    Template_osalErr_e (*softwareTimerReset)(void *const osal,
                                             const Template_osalSoftwareTimerHandle_t timerHandle);
    // END SOFTWARE_TIMER

    // BEGIN TIME
    /*------------------------------------- Time -------------------------------------*/

    /**
     * \brief Retrieve the current system time in milliseconds.
     *
     * \param osal      Pointer to OSAL instance.
     * \param osTimeMs  Pointer to store the current time in milliseconds.
     *
     * \return Template_osalErr_e error code, non-zero indicates error.
     */
    Template_osalErr_e (*timeMsGet)(void *const osal,
                                    Template_osalTimeMs_t *const osTimeMs);
    // END TIME

    // BEGIN MEMORY
    /*------------------------------------- Memory -----------------------------------*/

    /**
     * \brief Allocates a memory block in the backend.
     *
     * \param osal    OSAL instance.
     * \param size    Allocation size in bytes.
     * \param memPtr  Pointer to store the allocated memory address.
     *
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*memAlloc)(void *const osal,
                                   const size_t size,
                                   void **const memPtr);

    /**
     * \brief Frees a previously allocated memory block in the backend.
     *
     * \param osal  OSAL instance.
     * \param memPtr  Pointer to a block previously returned by memAlloc.
     *
     * \return Template_osalErr_e (0 on success).
     */
    Template_osalErr_e (*memFree)(void *const osal,
                                  void *const memPtr);
    // END MEMORY

    /*---------------------------------- Predicate -----------------------------------*/

    /**
     * \brief Validate OSAL instance (including vtable layer).
     *
     * \param osal  OSAL instance pointer (const).
     *
     * \return true if valid/initialized; false otherwise.
     */
    bool (*isValid)(const void *const osal);
} Template_osalVtable_s;

/*-------------------------------- Protected  -------------------------------*/

/**
 * \struct  Template_osalPtable_s
 * \brief Protected OSAL helpers for backend ports (registry utilities etc).
 * \details Internal slot/resource lookup helpers for every registry-backed primitive group.
 *          Returned IDs are 1-based (index + 1). Zero value indicates "not found"/"no free slot".
 * \note    This is a ptable API intended for OSAL backends only.
 */
typedef struct
{
    // BEGIN QUEUE
    /*------------------------------- Queues --------------------------------*/

    /**
     * \brief Find a free queue slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*queueFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find a queue handle in the internal registry.
     *
     * \param osalPort     Derived OSAL instance pointer (opaque in base).
     * \param queueHandle  Queue handle to search.
     *
     * \return size_t       Queue ID (index + 1) if found; 0 otherwise.
     */
    size_t (*queueHandleFind)(void *const osalPort,
                              const Template_osalQueueHandle_t queueHandle);

    // END QUEUE

    // BEGIN STREAM_BUFFER
    /*----------------------------- Stream buffers ----------------------------*/

    /**
     * \brief Find a free stream-buffer slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t    Slot ID (index + 1) if a free slot exists; 0 otherwise.
     */
    size_t (*streamBufferFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find a stream-buffer handle in the internal registry.
     *
     * \param osalPort            Derived OSAL instance pointer (opaque in base).
     * \param streamBufferHandle  Stream-buffer handle to search.
     *
     * \return size_t             Stream-buffer ID (index + 1) if found; 0 otherwise.
     */
    size_t (*streamBufferHandleFind)(void *const osalPort,
                                     const Template_osalStreamBufferHandle_t streamBufferHandle);
    // END STREAM_BUFFER

    // BEGIN MUTEX
    /*-------------------------------- Mutexes --------------------------------*/

    /**
     * \brief Find a free mutex slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*mutexFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find a mutex handle in the internal registry.
     *
     * \param osalPort     Derived OSAL instance pointer (opaque in base).
     * \param mutexHandle  Mutex handle to search.
     *
     * \return size_t        Mutex ID (index + 1) if found; 0 otherwise.
     */
    size_t (*mutexHandleFind)(void *const osalPort,
                              const Template_osalMutexHandle_t mutexHandle);
    // END MUTEX

    // BEGIN SEMAPHORE
    /*--------------------------- Counting semaphores --------------------------*/

    /**
     * \brief Find a free counting-semaphore slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t    Slot ID (index + 1) if a free slot exists; 0 otherwise.
     */
    size_t (*semaphoreFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find a counting-semaphore handle in the internal registry.
     *
     * \param osalPort         Derived OSAL instance pointer (opaque in base).
     * \param semaphoreHandle  Counting-semaphore handle to search.
     *
     * \return size_t          Semaphore ID (index + 1) if found; 0 otherwise.
     */
    size_t (*semaphoreHandleFind)(void *const osalPort,
                                  const Template_osalSemaphoreHandle_t semaphoreHandle);
    // END SEMAPHORE

    // BEGIN EVENT_FLAGS
    /*------------------------------- Event flags -------------------------------*/

    /**
     * \brief Find a free event-flags slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t, slot ID (index + 1) if a free slot exists; 0 otherwise.
     */
    size_t (*eventFlagsFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find an event-flags handle in the internal registry.
     *
     * \param osalPort          Derived OSAL instance pointer (opaque in base).
     * \param eventFlagsHandle  Event flags handle to search.
     *
     * \return size_t, event-flags ID (index + 1) if found; 0 otherwise.
     */
    size_t (*eventFlagsHandleFind)(void *const osalPort,
                                   const Template_osalEventFlagsHandle_t eventFlagsHandle);
    // END EVENT_FLAGS

    // BEGIN THREAD
    /*------------------------------- Threads -------------------------------*/

    /**
     * \brief Find a free thread slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*threadFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find a thread handle in the internal registry.
     *
     * \param osalPort      Derived OSAL instance pointer (opaque in base).
     * \param threadHandle  Thread handle to search.
     *
     * \return size_t       Thread ID (index + 1) if found; 0 otherwise.
     */
    size_t (*threadHandleFind)(void *const osalPort,
                               const Template_osalThreadHandle_t threadHandle);

    /**
     * \brief Clear a thread registry slot.
     *
     * \param osalPort   Derived OSAL instance pointer (opaque in base).
     * \param threadIdx  Zero-based thread registry index.
     */
    void (*threadSlotClear)(void *const osalPort,
                            const size_t threadIdx);
    // END THREAD

    // BEGIN SOFTWARE_TIMER
    /*----------------------------- Software timers ----------------------------*/

    /**
     * \brief Find a free software-timer slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t    Slot ID (index + 1) if a free slot exists; 0 otherwise.
     */
    size_t (*softwareTimerFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find a software-timer handle in the internal registry.
     *
     * \param osalPort     Derived OSAL instance pointer (opaque in base).
     * \param timerHandle  Software-timer handle to search.
     *
     * \return size_t      Software-timer ID (index + 1) if found; 0 otherwise.
     */
    size_t (*softwareTimerHandleFind)(void *const osalPort,
                                      const Template_osalSoftwareTimerHandle_t timerHandle);
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    /*-------------------------------- Memory --------------------------------*/

    /**
     * \brief Find a free memory slot in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     *
     * \return size_t    Slot ID (index + 1) if free slot exists; 0 otherwise.
     */
    size_t (*memFreeSlotFind)(void *const osalPort);

    /**
     * \brief Find a registered memory pointer in the internal registry.
     *
     * \param osalPort  Derived OSAL instance pointer (opaque in base).
     * \param memPtr    Memory pointer to search.
     *
     * \return size_t    Memory slot ID (index + 1) if found; 0 otherwise.
     */
    size_t (*memPtrFind)(void *const osalPort,
                         const void *const memPtr);
    // END MEMORY

    uint8_t reserved; /*!< Keeps the protected table valid when no registry-backed primitive is selected. */
} Template_osalPtable_s;


/**
 * \struct  Template_osal_s
 * \brief OS Abstraction Layer (OSAL) interface descriptor.
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

    // BEGIN MUTEX
    /* Mutex handles */
    Template_osalMutexHandle_t mutexHandle[TEMPLATE_OSAL_MUTEX_SLOTS_NUM];
    // END MUTEX

    // BEGIN SEMAPHORE
    /* Counting semaphore handles */
    Template_osalSemaphoreHandle_t semaphoreObjHandle[TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM];
    // END SEMAPHORE

    // BEGIN EVENT_FLAGS
    /* Event flags handles */
    Template_osalEventFlagsHandle_t eventFlagsObjHandle[TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM];
    // END EVENT_FLAGS

    // BEGIN THREAD
    /* Threads handles */
    Template_osalThread_s threadObjHandle[TEMPLATE_OSAL_THREAD_SLOTS_NUM];
    // END THREAD

    // BEGIN SOFTWARE_TIMER
    /* Software timer objects */
    Template_osalSoftwareTimer_s softwareTimerObj[TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM];
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    /* Registered memory pointers */
    void *memPtr[TEMPLATE_OSAL_MEM_SLOTS_NUM];
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
 * \brief Initialize Template OSAL instance; set name/parent and clear internal objects.
 *
 * \param osal    OSAL instance pointer.
 * \param name    Optional instance name (may be NULL).
 * \param parent  Optional parent pointer (opaque; may be NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalInit(Template_osal_s *const osal,
                                     const char *name,
                                     void *const parent);

/**
 * \brief Deinitialize Template OSAL instance.
 *
 * \param osal  OSAL instance pointer.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalDeinit(Template_osal_s *const osal);

/**
 * \brief Validate OSAL instance (including vtable layer).
 *
 * \param osal  OSAL instance pointer (const).
 *
 * \return true if valid/initialized; false otherwise.
 */
bool template_osalIsValid(const Template_osal_s *const osal);

/*----------------------------------- Metadata --------------------------------*/

/**
 * \brief Get pointer to a parent of the given OSAL object.
 *
 * \param osal    OSAL instance pointer.
 * \param parent  Output: pointer to parent (must not be NULL; may be set to NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalParentGet(Template_osal_s *const osal,
                                          void **const parent);

/**
 * \brief Set the parent object for the given OSAL instance.
 *
 * \param osal    OSAL instance pointer.
 * \param parent  Parent pointer to be set (may be NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalParentSet(Template_osal_s *const osal,
                                          const void *const parent);

/**
 * \brief Get pointer to the name field of the given OSAL instance.
 *
 * \param osal  OSAL instance pointer.
 * \param name  Output: pointer to name (must not be NULL; may be set to NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalNameGet(Template_osal_s *const osal,
                                        const char **const name);

/**
 * \brief Set name for the given OSAL instance.
 *
 * \param osal  OSAL instance pointer.
 * \param name  Pointer to name string to set (may be NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalNameSet(Template_osal_s *const osal,
                                        const char *const name);

// BEGIN QUEUE
/*------------------------------------ Queues ---------------------------------*/

/**
 * \brief Create a message queue.
 *
 * \param osal           OSAL instance pointer.
 * \param queueItemSize  Size of a single queue item in bytes.
 * \param queueDepth     Maximum number of items the queue can hold.
 * \param queueHandle    Output: created queue handle (must not be NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueCreate(Template_osal_s *const osal,
                                            const size_t queueItemSize,
                                            const size_t queueDepth,
                                            Template_osalQueueHandle_t *const queueHandle);

/**
 * \brief Delete a message queue.
 *
 * \param osal         OSAL instance pointer.
 * \param queueHandle  Queue handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueDelete(Template_osal_s *const osal,
                                            const Template_osalQueueHandle_t queueHandle);

/**
 * \brief Put an item into a queue.
 *
 * \param osal          OSAL instance pointer.
 * \param queueHandle   Queue handle.
 * \param queueItemPtr  Pointer to the item to enqueue.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueItemPut(Template_osal_s *const osal,
                                             const Template_osalQueueHandle_t queueHandle,
                                             const void *const queueItemPtr);

/**
 * \brief Post an item to a queue, waiting up to the requested timeout for free capacity.
 *
 * \param osal          OSAL instance pointer.
 * \param queueHandle   Queue handle.
 * \param queueItemPtr  Pointer to the item to enqueue.
 * \param timeoutMs     Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueItemPost(Template_osal_s *const osal,
                                              const Template_osalQueueHandle_t queueHandle,
                                              const void *const queueItemPtr,
                                              const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Retrieve an already available item from a queue without waiting.
 *
 * \param osal          OSAL instance pointer.
 * \param queueHandle   Queue handle.
 * \param queueItemPtr  Destination buffer for the item.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueItemGet(Template_osal_s *const osal,
                                             const Template_osalQueueHandle_t queueHandle,
                                             void *const queueItemPtr);

/**
 * \brief Wait indefinitely for an item and retrieve it from a queue.
 *
 * \param osal          OSAL instance pointer.
 * \param queueHandle   Queue handle.
 * \param queueItemPtr  Destination buffer for the item.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueItemWait(Template_osal_s *const osal,
                                              const Template_osalQueueHandle_t queueHandle,
                                              void *const queueItemPtr);

/**
 * \brief Get an item from a queue (blocking with timeout).
 *
 * \param osal          OSAL instance pointer.
 * \param queueHandle   Queue handle.
 * \param queueItemPtr  Destination buffer for the item.
 * \param timeoutMs     Timeout in milliseconds.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueItemPend(Template_osal_s *const osal,
                                              const Template_osalQueueHandle_t queueHandle,
                                              void *const queueItemPtr,
                                              const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Reset a queue (discard all items).
 *
 * \param osal         OSAL instance pointer.
 * \param queueHandle  Queue handle to reset.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueReset(Template_osal_s *const osal,
                                           const Template_osalQueueHandle_t queueHandle);

/**
 * \brief Get a queue handle of the given OSAL object.
 *
 * \param osal          Pointer to OSAL instance.
 * \param queueSlotInd  Index of queue slot.
 * \param queueHandle   Pointer to the current queue handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueHandleGet(Template_osal_s *const osal,
                                               const size_t queueSlotInd,
                                               Template_osalQueueHandle_t *const queueHandle);

// END QUEUE

// BEGIN STREAM_BUFFER
/*-------------------------------- Stream buffers --------------------------------*/

/**
 * \brief Create a byte stream buffer.
 *
 * \param osal                OSAL instance pointer.
 * \param bufferSizeBytes     Stream-buffer capacity in bytes.
 * \param triggerLevelBytes   Trigger level in bytes used by the backend read operation.
 * \param streamBufferHandle  Output pointer receiving the created stream-buffer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferCreate(Template_osal_s *const osal,
                                                   const size_t bufferSizeBytes,
                                                   const size_t triggerLevelBytes,
                                                   Template_osalStreamBufferHandle_t *const streamBufferHandle);

/**
 * \brief Delete a byte stream buffer.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Stream-buffer handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferDelete(Template_osal_s *const osal,
                                                   const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Put bytes into a stream buffer without waiting for free capacity.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Target stream-buffer handle.
 * \param data                Pointer to the source byte buffer.
 * \param dataLengthBytes     Number of bytes requested for transfer; must be non-zero.
 * \param bytesPut            Output pointer receiving the number of bytes actually written.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferPut(Template_osal_s *const osal,
                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                const void *const data,
                                                const size_t dataLengthBytes,
                                                size_t *const bytesPut);

/**
 * \brief Put bytes into a stream buffer, waiting up to the requested timeout for free capacity.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Target stream-buffer handle.
 * \param data                Pointer to the source byte buffer.
 * \param dataLengthBytes     Number of bytes requested for transfer; must be non-zero.
 * \param timeoutMs           Maximum wait time in milliseconds.
 * \param bytesPut            Output pointer receiving the number of bytes actually written.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferPost(Template_osal_s *const osal,
                                                 const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                 const void *const data,
                                                 const size_t dataLengthBytes,
                                                 const Template_osalTimeMs_t timeoutMs,
                                                 size_t *const bytesPut);

/**
 * \brief Get already available bytes from a stream buffer without waiting.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Source stream-buffer handle.
 * \param data                Destination byte buffer.
 * \param dataLengthBytes     Maximum number of bytes to transfer; must be non-zero.
 * \param bytesGet            Output pointer receiving the number of bytes actually read.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferGet(Template_osal_s *const osal,
                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                void *const data,
                                                const size_t dataLengthBytes,
                                                size_t *const bytesGet);

/**
 * \brief Wait indefinitely for bytes and get them from a stream buffer.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Source stream-buffer handle.
 * \param data                Destination byte buffer.
 * \param dataLengthBytes     Maximum number of bytes to transfer; must be non-zero.
 * \param bytesGet            Output pointer receiving the number of bytes actually read.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferWait(Template_osal_s *const osal,
                                                 const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                 void *const data,
                                                 const size_t dataLengthBytes,
                                                 size_t *const bytesGet);

/**
 * \brief Get bytes from a stream buffer, waiting up to the requested timeout for data.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Source stream-buffer handle.
 * \param data                Destination byte buffer.
 * \param dataLengthBytes     Maximum number of bytes to transfer; must be non-zero.
 * \param timeoutMs           Maximum wait time in milliseconds.
 * \param bytesGet            Output pointer receiving the number of bytes actually read.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferPend(Template_osal_s *const osal,
                                                 const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                 void *const data,
                                                 const size_t dataLengthBytes,
                                                 const Template_osalTimeMs_t timeoutMs,
                                                 size_t *const bytesGet);

/**
 * \brief Reset a stream buffer to its initial empty state.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Stream-buffer handle to reset.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferReset(Template_osal_s *const osal,
                                                  const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief Get a stream-buffer handle from a stable registry slot.
 *
 * \param osal                 OSAL instance pointer.
 * \param streamBufferSlotInd  Zero-based stream-buffer registry slot index.
 * \param streamBufferHandle   Output pointer receiving the current slot handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferHandleGet(Template_osal_s *const osal,
                                                      const size_t streamBufferSlotInd,
                                                      Template_osalStreamBufferHandle_t *const streamBufferHandle);
// END STREAM_BUFFER

// BEGIN MUTEX
/*------------------------------------- Mutexes --------------------------------*/

/**
 * \brief Create a mutex.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Output pointer receiving the created mutex handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexCreate(Template_osal_s *const osal,
                                            Template_osalMutexHandle_t *const mutexHandle);

/**
 * \brief Delete a mutex.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexDelete(Template_osal_s *const osal,
                                            const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Lock a mutex and wait indefinitely until it becomes available.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to lock.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexLock(Template_osal_s *const osal,
                                          const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Try to lock a mutex without waiting.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to lock.
 *
 * \return Template_osalErr_e, zero value = success; otherwise the mutex was not acquired or an error occurred.
 */
Template_osalErr_e template_osalMutexTryLock(Template_osal_s *const osal,
                                             const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Lock a mutex, waiting up to the requested timeout for it to become available.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to lock.
 * \param timeoutMs    Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value = success; otherwise the mutex was not acquired or an error occurred.
 */
Template_osalErr_e template_osalMutexPendLock(Template_osal_s *const osal,
                                              const Template_osalMutexHandle_t mutexHandle,
                                              const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Unlock a previously locked mutex.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to unlock.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexUnlock(Template_osal_s *const osal,
                                            const Template_osalMutexHandle_t mutexHandle);

/**
 * \brief Get a mutex handle from a stable registry slot.
 *
 * \param osal          OSAL instance pointer.
 * \param mutexSlotInd  Zero-based mutex registry slot index.
 * \param mutexHandle   Output pointer receiving the current slot handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexHandleGet(Template_osal_s *const osal,
                                               const size_t mutexSlotInd,
                                               Template_osalMutexHandle_t *const mutexHandle);
// END MUTEX

// BEGIN SEMAPHORE
/*------------------------------ Counting semaphores ---------------------------*/

/**
 * \brief Create a counting semaphore.
 *
 * \param osal             OSAL instance pointer.
 * \param maxCount         Maximum semaphore count.
 * \param initialCount     Initial semaphore count.
 * \param semaphoreHandle  Output pointer receiving the created semaphore handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreCreate(Template_osal_s *const osal,
                                                const Template_osalSemaphoreCount_t maxCount,
                                                const Template_osalSemaphoreCount_t initialCount,
                                                Template_osalSemaphoreHandle_t *const semaphoreHandle);

/**
 * \brief Delete a counting semaphore.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreDelete(Template_osal_s *const osal,
                                                const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Wait indefinitely for one counting-semaphore count.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreWait(Template_osal_s *const osal,
                                              const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Wait for one counting-semaphore count up to an explicit timeout.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle.
 * \param timeoutMs        Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphorePend(Template_osal_s *const osal,
                                              const Template_osalSemaphoreHandle_t semaphoreHandle,
                                              const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Post one count to a counting semaphore.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphorePost(Template_osal_s *const osal,
                                              const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief Read the current counting-semaphore count.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle.
 * \param semaphoreCount   Output pointer receiving the current semaphore count.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreCountGet(Template_osal_s *const osal,
                                                  const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                  Template_osalSemaphoreCount_t *const semaphoreCount);

/**
 * \brief Get a counting-semaphore handle from a stable registry slot.
 *
 * \param osal              OSAL instance pointer.
 * \param semaphoreSlotInd  Zero-based semaphore registry slot index.
 * \param semaphoreHandle   Output pointer receiving the current slot handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreHandleGet(Template_osal_s *const osal,
                                                   const size_t semaphoreSlotInd,
                                                   Template_osalSemaphoreHandle_t *const semaphoreHandle);
// END SEMAPHORE

// BEGIN EVENT_FLAGS
/*-------------------------------- Event flags -------------------------------*/

/**
 * \brief Create an event flags object and register its opaque handle.
 *
 * \param osal              OSAL instance pointer.
 * \param eventFlagsHandle  Output pointer receiving the created event flags handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalEventFlagsCreate(Template_osal_s *const osal,
                                                 Template_osalEventFlagsHandle_t *const eventFlagsHandle);

/**
 * \brief Delete an event flags object and release its registry slot.
 *
 * \param osal              OSAL instance pointer.
 * \param eventFlagsHandle  Event flags handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalEventFlagsDelete(Template_osal_s *const osal,
                                                 const Template_osalEventFlagsHandle_t eventFlagsHandle);

/**
 * \brief Atomically set one or more event flag bits.
 *
 * \param osal              OSAL instance pointer.
 * \param eventFlagsHandle  Event flags handle.
 * \param flags             Non-zero bit mask of flags to set.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalEventFlagsSet(Template_osal_s *const osal,
                                              const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                              const uint32_t flags);

/**
 * \brief Atomically clear one or more event flag bits.
 *
 * \param osal              OSAL instance pointer.
 * \param eventFlagsHandle  Event flags handle.
 * \param flags             Non-zero bit mask of flags to clear.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalEventFlagsClear(Template_osal_s *const osal,
                                                const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                                const uint32_t flags);

/**
 * \brief Read the currently set event flag bits.
 *
 * \param osal              OSAL instance pointer.
 * \param eventFlagsHandle  Event flags handle.
 * \param flags             Output pointer receiving the current event flags snapshot.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalEventFlagsGet(Template_osal_s *const osal,
                                              const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                              uint32_t *const flags);

/**
 * \brief Wait for any or all requested event flag bits.
 *
 * \param osal              OSAL instance pointer.
 * \param eventFlagsHandle  Event flags handle.
 * \param flags             Non-zero bit mask of flags to wait for.
 * \param options           WAIT_ANY or WAIT_ALL, optionally combined with NO_CLEAR.
 * \param timeoutMs         Maximum wait time in milliseconds.
 * \param actualFlags       Output pointer receiving the event flags snapshot observed when the wait completes.
 *
 * \return Template_osalErr_e, zero value = success, otherwise the wait condition was not satisfied or an error occurred.
 */
Template_osalErr_e template_osalEventFlagsWait(Template_osal_s *const osal,
                                               const Template_osalEventFlagsHandle_t eventFlagsHandle,
                                               const uint32_t flags,
                                               const Template_osalEventFlagsOptions_e options,
                                               const Template_osalTimeMs_t timeoutMs,
                                               uint32_t *const actualFlags);

/**
 * \brief Get an event flags handle from a stable registry slot.
 *
 * \param osal               OSAL instance pointer.
 * \param eventFlagsSlotInd  Zero-based event flags registry slot index.
 * \param eventFlagsHandle   Output pointer receiving the current slot handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalEventFlagsHandleGet(Template_osal_s *const osal,
                                                    const size_t eventFlagsSlotInd,
                                                    Template_osalEventFlagsHandle_t *const eventFlagsHandle);
// END EVENT_FLAGS

// BEGIN THREAD
/*------------------------------------ Threads -------------------------------*/

/**
 * \brief Create a new thread.
 *
 * \param osal          Pointer to the OSAL instance.
 * \param threadHandle  Pointer to store the handle of the created thread.
 * \param threadAttr    Configuration parameters for the thread.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadCreate(Template_osal_s *const osal,
                                             Template_osalThreadHandle_t *const threadHandle,
                                             Template_osalThreadAttr_s threadAttr);

/**
 * \brief Delete the thread.
 *
 * \note The operation must be stopped before deleting the thread to avoid system damage.
 *
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread being deleted.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadDelete(Template_osal_s *const osal,
                                             const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Suspend the thread.
 *
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to suspend.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadSuspend(Template_osal_s *const osal,
                                              const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Resume the thread.
 *
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to resume.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadResume(Template_osal_s *const osal,
                                             const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Delay the execution of the current thread.
 *
 * \param osal     Pointer to OSAL instance.
 * \param delayMs  Delay duration in milliseconds.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadDelay(Template_osal_s *const osal,
                                            const Template_osalTimeMs_t delayMs);

/**
 * \brief Delay the current thread until the next periodic wake-up point.
 *
 * \param osal                OSAL instance pointer.
 * \param previousWakeTimeMs  In/out scheduled wake reference in milliseconds; updated to the next reference point.
 * \param periodMs            Period in milliseconds; must be non-zero.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalThreadDelayUntil(Template_osal_s *const osal,
                                                 Template_osalTimeMs_t *const previousWakeTimeMs,
                                                 const Template_osalTimeMs_t periodMs);

/**
 * \brief Terminate the calling thread (does not return).
 *
 * \param osal  Pointer to OSAL instance (must be valid).
 *
 * \note This function never returns control to the caller.
 */
void template_osalThreadExit(Template_osal_s *const osal);

/**
 * \brief Get a thread handle of the given OSAL object.
 *
 * \param osal           Pointer to OSAL instance.
 * \param threadSlotInd  Index of thread slots.
 * \param threadHandle   Pointer where the thread handle will be copied.
 *
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
 *
 * \param osal  OSAL instance pointer.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalCriticalSectionEnter(Template_osal_s *const osal);

/**
 * \brief Exit a previously entered OS critical section.
 *
 * \param osal  OSAL instance pointer.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalCriticalSectionExit(Template_osal_s *const osal);
// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*-------------------------------- Software timers -----------------------------*/

/**
 * \brief Create a one-shot or auto-reload software timer.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Output pointer receiving the created timer handle.
 * \param timerAttr    Software timer configuration.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerCreate(Template_osal_s *const osal,
                                                    Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                    Template_osalSoftwareTimerAttr_s timerAttr);

/**
 * \brief Delete a software timer.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerDelete(Template_osal_s *const osal,
                                                    const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Start a software timer using its configured period.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerStart(Template_osal_s *const osal,
                                                   const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Stop a software timer.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerStop(Template_osal_s *const osal,
                                                  const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Reset a software timer and restart its configured period.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerReset(Template_osal_s *const osal,
                                                   const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Get a software timer handle from a stable registry slot.
 *
 * \param osal          OSAL instance pointer.
 * \param timerSlotInd  Zero-based software timer registry slot index.
 * \param timerHandle   Output pointer receiving the current slot handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerHandleGet(Template_osal_s *const osal,
                                                       const size_t timerSlotInd,
                                                       Template_osalSoftwareTimerHandle_t *const timerHandle);
// END SOFTWARE_TIMER

// BEGIN TIME
/*-------------------------------------- Time --------------------------------*/

/**
 * \brief Retrieve the current system time in milliseconds.
 *
 * \param osal      Pointer to OSAL instance.
 * \param osTimeMs  Pointer to store the current time in milliseconds.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalTimeMsGet(Template_osal_s *const osal,
                                          Template_osalTimeMs_t *const osTimeMs);
// END TIME

// BEGIN MEMORY
/*------------------------------------- Memory --------------------------------*/

/**
 * \brief Allocate memory via the OSAL backend and register the pointer internally.
 *
 * \param osal    Pointer to OSAL instance.
 * \param size    Allocation size in bytes.
 * \param memPtr  Output pointer receiving the allocated memory address.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalMalloc(Template_osal_s *const osal,
                                       const size_t size,
                                       void **const memPtr);

/**
 * \brief Free memory via the OSAL backend and unregister the pointer internally.
 *
 * \param osal    Pointer to OSAL instance.
 * \param memPtr  Pointer to the memory block to free (must not be NULL and must be registered).
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalFree(Template_osal_s *const osal,
                                     void *const memPtr);

/**
 * \brief Get a registered memory pointer from the specified OSAL memory registry slot.
 *
 * \param osal        Pointer to OSAL instance.
 * \param memSlotInd  Index of memory registry slot.
 * \param memPtr      Output pointer receiving the registered memory pointer.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalMemPtrGet(Template_osal_s *const osal,
                                          const size_t memSlotInd,
                                          void **const memPtr);
// END MEMORY

#ifdef __cplusplus
    }
#endif

#endif /* TEMPLATE_OSAL_H_ */
