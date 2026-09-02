/* SPDX-License-Identifier: MIT */

/**
 * \file      template_osal_freertos.c
 * \brief     FreeRTOS OSAL port for Template.
 * \details   FreeRTOS implementation of the component-scoped Template OSAL contract.
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

//====================================================================[ INTERNAL DATA TYPES DEFINITIONS ]===========================================================================

/* None */

//===============================================================[ INTERNAL FUNCTIONS AND OBJECTS DECLARATION ]=====================================================================

// BEGIN QUEUE
/*-------------------------------- Queues ---------------------------------*/

/**
 * \brief Create a queue.
 */
static Template_osalErr_e template_osalFreertosQueueCreate(void *const osal,
                                                           const size_t queueItemSize,
                                                           const size_t queueDepth,
                                                           Template_osalQueueHandle_t *const queueHandle);

/**
 * \brief Delete a queue.
 */
static Template_osalErr_e template_osalFreertosQueueDelete(void *const osal,
                                                           const Template_osalQueueHandle_t queueHandle);

/**
 * \brief Enqueue an item.
 */
static Template_osalErr_e template_osalFreertosQueueItemPut(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            const void *const queueItemPtr);

/**
 * \brief   Post an item to a registered queue with the requested timeout.
 * \param   osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   queueHandle   Registered queue handle.
 * \param   queueItemPtr  Pointer to the item to enqueue.
 * \param   timeoutMs     Maximum wait time in milliseconds.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemPost(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             const void *const queueItemPtr,
                                                             const Template_osalTimeMs_t timeoutMs);

/**
 * \brief   Retrieve an already available item from a registered queue without waiting.
 * \param   osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   queueHandle   Registered queue handle.
 * \param   queueItemPtr  Destination buffer receiving the queue item.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemGet(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            void *const queueItemPtr);

/**
 * \brief   Wait indefinitely for an item from a registered queue.
 * \param   osal          Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   queueHandle   Registered queue handle.
 * \param   queueItemPtr  Destination buffer receiving the queue item.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemWait(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             void *const queueItemPtr);

/**
 * \brief Dequeue an item with timeout.
 */
static Template_osalErr_e template_osalFreertosQueueItemPend(void *const osal,
                                                             const Template_osalQueueHandle_t queueHandle,
                                                             void *const queueItemPtr,
                                                             const Template_osalTimeMs_t timeoutMs);

/**
 * \brief Reset a queue.
 */
static Template_osalErr_e template_osalFreertosQueueReset(void *const osal,
                                                          const Template_osalQueueHandle_t queueHandle);

// END QUEUE

// BEGIN STREAM_BUFFER
/*----------------------------- Stream buffers -----------------------------*/

/**
 * \brief   Create a FreeRTOS stream buffer and register it in the OSAL instance.
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   bufferSizeBytes      Stream buffer capacity in bytes.
 * \param   triggerLevelBytes    Receive trigger level in bytes.
 * \param   streamBufferHandle   Output pointer receiving the created handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferCreate(void *const osal,
                                                                  const size_t bufferSizeBytes,
                                                                  const size_t triggerLevelBytes,
                                                                  Template_osalStreamBufferHandle_t *const streamBufferHandle);

/**
 * \brief   Delete a registered FreeRTOS stream buffer.
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferDelete(void *const osal,
                                                                  const Template_osalStreamBufferHandle_t streamBufferHandle);

/**
 * \brief   Send bytes to a registered stream buffer without waiting for capacity.
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 * \param   data                 Pointer to source bytes.
 * \param   dataLengthBytes      Number of bytes requested for transfer.
 * \param   bytesSent            Output pointer receiving the number of bytes written.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferSend(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                const void *const data,
                                                                const size_t dataLengthBytes,
                                                                size_t *const bytesSent);

/**
 * \brief   Receive bytes from a registered stream buffer using the requested timeout.
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 * \param   data                 Destination byte buffer.
 * \param   dataLengthBytes      Maximum number of bytes to receive.
 * \param   timeoutMs            Maximum wait time in milliseconds.
 * \param   bytesReceived        Output pointer receiving the number of bytes read.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferReceive(void *const osal,
                                                                   const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                   void *const data,
                                                                   const size_t dataLengthBytes,
                                                                   const Template_osalTimeMs_t timeoutMs,
                                                                   size_t *const bytesReceived);

/**
 * \brief   Reset a registered FreeRTOS stream buffer to the empty state.
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferReset(void *const osal,
                                                                 const Template_osalStreamBufferHandle_t streamBufferHandle);

// END STREAM_BUFFER

// BEGIN LOCK
/*-------------------------------- Locks ----------------------------------*/

/**
 * \brief Create a lock object.
 */
static Template_osalErr_e template_osalFreertosLockObjCreate(void *const osal,
                                                             Template_osalLockObjHandle_t *const lockObjHandle);

/**
 * \brief Delete a lock object.
 */
static Template_osalErr_e template_osalFreertosLockObjDelete(void *const osal,
                                                             const Template_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Acquire a lock.
 */
static Template_osalErr_e template_osalFreertosLock(void *const osal,
                                                    const Template_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Release a previously acquired lock.
 */
static Template_osalErr_e template_osalFreertosUnlock(void *const osal,
                                                      const Template_osalLockObjHandle_t lockObjHandle);

// END LOCK

// BEGIN SEMAPHORE
/*--------------------------- Counting semaphores --------------------------*/

/**
 * \brief   Create a FreeRTOS counting semaphore and register it in the OSAL instance.
 * \param   osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   maxCount         Maximum semaphore count.
 * \param   initialCount     Initial semaphore count.
 * \param   semaphoreHandle  Output pointer receiving the created handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreCreate(void *const osal,
                                                               const Template_osalSemaphoreCount_t maxCount,
                                                               const Template_osalSemaphoreCount_t initialCount,
                                                               Template_osalSemaphoreHandle_t *const semaphoreHandle);

/**
 * \brief   Delete a registered FreeRTOS counting semaphore.
 * \param   osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle  Registered counting semaphore handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreDelete(void *const osal,
                                                               const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief   Acquire a registered counting semaphore without waiting.
 * \param   osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle  Registered counting semaphore handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreAcquire(void *const osal,
                                                                const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief   Acquire a registered counting semaphore using the requested timeout.
 * \param   osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle  Registered counting semaphore handle.
 * \param   timeoutMs        Maximum wait time in milliseconds.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreAcquireWait(void *const osal,
                                                                    const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                                    const Template_osalTimeMs_t timeoutMs);

/**
 * \brief   Release one count to a registered counting semaphore.
 * \param   osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle  Registered counting semaphore handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreRelease(void *const osal,
                                                                const Template_osalSemaphoreHandle_t semaphoreHandle);

/**
 * \brief   Read the current count of a registered counting semaphore.
 * \param   osal             Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle  Registered counting semaphore handle.
 * \param   semaphoreCount   Output pointer receiving the current count.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreCountGet(void *const osal,
                                                                 const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                                 Template_osalSemaphoreCount_t *const semaphoreCount);

// END SEMAPHORE

// BEGIN THREAD
/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a thread.
 */
static Template_osalErr_e template_osalFreertosThreadCreate(void *const osal,
                                                            Template_osalThreadHandle_t *const threadHandle,
                                                            Template_osalThreadCfg_s threadCfg);

/**
 * \brief Delete a thread.
 */
static Template_osalErr_e template_osalFreertosThreadDelete(void *const osal,
                                                            const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Suspend a thread.
 */
static Template_osalErr_e template_osalFreertosThreadSuspend(void *const osal,
                                                             const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Resume a suspended thread.
 */
static Template_osalErr_e template_osalFreertosThreadResume(void *const osal,
                                                            const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Delay the current thread.
 */
static Template_osalErr_e template_osalFreertosThreadDelay(void *const osal,
                                                           const Template_osalTimeMs_t delayMs);

/**
 * \brief Terminate the calling thread.
 */
static void template_osalFreertosThreadExit(void *const osal);

/**
 * \brief Validate thread parameters.
 */
static bool template_osalFreertosThreadParamCheck(const Template_osalThreadCfg_s *const threadCfg);

// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ------------------------*/

/**
 * \brief   Enter a FreeRTOS task-context critical section.
 * \param   osal  Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosCriticalSectionEnter(void *const osal);

/**
 * \brief   Exit a previously entered FreeRTOS task-context critical section.
 * \param   osal  Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosCriticalSectionExit(void *const osal);

// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*------------------------------- Software timers -------------------------*/

/**
 * \brief   Create a FreeRTOS software timer and register it in the OSAL instance.
 * \param   osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle  Output pointer receiving the created timer handle.
 * \param   timerCfg     Software timer creation configuration.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerCreate(void *const osal,
                                                                   Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                   Template_osalSoftwareTimerCfg_s timerCfg);

/**
 * \brief   Delete a registered FreeRTOS software timer.
 * \param   osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle  Registered software timer handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerDelete(void *const osal,
                                                                   const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief   Start a registered FreeRTOS software timer.
 * \param   osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle  Registered software timer handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStart(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief   Stop a registered FreeRTOS software timer.
 * \param   osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle  Registered software timer handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStop(void *const osal,
                                                                 const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief   Reset and restart the period of a registered FreeRTOS software timer.
 * \param   osal         Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle  Registered software timer handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerReset(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle);

/**
 * \brief Dispatch a native FreeRTOS timer callback to the component callback.
 * \param timerHandle Native FreeRTOS timer handle passed by the timer service.
 */
static void template_osalFreertosSoftwareTimerCallback(TimerHandle_t timerHandle);

// END SOFTWARE_TIMER

/*--------------------------------- Time ----------------------------------*/
// BEGIN TIME
static Template_osalErr_e template_osalFreertosTimeMsGet(void *const osal,
                                                         Template_osalTimeMs_t *const osTimeMs);
// END TIME

/**
 * \brief Convert milliseconds to FreeRTOS ticks for timeout-aware OSAL operations.
 * \param timeMs Timeout value in milliseconds.
 * \return TickType_t FreeRTOS timeout value in ticks.
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
                                                       void *const ptr);

// END MEMORY

/*------------------------------- Predicate -------------------------------*/

/**
 * \brief Validate the FreeRTOS OSAL instance.
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
 * \brief FreeRTOS priority levels lookup table.
 */
static const UBaseType_t template_osalFreertosThreadPriority
[TEMPLATE_OSAL_THREAD_PRIORITY_THE_LAST_ONE] =
{
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_LOW,
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_MIDDLE,
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_HIGH,
    TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_ULTRA
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
    .streamBufferCreate  = template_osalFreertosStreamBufferCreate,
    .streamBufferDelete  = template_osalFreertosStreamBufferDelete,
    .streamBufferSend    = template_osalFreertosStreamBufferSend,
    .streamBufferReceive = template_osalFreertosStreamBufferReceive,
    .streamBufferReset   = template_osalFreertosStreamBufferReset,

// END STREAM_BUFFER

// BEGIN LOCK
    /*-------------------------------- Locks ----------------------------------*/

    .lockObjCreate = template_osalFreertosLockObjCreate,
    .lockObjDelete = template_osalFreertosLockObjDelete,
    .lock          = template_osalFreertosLock,
    .unlock        = template_osalFreertosUnlock,

// END LOCK

// BEGIN SEMAPHORE
    /*--------------------------- Counting semaphores --------------------------*/
    .semaphoreCreate      = template_osalFreertosSemaphoreCreate,
    .semaphoreDelete      = template_osalFreertosSemaphoreDelete,
    .semaphoreAcquire     = template_osalFreertosSemaphoreAcquire,
    .semaphoreAcquireWait = template_osalFreertosSemaphoreAcquireWait,
    .semaphoreRelease     = template_osalFreertosSemaphoreRelease,
    .semaphoreCountGet    = template_osalFreertosSemaphoreCountGet,

// END SEMAPHORE

// BEGIN THREAD
    /*-------------------------------- Threads --------------------------------*/

    .threadCreate  = template_osalFreertosThreadCreate,
    .threadDelete  = template_osalFreertosThreadDelete,
    .threadSuspend = template_osalFreertosThreadSuspend,
    .threadResume  = template_osalFreertosThreadResume,
    .threadDelay   = template_osalFreertosThreadDelay,
    .threadExit    = template_osalFreertosThreadExit,

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
 * \brief   Initialize the Template FreeRTOS OSAL instance.
 *
 * \details
 * Initializes the generic OSAL base object, creates the internal resource
 * mutex and binds the FreeRTOS backend vtable.
 *
 * \param   osalFreertos Pointer to the FreeRTOS-specific OSAL instance.
 * \param   name Optional instance name. May be NULL.
 * \param   parent Optional parent object pointer. May be NULL.
 * \param   param Optional FreeRTOS parameter structure. May be NULL.
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
    osalFreertos->param.handle  = NULL;

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

    /* Store optional FreeRTOS parameters */
    if (param != NULL)
    {
        osalFreertos->param = *param;
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
 * \brief   Deinitialize the Template FreeRTOS OSAL instance.
 *
 * \details
 * Releases all registered resources on a best-effort basis, deletes the
 * internal resource mutex and deinitializes the generic OSAL base object.
 *
 * \param   osalFreertos Pointer to the FreeRTOS-specific OSAL instance.
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

// BEGIN LOCK
    /* Delete registered lock objects */
    for (size_t i = 0u; i < TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.lockObjHandle[i] != NULL)
        {
            (void)template_osalFreertosLockObjDelete(osalFreertos,
                                                        osalFreertos->base.lockObjHandle[i]);
        }
    }

// END LOCK

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

// BEGIN MEMORY
    /* Free registered memory blocks */
    for (size_t i = 0u; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.memObjHandle[i] != NULL)
        {
            (void)template_osalFreertosMemFree(osalFreertos,
                                                  osalFreertos->base.memObjHandle[i]);
        }
    }

// END MEMORY

    /* Clear the FreeRTOS-specific state */
    osalFreertos->validFlag   = false;
    osalFreertos->base.vtable = NULL;
    vSemaphoreDelete(osalFreertos->resourceMutex);
    osalFreertos->resourceMutex = NULL;
    osalFreertos->param.handle  = NULL;

    /* Deinitialize the generic OSAL base */
    osalStatus = template_osalDeinit(&osalFreertos->base);

    /* Trace the deinitialization result */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosDeinit -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: FreeRTOS OSAL was deinitialized
}

//============================================================================[ PRIVATE FUNCTIONS ]==================================================================================

// BEGIN QUEUE
/*-------------------------------- Queues ---------------------------------*/

/**
 * \brief   Create a FreeRTOS queue.
 *
 * \details Reserves a free registry slot through \c ptable, creates a queue
 *          and registers its handle under the resource mutex.
 *
 * \param   osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   queueItemSize Size of one queue item in bytes.
 * \param   queueDepth Maximum number of queue items.
 * \param   queueHandle Output pointer receiving the queue handle.
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
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueCreate(%p, %zu, %zu, %p)",
                                      osal, queueItemSize, queueDepth, (void *)queueHandle);

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
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle Queue handle to delete.
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
 * \brief Put an item into a FreeRTOS queue without waiting for capacity.
 */
static Template_osalErr_e template_osalFreertosQueueItemPut(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            const void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t sendStatus = pdFALSE;

    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut(%p, %p, %p)",
                                 osal, (void *)queueHandle, queueItemPtr);

    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);

    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->queueHandleFind != NULL);

    const size_t queueId = port->base.ptable->queueHandleFind(port, queueHandle);
    if ((queueId == 0u) ||
        (queueId > TEMPLATE_OSAL_QUEUE_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut -> %d", (int)osalStatus);
        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        sendStatus = xQueueSendFromISR((QueueHandle_t)queueHandle,
                                       queueItemPtr,
                                       &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        sendStatus = xQueueSend((QueueHandle_t)queueHandle, queueItemPtr, 0u);
    }

    if (sendStatus != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_QUEUE_IS_FULL_ERR;
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: queue is full
    }

    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosQueueItemPut -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: queue item was enqueued
}


/**
 * \brief   Post an item to a registered FreeRTOS queue with a finite or infinite timeout.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   queueHandle          Registered queue handle.
 * \param   queueItemPtr         Pointer to the item to enqueue.
 * \param   timeoutMs            Maximum wait time in milliseconds.
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
 * \brief   Retrieve an already available item from a registered FreeRTOS queue without waiting.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   queueHandle          Registered queue handle.
 * \param   queueItemPtr         Destination buffer receiving the queue item.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosQueueItemGet(void *const osal,
                                                            const Template_osalQueueHandle_t queueHandle,
                                                            void *const queueItemPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t receiveStatus = pdFALSE;

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
 * \brief   Wait indefinitely for an item from a registered FreeRTOS queue.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   queueHandle          Registered queue handle.
 * \param   queueItemPtr         Destination buffer receiving the queue item.
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
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle Queue handle to read from.
 * \param queueItemPtr Destination buffer receiving the queue item.
 * \param timeoutMs Maximum wait time in milliseconds.
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
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param queueHandle Queue handle to reset.
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
 * \brief   Create a FreeRTOS stream buffer and register it in the OSAL instance.
 *
 * \details The operation follows the component-scoped OSAL ownership model and
 *          validates/registers the native object through the OSAL registry.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   bufferSizeBytes      Stream buffer capacity in bytes.
 * \param   triggerLevelBytes    Receive trigger level in bytes.
 * \param   streamBufferHandle   Output pointer receiving the created handle.
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
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate(%p, %zu, %zu, %p)",
                                 osal, bufferSizeBytes, triggerLevelBytes, (void *)streamBufferHandle);

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

    *streamBufferHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    const size_t streamBufferId = port->base.ptable->streamBufferFreeSlotFind(port);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free stream-buffer registry slot
    }

    const StreamBufferHandle_t nativeHandle = xStreamBufferCreate(bufferSizeBytes, triggerLevelBytes);
    if (nativeHandle == NULL)
    {
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream-buffer allocation failed
    }

    port->base.streamBufferObjHandle[streamBufferId - 1u] =
        (Template_osalStreamBufferHandle_t)nativeHandle;
    *streamBufferHandle = (Template_osalStreamBufferHandle_t)nativeHandle;

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
 * \brief   Delete a registered FreeRTOS stream buffer.
 *
 * \details The handle must belong to this OSAL instance. Registry bookkeeping
 *          is updated together with the native FreeRTOS resource lifecycle.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
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

    /* Find and validate the resource handle in the component registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        (void)template_osalFreertosResourceUnlock(port);
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    vStreamBufferDelete((StreamBufferHandle_t)streamBufferHandle);
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
 * \brief   Send bytes to a registered FreeRTOS stream buffer without waiting for capacity.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 * \param   data                 Pointer to source bytes.
 * \param   dataLengthBytes      Number of bytes requested for transfer.
 * \param   bytesSent            Output pointer receiving the number of bytes written.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferSend(void *const osal,
                                                                const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                const void *const data,
                                                                const size_t dataLengthBytes,
                                                                size_t *const bytesSent)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferSend(%p, %p, %p, %zu, %p)",
                                 osal,
                                 (void *)streamBufferHandle,
                                 data,
                                 dataLengthBytes,
                                 (void *)bytesSent);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(data != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bytesSent != NULL);

    *bytesSent = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Find and validate the resource handle in the component registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferSend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        *bytesSent = xStreamBufferSendFromISR((StreamBufferHandle_t)streamBufferHandle,
                                              data,
                                              dataLengthBytes,
                                              &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        *bytesSent = xStreamBufferSend((StreamBufferHandle_t)streamBufferHandle,
                                       data,
                                       dataLengthBytes,
                                       0u);
    }

    if (*bytesSent == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_FULL_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferSend -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: stream buffer accepted no data
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferSend -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was sent
}


/**
 * \brief   Receive bytes from a registered FreeRTOS stream buffer with timeout.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 * \param   data                 Destination byte buffer.
 * \param   dataLengthBytes      Maximum number of bytes to receive.
 * \param   timeoutMs            Maximum wait time in milliseconds.
 * \param   bytesReceived        Output pointer receiving the number of bytes read.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosStreamBufferReceive(void *const osal,
                                                                   const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                                   void *const data,
                                                                   const size_t dataLengthBytes,
                                                                   const Template_osalTimeMs_t timeoutMs,
                                                                   size_t *const bytesReceived)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReceive(%p, %p, %p, %zu, %u, %p)",
                                 osal,
                                 (void *)streamBufferHandle,
                                 data,
                                 dataLengthBytes,
                                 (unsigned int)timeoutMs,
                                 (void *)bytesReceived);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(streamBufferHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(data != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(dataLengthBytes != 0u);
    TEMPLATE_OSAL_FREERTOS_ASSERT(bytesReceived != NULL);

    *bytesReceived = 0u;

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->streamBufferHandleFind != NULL);

    /* Find and validate the resource handle in the component registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReceive -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        if (timeoutMs != 0u)
        {
            TEMPLATE_OSAL_FREERTOS_ASSERT(0);
            osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

            /* Trace returned value */
            TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReceive -> %d", (int)osalStatus);

            return osalStatus;  // Exit: Error: ISR context is not supported
        }

        BaseType_t higherPriorityTaskWoken = pdFALSE;
        *bytesReceived = xStreamBufferReceiveFromISR((StreamBufferHandle_t)streamBufferHandle,
                                                      data,
                                                      dataLengthBytes,
                                                      &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        *bytesReceived = xStreamBufferReceive((StreamBufferHandle_t)streamBufferHandle,
                                              data,
                                              dataLengthBytes,
                                              template_osalFreertosTimeMsToTicksConvert(timeoutMs));
    }

    if (*bytesReceived == 0u)
    {
        osalStatus = TEMPLATE_OSAL_STREAM_BUFFER_IS_EMPTY_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReceive -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no stream-buffer data was received
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReceive -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: stream-buffer data was received
}


/**
 * \brief   Reset a registered FreeRTOS stream buffer to the empty state.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   streamBufferHandle   Registered stream buffer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
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
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find and validate the resource handle in the component registry */
    const size_t streamBufferId = port->base.ptable->streamBufferHandleFind(port, streamBufferHandle);
    if ((streamBufferId == 0u) ||
        (streamBufferId > TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosStreamBufferReset -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

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

// BEGIN LOCK
/*-------------------------------- Locks ----------------------------------*/

/**
 * \brief Create a recursive FreeRTOS mutex.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param lockObjHandle Output pointer receiving the lock handle.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosLockObjCreate(void *const osal,
                                                             Template_osalLockObjHandle_t *const lockObjHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjCreate(%p, %p)",
                                      osal, (void *)lockObjHandle);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->lockObjFreeSlotFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Clear the output value */
    *lockObjHandle = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find a free lock-object registry slot */
    const size_t lockId = port->base.ptable->lockObjFreeSlotFind(port);
    if ((lockId == 0u) ||
        (lockId > TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_LOCK_OBJ_CREATE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: no free lock slot
    }

    /* Create the native recursive mutex */
    const SemaphoreHandle_t nativeLock = xSemaphoreCreateRecursiveMutex();
    if (nativeLock == NULL)
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_LOCK_OBJ_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: lock allocation failed
    }

    /* Register the resource handle */
    port->base.lockObjHandle[lockId - 1u] = (Template_osalLockObjHandle_t)nativeLock;
    *lockObjHandle                        = (Template_osalLockObjHandle_t)nativeLock;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjCreate -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: lock object was created and registered
}


/**
 * \brief Delete a recursive FreeRTOS mutex.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param lockObjHandle Lock handle to delete.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosLockObjDelete(void *const osal,
                                                             const Template_osalLockObjHandle_t lockObjHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjDelete(%p, %p)",
                                      osal, (void *)lockObjHandle);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->lockObjHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Acquire the resource mutex */
    osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex acquisition failed
    }

    /* Find the lock-object handle in the registry */
    const size_t lockId = port->base.ptable->lockObjHandleFind(port, lockObjHandle);
    if ((lockId == 0u) ||
        (lockId > TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);

        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: lock handle is not registered
    }

    /* Delete the native mutex */
    vSemaphoreDelete((SemaphoreHandle_t)lockObjHandle);

    /* Clear the registry slot */
    port->base.lockObjHandle[lockId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release the resource mutex */
    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjDelete -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: resource mutex release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLockObjDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: lock object was deleted and unregistered
}


/**
 * \brief Acquire a recursive FreeRTOS mutex.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param lockObjHandle Lock handle to acquire.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosLock(void *const osal,
                                                    const Template_osalLockObjHandle_t lockObjHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLock(%p, %p)", osal, (void *)lockObjHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->lockObjHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the lock-object handle in the registry */
    const size_t lockId = port->base.ptable->lockObjHandleFind(port, lockObjHandle);

    if ((lockId == 0u) ||
        (lockId > TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: lock handle is not registered
    }

    /* Acquire the native recursive mutex */
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)lockObjHandle, portMAX_DELAY) != pdTRUE)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native recursive mutex operation failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosLock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: lock was acquired
}


/**
 * \brief Release a recursive FreeRTOS mutex.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param lockObjHandle Lock handle to release.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosUnlock(void *const osal,
                                                      const Template_osalLockObjHandle_t lockObjHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosUnlock(%p, %p)", osal, (void *)lockObjHandle);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->lockObjHandleFind != NULL);

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_CALL_FROM_ISR_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: ISR context is not supported
    }

    /* Find the lock-object handle in the registry */
    const size_t lockId = port->base.ptable->lockObjHandleFind(port, lockObjHandle);

    if ((lockId == 0u) ||
        (lockId > TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM))
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: lock handle is not registered
    }

    /* Release the native recursive mutex */
    if (xSemaphoreGiveRecursive((SemaphoreHandle_t)lockObjHandle) != pdTRUE)
    {
        /* Report an invariant violation */
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        osalStatus = TEMPLATE_OSAL_PORT_SPECIFIC_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosUnlock -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: native recursive mutex operation failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosUnlock -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: lock was released
}


// END LOCK

// BEGIN SEMAPHORE
/*--------------------------- Counting semaphores --------------------------*/

/**
 * \brief   Create a FreeRTOS counting semaphore and register it in the OSAL instance.
 *
 * \details The operation follows the component-scoped OSAL ownership model and
 *          validates/registers the native object through the OSAL registry.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   maxCount             Maximum semaphore count.
 * \param   initialCount         Initial semaphore count.
 * \param   semaphoreHandle      Output pointer receiving the created handle.
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
 * \brief   Delete a registered FreeRTOS counting semaphore.
 *
 * \details The handle must belong to this OSAL instance. Registry bookkeeping
 *          is updated together with the native FreeRTOS resource lifecycle.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle      Registered counting semaphore handle.
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
 * \brief   Acquire a registered counting semaphore without waiting.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle      Registered counting semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreAcquire(void *const osal,
                                                                const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t acquireStatus = pdFALSE;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquire(%p, %p)",
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
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquire -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        acquireStatus = xSemaphoreTakeFromISR((SemaphoreHandle_t)semaphoreHandle,
                                              &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        acquireStatus = xSemaphoreTake((SemaphoreHandle_t)semaphoreHandle, 0u);
    }

    if (acquireStatus != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_ACQUIRE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquire -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore token is unavailable
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquire -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore token was acquired
}


/**
 * \brief   Acquire a registered counting semaphore with timeout.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle      Registered counting semaphore handle.
 * \param   timeoutMs            Maximum wait time in milliseconds.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreAcquireWait(void *const osal,
                                                                    const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                                    const Template_osalTimeMs_t timeoutMs)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquireWait(%p, %p, %u)",
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
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquireWait -> %d", (int)osalStatus);

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
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquireWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    if (xSemaphoreTake((SemaphoreHandle_t)semaphoreHandle,
                       template_osalFreertosTimeMsToTicksConvert(timeoutMs)) != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_ACQUIRE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquireWait -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore acquisition failed or timed out
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreAcquireWait -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore token was acquired
}


/**
 * \brief   Release one count to a registered FreeRTOS counting semaphore.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle      Registered counting semaphore handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSemaphoreRelease(void *const osal,
                                                                const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t releaseStatus = pdFALSE;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreRelease(%p, %p)",
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
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreRelease -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid argument or unregistered handle
    }

    /* Validate execution context */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        releaseStatus = xSemaphoreGiveFromISR((SemaphoreHandle_t)semaphoreHandle,
                                              &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
    else
    {
        releaseStatus = xSemaphoreGive((SemaphoreHandle_t)semaphoreHandle);
    }

    if (releaseStatus != pdTRUE)
    {
        osalStatus = TEMPLATE_OSAL_SEMAPHORE_RELEASE_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreRelease -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: semaphore release failed
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSemaphoreRelease -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: semaphore token was released
}


/**
 * \brief   Read the current count of a registered FreeRTOS counting semaphore.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   semaphoreHandle      Registered counting semaphore handle.
 * \param   semaphoreCount       Output pointer receiving the current count.
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

// BEGIN THREAD
/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a FreeRTOS task.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle Output pointer receiving the thread handle.
 * \param threadCfg Thread configuration.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosThreadCreate(void *const osal,
                                                            Template_osalThreadHandle_t *const threadHandle,
                                                            Template_osalThreadCfg_s threadCfg)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate(%p, %p, {%p, %s, %zu, %p, %d})",
                                      osal,
                                      (void *)threadHandle,
                                      (void *)(uintptr_t)threadCfg.worker,
                                      (threadCfg.name != NULL) ? threadCfg.name : "(null)",
                                      threadCfg.stackSize,
                                      threadCfg.args,
                                      (int)threadCfg.prio);

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

    if (!template_osalFreertosThreadParamCheck(&threadCfg))
    {
        osalStatus = TEMPLATE_OSAL_INVALID_ARGS_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: invalid thread configuration
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

    const size_t stackWordSize              = sizeof(StackType_t);
    const size_t stackWordsRaw              = (threadCfg.stackSize + stackWordSize - 1u) / stackWordSize;
    const configSTACK_DEPTH_TYPE stackWords = (configSTACK_DEPTH_TYPE)stackWordsRaw;
    const UBaseType_t priority              = template_osalFreertosThreadPriority[threadCfg.prio];

    TaskHandle_t nativeThread = NULL;
    /* Create the native FreeRTOS task */
    const BaseType_t rc = xTaskCreate((TaskFunction_t)threadCfg.worker,
                                      threadCfg.name,
                                      stackWords,
                                      threadCfg.args,
                                      priority,
                                      &nativeThread);
    if ((rc != pdPASS) ||
        (nativeThread == NULL))
    {
        /* Release the resource mutex */
        (void)template_osalFreertosResourceUnlock(port);
        osalStatus = TEMPLATE_OSAL_THREAD_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: task creation failed
    }

    const size_t threadIdx = threadId - 1u;
    port->base.threadObjHandle[threadIdx].cfg = threadCfg;

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
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle Thread handle to delete.
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

    const bool deleteSelf = ((TaskHandle_t)threadHandle == xTaskGetCurrentTaskHandle());
    port->base.ptable->threadSlotClear(port, threadId - 1u);

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

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadDelete -> %d", (int)osalStatus);

    return osalStatus;  // Exit: Success: thread was deleted
}


/**
 * \brief Suspend a FreeRTOS task.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle Thread handle to suspend.
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
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param threadHandle Thread handle to resume.
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
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param delayMs Delay duration in milliseconds.
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
 * \brief Terminate the calling FreeRTOS task.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \note This function does not return on a valid call.
 */
static void template_osalFreertosThreadExit(void *const osal)
{
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit(%p)", osal);

    if (osal == NULL)
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> invalid OSAL");
        return;  // Exit: Error: invalid OSAL instance
    }

    if (xPortIsInsideInterrupt())
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> ISR context is not supported");
        return;  // Exit: Error: ISR context is not supported
    }

    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;
    if (!template_osalFreertosIsValid(port) ||
        (port->base.ptable == NULL) ||
        (port->base.ptable->threadHandleFind == NULL))
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> invalid backend state");
        return;  // Exit: Error: backend invariant is not satisfied
    }

    const TaskHandle_t currentThread = xTaskGetCurrentTaskHandle();
    if (currentThread == NULL)
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> current task is unavailable");
        return;  // Exit: Error: current task handle is unavailable
    }

    Template_osalErr_e osalStatus = template_osalFreertosResourceLock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> resource lock failed: %d", (int)osalStatus);
        return;  // Exit: Error: resource mutex acquisition failed
    }

    const size_t threadId = port->base.ptable->threadHandleFind(
        port, (Template_osalThreadHandle_t)currentThread);
    if ((threadId == 0u) ||
        (threadId > TEMPLATE_OSAL_THREAD_SLOTS_NUM))
    {
        (void)template_osalFreertosResourceUnlock(port);
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> current task is not registered");
        return;  // Exit: Error: current task is not registered
    }

    port->base.ptable->threadSlotClear(port, threadId - 1u);

    osalStatus = template_osalFreertosResourceUnlock(port);
    if (osalStatus != TEMPLATE_OSAL_NO_ERR)
    {
        TEMPLATE_OSAL_FREERTOS_ASSERT(0);
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> resource unlock failed: %d", (int)osalStatus);
        return;  // Exit: Error: resource mutex release failed
    }

    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadExit -> no return");
    vTaskDelete(NULL);

    TEMPLATE_OSAL_FREERTOS_ASSERT(0);
    while (1)
    {
        (void)0;
    }
}


/**
 * \brief Validate a FreeRTOS task configuration.
 * \param threadCfg Pointer to the task configuration.
 * \return true if the configuration is valid, otherwise false.
 */
static bool template_osalFreertosThreadParamCheck(const Template_osalThreadCfg_s *const threadCfg)
{
    bool isValid = true;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadParamCheck(%p)",
                                      (const void *)threadCfg);
    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(threadCfg != NULL);

    if (threadCfg->worker == NULL)
    {
        isValid = false;
    }

    if (threadCfg->prio >= TEMPLATE_OSAL_THREAD_PRIORITY_THE_LAST_ONE)
    {
        isValid = false;
    }

    const size_t stackWordSize = sizeof(StackType_t);
    const size_t stackWords    = (threadCfg->stackSize + stackWordSize - 1u) / stackWordSize;

    if ((stackWords < (size_t)configMINIMAL_STACK_SIZE) ||
        (stackWords > (size_t)((configSTACK_DEPTH_TYPE) - 1)))
    {
        isValid = false;
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosThreadParamCheck -> %d", (int)isValid);

    return isValid;  // Exit: Success: validation result returned
}


// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section ------------------------*/

/**
 * \brief   Enter a FreeRTOS critical section from task context.
 *
 * \details This primitive does not allocate a registry-backed object; it maps
 *          the unified OSAL critical-section contract to the FreeRTOS backend.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
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
    TEMPLATE_OSAL_FREERTOS_ASSERT(
        template_osalFreertosIsValid((const Template_osalFreertos_s *)osal));

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
 * \brief   Exit a previously entered FreeRTOS critical section from task context.
 *
 * \details This primitive does not allocate a registry-backed object; it maps
 *          the unified OSAL critical-section contract to the FreeRTOS backend.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
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
    TEMPLATE_OSAL_FREERTOS_ASSERT(
        template_osalFreertosIsValid((const Template_osalFreertos_s *)osal));

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
 * \brief   Create a FreeRTOS software timer and register it in the OSAL instance.
 *
 * \details The operation follows the component-scoped OSAL ownership model and
 *          validates/registers the native object through the OSAL registry.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle          Output pointer receiving the created timer handle.
 * \param   timerCfg             Software timer creation configuration.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerCreate(void *const osal,
                                                                   Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                                   Template_osalSoftwareTimerCfg_s timerCfg)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate(%p, %p, {%s, %p, %p, %d, %u})",
                                 osal,
                                 (void *)timerHandle,
                                 (timerCfg.name != NULL) ? timerCfg.name : "(null)",
                                 timerCfg.timerParam,
                                 (void *)(uintptr_t)timerCfg.timerExpiredCb,
                                 (int)timerCfg.autoReload,
                                 (unsigned int)timerCfg.periodMs);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerHandle != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerCfg.timerExpiredCb != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerCfg.periodMs != 0u);

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
    timerObj->cfg = timerCfg;

    const TimerHandle_t nativeHandle =
        xTimerCreate(timerCfg.name,
                     template_osalFreertosTimeMsToTicksConvert(timerCfg.periodMs),
                     timerCfg.autoReload ? pdTRUE : pdFALSE,
                     timerObj,
                     template_osalFreertosSoftwareTimerCallback);
    if (nativeHandle == NULL)
    {
        timerObj->cfg.name           = NULL;
        timerObj->cfg.timerParam     = NULL;
        timerObj->cfg.timerExpiredCb = NULL;
        timerObj->cfg.autoReload     = false;
        timerObj->cfg.periodMs       = 0u;
        (void)template_osalFreertosResourceUnlock(port);

        osalStatus = TEMPLATE_OSAL_SOFTWARE_TIMER_MEM_ALLOCATION_ERR;

        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCreate -> %d", (int)osalStatus);

        return osalStatus;  // Exit: Error: software-timer allocation failed
    }

    timerObj->handle = (Template_osalSoftwareTimerHandle_t)nativeHandle;
    *timerHandle = (Template_osalSoftwareTimerHandle_t)nativeHandle;

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
 * \brief   Delete a registered FreeRTOS software timer.
 *
 * \details The handle must belong to this OSAL instance. Registry bookkeeping
 *          is updated together with the native FreeRTOS resource lifecycle.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle          Registered software timer handle.
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
    timerObj->handle             = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;
    timerObj->cfg.name           = NULL;
    timerObj->cfg.timerParam     = NULL;
    timerObj->cfg.timerExpiredCb = NULL;
    timerObj->cfg.autoReload     = false;
    timerObj->cfg.periodMs       = 0u;

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
 * \brief   Start a registered FreeRTOS software timer.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle          Registered software timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStart(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t timerStatus = pdFAIL;

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
 * \brief   Stop a registered FreeRTOS software timer.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle          Registered software timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerStop(void *const osal,
                                                                 const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t timerStatus = pdFAIL;

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
 * \brief   Reset and restart the period of a registered FreeRTOS software timer.
 *
 * \details The handle is validated against the component OSAL registry before
 *          the native FreeRTOS operation is executed.
 *
 * \param   osal                 Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param   timerHandle          Registered software timer handle.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
static Template_osalErr_e template_osalFreertosSoftwareTimerReset(void *const osal,
                                                                  const Template_osalSoftwareTimerHandle_t timerHandle)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;
    BaseType_t timerStatus = pdFAIL;

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
 * \brief   Dispatch a native FreeRTOS timer callback to the component callback.
 *
 * \details The native timer identifier stores the OSAL timer object used to
 *          recover the component callback and user parameter.
 *
 * \param   timerHandle          Native FreeRTOS timer handle passed by the timer service.
 *
 * \return None.
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
    TEMPLATE_OSAL_FREERTOS_ASSERT(timerObj->cfg.timerExpiredCb != NULL);

    if ((timerObj->handle != (Template_osalSoftwareTimerHandle_t)timerHandle) ||
        (timerObj->cfg.timerExpiredCb == NULL))
    {
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCallback -> invalid timer state");
        return;  // Exit: Error: timer callback state is invalid
    }

    timerObj->cfg.timerExpiredCb(timerObj->cfg.timerParam);

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosSoftwareTimerCallback -> ok");
}


// END SOFTWARE_TIMER

/*--------------------------------- Time ----------------------------------*/

// BEGIN TIME
/**
 * \brief Retrieve the current FreeRTOS system time in milliseconds.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param osTimeMs Output pointer receiving the current time in milliseconds.
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
 * \param timeMs Time interval in milliseconds.
 * \return Converted FreeRTOS tick count.
 */
static inline TickType_t template_osalFreertosTimeMsToTicksConvert(const Template_osalTimeMs_t timeMs)
{
    TickType_t tickCount = 0u;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsToTicksConvert(%u)",
                                      (unsigned int)timeMs);

    if ((timeMs == TEMPLATE_OSAL_INFINITY_TOUT) ||
        (timeMs == TEMPLATE_OSAL_FREERTOS_INFINITY_TIMEOUT))
    {
        tickCount = portMAX_DELAY;
        /* Trace returned value */
        TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsToTicksConvert -> %u",
                                          (unsigned int)tickCount);

        return tickCount;  // Exit: Success: infinite timeout converted
    }

    tickCount = pdMS_TO_TICKS(timeMs);
    if ((timeMs != 0u) &&
        (tickCount == 0u))
    {
        tickCount = 1u;
    }

    /* Trace returned value */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosTimeMsToTicksConvert -> %u",
                                      (unsigned int)tickCount);

    return tickCount;  // Exit: Success: timeout converted
}

// BEGIN MEMORY
/*-------------------------------- Memory ---------------------------------*/

/**
 * \brief Allocate memory from the FreeRTOS heap.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param size Allocation size in bytes.
 * \param memPtr Output pointer receiving the allocated memory address.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMemAlloc(void *const osal,
                                                        const size_t size,
                                                        void **const memPtr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemAlloc(%p, %zu, %p)",
                                      osal, size, (void *)memPtr);
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

        return osalStatus;  // Exit: Error: no free memory slot
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

    /* Register the resource handle */
    port->base.memObjHandle[memoryId - 1u] = (Template_osalMemHandle_t)allocatedPtr;
    *memPtr                                = allocatedPtr;

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
 * \brief Free memory allocated from the FreeRTOS heap.
 * \param osal Opaque pointer to the initialized FreeRTOS OSAL instance.
 * \param ptr Pointer to the memory block to release.
 * \return Template_osalErr_e, zero value means success, otherwise an error has occurred.
 */
static Template_osalErr_e template_osalFreertosMemFree(void *const osal,
                                                       void *const ptr)
{
    Template_osalErr_e osalStatus = TEMPLATE_OSAL_NO_ERR;

    /* Trace input args */
    TEMPLATE_OSAL_FREERTOS_TRACE("template_osalFreertosMemFree(%p, %p)", osal, ptr);

    /* Validate input args */
    TEMPLATE_OSAL_FREERTOS_ASSERT(osal != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(ptr != NULL);

    /* Downcast the generic OSAL instance to the FreeRTOS-specific type */
    Template_osalFreertos_s *const port = (Template_osalFreertos_s *)osal;

    /* Validate backend state */
    TEMPLATE_OSAL_FREERTOS_ASSERT(template_osalFreertosIsValid(port));
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable != NULL);
    TEMPLATE_OSAL_FREERTOS_ASSERT(port->base.ptable->memHandleFind != NULL);

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

    /* Find the memory handle in the registry */
    const size_t memoryId = port->base.ptable->memHandleFind(port, ptr);
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
    port->base.memObjHandle[memoryId - 1u] = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Release memory to the FreeRTOS heap */
    vPortFree(ptr);

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
 * \param osal Opaque pointer to the FreeRTOS OSAL instance.
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
 * \param osalFreertos Pointer to the initialized FreeRTOS OSAL instance.
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
 * \param osalFreertos Pointer to the initialized FreeRTOS OSAL instance.
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
