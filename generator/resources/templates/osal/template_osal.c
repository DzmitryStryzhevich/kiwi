/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

/**
 * \file     template_osal.c
 * \brief    OSAL layer interface implementation for Template.
 * \details  The concrete OSAL port is supplied via the vtable in the RTOS layer.
 */

/*=============================================================================[ INCLUDE ]=============================================================================*/

#include "template_osal.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Include config file if it is defined at compilation time */
#ifdef TEMPLATE_CONFIG_FILE
    #include TEMPLATE_OSAL_STR(TEMPLATE_CONFIG_FILE)
#endif


/*================================================================[ INTERNAL MACRO DEFINITIONS ]======================================================================*/

/**
 * \def   TEMPLATE_OSAL_ASSERT
 * \brief Redirect to the subsystem's main assert if available; otherwise fallback to <assert.h>.
 */
#ifndef TEMPLATE_OSAL_ASSERT
    #if defined(TEMPLATE_ASSERT)
        #define TEMPLATE_OSAL_ASSERT(cond)    TEMPLATE_ASSERT(cond)
    #else
        #include <assert.h>
        #define TEMPLATE_OSAL_ASSERT(cond)    assert(cond)
    #endif
#endif

/**
 * \def   TEMPLATE_OSAL_TRACE
 * \brief Redirect to the subsystem's main trace if available; otherwise no-op.
 */
#ifndef TEMPLATE_OSAL_TRACE
    #if defined(TEMPLATE_TRACE)
        #define TEMPLATE_OSAL_TRACE(...)    TEMPLATE_TRACE(__VA_ARGS__)
    #else
        #define TEMPLATE_OSAL_TRACE(...)    ((void)0)
    #endif
#endif


/*===============================================================[ INTERNAL FUNCTIONS AND OBJECTS DECLARATION ]======================================================*/

/**
 * \brief Reset (clear) OSAL internal object registry
 */
static void template_osalRegReset(Template_osal_s *const osal);

// BEGIN QUEUE

/**
 * \brief Find a free queue slot.
 */
static size_t template_osalRegQueueFreeSlotFind(void *const osalPort);

/**
 * \brief Find queue handle.
 */
static size_t template_osalRegQueueHandleFind(void *const osalPort,
                                              const Template_osalQueueHandle_t queueHandle);
// END QUEUE

// BEGIN STREAM_BUFFER

/**
 * \brief Find a free stream-buffer slot.
 */
static size_t template_osalRegStreamBufferFreeSlotFind(void *const osalPort);

/**
 * \brief Find a stream-buffer handle.
 */
static size_t template_osalRegStreamBufferHandleFind(void *const osalPort,
                                                     const Template_osalStreamBufferHandle_t streamBufferHandle);
// END STREAM_BUFFER

// BEGIN MUTEX

/**
 * \brief Find a free mutex slot.
 */
static size_t template_osalRegMutexFreeSlotFind(void *const osalPort);

/**
 * \brief Find mutex handle.
 */
static size_t template_osalRegMutexHandleFind(void *const osalPort,
                                             const Template_osalMutexHandle_t mutexHandle);
// END MUTEX

// BEGIN SEMAPHORE

/**
 * \brief Find a free counting-semaphore slot.
 */
static size_t template_osalRegSemaphoreFreeSlotFind(void *const osalPort);

/**
 * \brief Find a counting-semaphore handle.
 */
static size_t template_osalRegSemaphoreHandleFind(void *const osalPort,
                                                  const Template_osalSemaphoreHandle_t semaphoreHandle);
// END SEMAPHORE

// BEGIN EVENT_FLAGS

/**
 * \brief Find a free event-flags slot.
 */
static size_t template_osalRegEventFlagsFreeSlotFind(void *const osalPort);

/**
 * \brief Find an event-flags handle.
 */
static size_t template_osalRegEventFlagsHandleFind(void *const osalPort,
                                                   const Template_osalEventFlagsHandle_t eventFlagsHandle);
// END EVENT_FLAGS

// BEGIN THREAD

/**
 * \brief Find a free thread slot.
 */
static size_t template_osalRegThreadFreeSlotFind(void *const osalPort);

/**
 * \brief Find thread handle.
 */
static size_t template_osalRegThreadHandleFind(void *const osalPort,
                                               const Template_osalThreadHandle_t threadHandle);

/**
 * \brief Clear a thread registry slot.
 */
static void template_osalRegThreadSlotClear(void *const osalPort,
                                            const size_t threadIdx);
// END THREAD

// BEGIN SOFTWARE_TIMER

/**
 * \brief Find a free software-timer slot.
 */
static size_t template_osalRegSoftwareTimerFreeSlotFind(void *const osalPort);

/**
 * \brief Find a software-timer handle.
 */
static size_t template_osalRegSoftwareTimerHandleFind(void *const osalPort,
                                                      const Template_osalSoftwareTimerHandle_t timerHandle);
// END SOFTWARE_TIMER

// BEGIN MEMORY

/**
 * \brief Find a free memory slot.
 */
static size_t template_osalRegMemFreeSlotFind(void *const osalPort);

/**
 * \brief Find a registered memory pointer in the internal registry.
 */
static size_t template_osalRegMemPtrFind(void *const osalPort,
                                         const void *const memPtr);
// END MEMORY

/**
 * \brief Protected registry helpers vtable (for backend ports).
 * \details Provides unified helpers for slot search and resource lookup for all registry-backed primitive groups.
 *          IDs are 1-based (index + 1). Zero value indicates "not found"/"no free slot".
 */
static const Template_osalPtable_s template_osalPtable =
{
    // BEGIN QUEUE
    /*------------------------------- Queues --------------------------------*/
    .queueFreeSlotFind = template_osalRegQueueFreeSlotFind,
    .queueHandleFind   = template_osalRegQueueHandleFind,
    // END QUEUE

    // BEGIN STREAM_BUFFER
    /*----------------------------- Stream buffers ----------------------------*/
    .streamBufferFreeSlotFind = template_osalRegStreamBufferFreeSlotFind,
    .streamBufferHandleFind   = template_osalRegStreamBufferHandleFind,
    // END STREAM_BUFFER

    // BEGIN MUTEX
    /*-------------------------------- Mutexes --------------------------------*/
    .mutexFreeSlotFind = template_osalRegMutexFreeSlotFind,
    .mutexHandleFind   = template_osalRegMutexHandleFind,
    // END MUTEX

    // BEGIN SEMAPHORE
    /*--------------------------- Counting semaphores --------------------------*/
    .semaphoreFreeSlotFind = template_osalRegSemaphoreFreeSlotFind,
    .semaphoreHandleFind   = template_osalRegSemaphoreHandleFind,
    // END SEMAPHORE

    // BEGIN EVENT_FLAGS
    /*------------------------------- Event flags -------------------------------*/
    .eventFlagsFreeSlotFind = template_osalRegEventFlagsFreeSlotFind,
    .eventFlagsHandleFind   = template_osalRegEventFlagsHandleFind,
    // END EVENT_FLAGS

    // BEGIN THREAD
    /*------------------------------- Threads -------------------------------*/
    .threadFreeSlotFind = template_osalRegThreadFreeSlotFind,
    .threadHandleFind   = template_osalRegThreadHandleFind,
    .threadSlotClear    = template_osalRegThreadSlotClear,
    // END THREAD

    // BEGIN SOFTWARE_TIMER
    /*----------------------------- Software timers ----------------------------*/
    .softwareTimerFreeSlotFind = template_osalRegSoftwareTimerFreeSlotFind,
    .softwareTimerHandleFind   = template_osalRegSoftwareTimerHandleFind,
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    /*-------------------------------- Memory -------------------------------*/
    .memFreeSlotFind = template_osalRegMemFreeSlotFind,
    .memPtrFind      = template_osalRegMemPtrFind,
    // END MEMORY

    .reserved = 0u
};


/*=======================================================================[ PUBLIC INTERFACE FUNCTIONS ]================================================================*/

/*---------------------------------- Lifecycle --------------------------------------*/

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
                                     void *const parent)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalInit(%p, %s, %p)",
                        (void *)osal, (name ? name : "(null)"), parent);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalInit -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Init instance */
    osal->validFlag = false;
    osal->name      = name;
    osal->parent    = parent;

    /* Reset internals */
    template_osalRegReset(osal);

    /* Reset methods table before use */
    osal->vtable = NULL;

    /* Assign ptable methods table */
    osal->ptable = &template_osalPtable;

    /* Mark as valid */
    osal->validFlag = true;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalInit -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


/**
 * \brief Deinitialize Template OSAL instance.
 *
 * \param osal  OSAL instance pointer.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalDeinit(Template_osal_s *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalDeinit(%p)", (void *)osal);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalDeinit -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalDeinit -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Clear instance */
    osal->validFlag = false;
    osal->name      = NULL;
    osal->parent    = NULL;
    osal->vtable    = NULL;

    /* Reset internals */
    template_osalRegReset(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalDeinit -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


/**
 * \brief Validate OSAL instance (including vtable layer).
 *
 * \param osal  OSAL instance pointer (const).
 *
 * \return true if valid/initialized; false otherwise.
 */
bool template_osalIsValid(const Template_osal_s *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalIsValid(%p)", (const void *)osal);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", 0);

        return false;  // Exit: Error: invalid args
    }

    /* Validate local state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", 0);

        return false;  // Exit: Error: not initialized
    }

    /* Check vtable table presence */
    if (osal->vtable == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", 0);

        return false;  // Exit: Error: vtable is unavailable
    }

    /* Check vtable predicate presence */
    if (osal->vtable->isValid == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", 0);

        return false;  // Exit: Error: backend predicate is unavailable
    }

    /* Delegate to backend */
    const bool ok = osal->vtable->isValid(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", (int)ok);

    return ok;  // Exit: Success: summary validation status returned
}


/*---------------------------------- Metadata -----------------------------------------*/

/**
 * \brief Get pointer to a parent of the given OSAL object.
 *
 * \param osal    OSAL instance pointer.
 * \param parent  Output: pointer to parent (must not be NULL; may be set to NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalParentGet(Template_osal_s *const osal,
                                          void **const parent)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalParentGet(%p, %p)", (void *)osal, (void *)parent);

    /* Validate args */
    if ((osal == NULL) ||
        (parent == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalParentGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalParentGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Get parent */
    *parent = (void *)osal->parent;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalParentGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


/**
 * \brief Set the parent object for the given OSAL instance.
 *
 * \param osal    OSAL instance pointer.
 * \param parent  Parent pointer to be set (may be NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalParentSet(Template_osal_s *const osal,
                                          const void *const parent)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalParentSet(%p, %p)", (void *)osal, parent);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalParentSet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalParentSet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Set parent */
    osal->parent = parent;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalParentSet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


/**
 * \brief Get pointer to the name field of the given OSAL instance.
 *
 * \param osal  OSAL instance pointer.
 * \param name  Output: pointer to name (must not be NULL; may be set to NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalNameGet(Template_osal_s *const osal,
                                        const char **const name)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalNameGet(%p, %p)", (void *)osal, (void *)name);

    /* Validate args */
    if ((osal == NULL) ||
        (name == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalNameGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        TEMPLATE_OSAL_TRACE("template_osalNameGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Get name */
    *name = osal->name;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalNameGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


/**
 * \brief Set name for the given OSAL instance.
 *
 * \param osal  OSAL instance pointer.
 * \param name  Pointer to name string to set (may be NULL).
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalNameSet(Template_osal_s *const osal,
                                        const char *const name)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalNameSet(%p, %s)", (void *)osal, (name ? name : "(null)"));

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalNameSet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        TEMPLATE_OSAL_TRACE("template_osalNameSet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Set name */
    osal->name = name;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalNameSet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


// BEGIN QUEUE
/*----------------------------------- Queue -------------------------------------------*/

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
                                            Template_osalQueueHandle_t *const queueHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueCreate(%p, %lu, %lu, %p)",
                        (void *)osal, (unsigned long)queueItemSize, (unsigned long)queueDepth, (void *)queueHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if ((osal->vtable == NULL) ||
        (osal->vtable->queueCreate == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueCreate(osal, queueItemSize, queueDepth, queueHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueCreate -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Delete a message queue.
 *
 * \param osal         OSAL instance pointer.
 * \param queueHandle  Queue handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueDelete(Template_osal_s *const osal,
                                            const Template_osalQueueHandle_t queueHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueDelete(%p, %p)", (void *)osal, (void *)queueHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueDelete(osal, queueHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueDelete -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                             const void *const queueItemPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPut(%p, %p, %p)",
                        (void *)osal, (void *)queueHandle, (void *)queueItemPtr);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (queueItemPtr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPut -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPut -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemPut == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPut -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemPut(osal, queueHandle, queueItemPtr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPut -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                              const Template_osalTimeMs_t timeoutMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPost(%p, %p, %p, %u)",
                        (void *)osal, (void *)queueHandle, queueItemPtr,
                        (unsigned int)timeoutMs);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (queueItemPtr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPost -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPost -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemPost == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPost -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemPost(osal, queueHandle, queueItemPtr, timeoutMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPost -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                             void *const queueItemPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemGet(%p, %p, %p)",
                        (void *)osal, (void *)queueHandle, queueItemPtr);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (queueItemPtr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemGet == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemGet(osal, queueHandle, queueItemPtr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemGet -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                              void *const queueItemPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemWait(%p, %p, %p)",
                        (void *)osal, (void *)queueHandle, queueItemPtr);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (queueItemPtr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemWait -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemWait -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemWait == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemWait -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemWait(osal, queueHandle, queueItemPtr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemWait -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                              const Template_osalTimeMs_t timeoutMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPend(%p, %p, %p, %u)",
                        (void *)osal, (void *)queueHandle, (void *)queueItemPtr,
                        (unsigned)timeoutMs);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (queueItemPtr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPend -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPend -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemPend == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPend -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemPend(osal, queueHandle, queueItemPtr, timeoutMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPend -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Reset a queue (discard all items).
 *
 * \param osal         OSAL instance pointer.
 * \param queueHandle  Queue handle to reset.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueReset(Template_osal_s *const osal,
                                           const Template_osalQueueHandle_t queueHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueReset(%p, %p)", (void *)osal, (void *)queueHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueReset -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueReset -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueReset == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueReset -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueReset(osal, queueHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueReset -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                               Template_osalQueueHandle_t *const queueHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet(%p, %lu, %p)",
                        (void *)osal, (unsigned long)queueSlotInd, (void *)queueHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (TEMPLATE_OSAL_QUEUE_SLOTS_NUM <= queueSlotInd))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the queue handle from the specified slot */
    *queueHandle = osal->queueObjHandle[queueSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}
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
                                                   Template_osalStreamBufferHandle_t *const streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate(%p, %lu, %lu, %p)",
                        (void *)osal,
                        (unsigned long)bufferSizeBytes,
                        (unsigned long)triggerLevelBytes,
                        (void *)streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (bufferSizeBytes == 0u) ||
        (triggerLevelBytes == 0u) ||
        (triggerLevelBytes > bufferSizeBytes))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferCreate(osal,
                                         bufferSizeBytes,
                                         triggerLevelBytes,
                                         streamBufferHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Delete a byte stream buffer.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Stream-buffer handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferDelete(Template_osal_s *const osal,
                                                   const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete(%p, %p)",
                        (void *)osal, (void *)streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferDelete(osal, streamBufferHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                size_t *const bytesPut)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferPut(%p, %p, %p, %lu, %p)",
                        (void *)osal,
                        (void *)streamBufferHandle,
                        data,
                        (unsigned long)dataLengthBytes,
                        (void *)bytesPut);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (data == NULL) ||
        (dataLengthBytes == 0u) ||
        (bytesPut == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPut -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPut -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferPut == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPut -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferPut(osal,
                                      streamBufferHandle,
                                      data,
                                      dataLengthBytes,
                                      bytesPut);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferPut -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                 size_t *const bytesPut)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferPost(%p, %p, %p, %lu, %u, %p)",
                        (void *)osal,
                        (void *)streamBufferHandle,
                        data,
                        (unsigned long)dataLengthBytes,
                        (unsigned int)timeoutMs,
                        (void *)bytesPut);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (data == NULL) ||
        (dataLengthBytes == 0u) ||
        (bytesPut == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPost -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPost -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferPost == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPost -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferPost(osal,
                                       streamBufferHandle,
                                       data,
                                       dataLengthBytes,
                                       timeoutMs,
                                       bytesPut);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferPost -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                size_t *const bytesGet)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferGet(%p, %p, %p, %lu, %p)",
                        (void *)osal,
                        (void *)streamBufferHandle,
                        data,
                        (unsigned long)dataLengthBytes,
                        (void *)bytesGet);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (data == NULL) ||
        (dataLengthBytes == 0u) ||
        (bytesGet == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferGet == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferGet(osal,
                                      streamBufferHandle,
                                      data,
                                      dataLengthBytes,
                                      bytesGet);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferGet -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                 size_t *const bytesGet)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferWait(%p, %p, %p, %lu, %p)",
                        (void *)osal,
                        (void *)streamBufferHandle,
                        data,
                        (unsigned long)dataLengthBytes,
                        (void *)bytesGet);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (data == NULL) ||
        (dataLengthBytes == 0u) ||
        (bytesGet == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferWait -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferWait -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferWait == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferWait -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferWait(osal,
                                       streamBufferHandle,
                                       data,
                                       dataLengthBytes,
                                       bytesGet);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferWait -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                 size_t *const bytesGet)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferPend(%p, %p, %p, %lu, %u, %p)",
                        (void *)osal,
                        (void *)streamBufferHandle,
                        data,
                        (unsigned long)dataLengthBytes,
                        (unsigned int)timeoutMs,
                        (void *)bytesGet);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (data == NULL) ||
        (dataLengthBytes == 0u) ||
        (bytesGet == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPend -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPend -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferPend == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferPend -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferPend(osal,
                                       streamBufferHandle,
                                       data,
                                       dataLengthBytes,
                                       timeoutMs,
                                       bytesGet);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferPend -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Reset a stream buffer to its initial empty state.
 *
 * \param osal                OSAL instance pointer.
 * \param streamBufferHandle  Stream-buffer handle to reset.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferReset(Template_osal_s *const osal,
                                                  const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset(%p, %p)",
                        (void *)osal, (void *)streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferReset == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferReset(osal, streamBufferHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                      Template_osalStreamBufferHandle_t *const streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet(%p, %lu, %p)",
                        (void *)osal,
                        (unsigned long)streamBufferSlotInd,
                        (void *)streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (streamBufferSlotInd >= TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the stream-buffer handle from the specified slot */
    *streamBufferHandle = osal->streamBufferObjHandle[streamBufferSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}
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
                                            Template_osalMutexHandle_t *const mutexHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMutexCreate(%p, %p)",
                        (void *)osal, (void *)mutexHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (mutexHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->mutexCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->mutexCreate(osal, mutexHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMutexCreate -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Delete a mutex.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexDelete(Template_osal_s *const osal,
                                            const Template_osalMutexHandle_t mutexHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMutexDelete(%p, %p)",
                        (void *)osal, (void *)mutexHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (mutexHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->mutexDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->mutexDelete(osal, mutexHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMutexDelete -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Lock a mutex and wait indefinitely until it becomes available.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to lock.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexLock(Template_osal_s *const osal,
                                          const Template_osalMutexHandle_t mutexHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMutexLock(%p, %p)",
                        (void *)osal, (void *)mutexHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (mutexHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexLock -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexLock -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->mutexLock == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexLock -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->mutexLock(osal, mutexHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMutexLock -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Try to lock a mutex without waiting.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to lock.
 *
 * \return Template_osalErr_e, zero value = success; otherwise the mutex was not acquired or an error occurred.
 */
Template_osalErr_e template_osalMutexTryLock(Template_osal_s *const osal,
                                             const Template_osalMutexHandle_t mutexHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMutexTryLock(%p, %p)",
                        (void *)osal, (void *)mutexHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (mutexHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexTryLock -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexTryLock -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->mutexTryLock == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexTryLock -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->mutexTryLock(osal, mutexHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMutexTryLock -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                              const Template_osalTimeMs_t timeoutMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMutexPendLock(%p, %p, %u)",
                        (void *)osal, (void *)mutexHandle, (unsigned int)timeoutMs);

    /* Validate args */
    if ((osal == NULL) ||
        (mutexHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexPendLock -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexPendLock -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->mutexPendLock == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexPendLock -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->mutexPendLock(osal, mutexHandle, timeoutMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMutexPendLock -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Unlock a previously locked mutex.
 *
 * \param osal         OSAL instance pointer.
 * \param mutexHandle  Mutex handle to unlock.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalMutexUnlock(Template_osal_s *const osal,
                                            const Template_osalMutexHandle_t mutexHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMutexUnlock(%p, %p)",
                        (void *)osal, (void *)mutexHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (mutexHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexUnlock -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexUnlock -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->mutexUnlock == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexUnlock -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->mutexUnlock(osal, mutexHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMutexUnlock -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                               Template_osalMutexHandle_t *const mutexHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMutexHandleGet(%p, %lu, %p)",
                        (void *)osal, (unsigned long)mutexSlotInd, (void *)mutexHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (mutexHandle == NULL) ||
        (mutexSlotInd >= TEMPLATE_OSAL_MUTEX_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMutexHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the mutex handle from the specified slot */
    *mutexHandle = osal->mutexHandle[mutexSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMutexHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}
// END MUTEX

// BEGIN SEMAPHORE
/*------------------------------ Counting semaphores ------------------------------*/

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
                                                Template_osalSemaphoreHandle_t *const semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate(%p, %u, %u, %p)",
                        (void *)osal,
                        (unsigned int)maxCount,
                        (unsigned int)initialCount,
                        (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL) ||
        (maxCount == 0u) ||
        (initialCount > maxCount))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreCreate(osal, maxCount, initialCount, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Delete a counting semaphore.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreDelete(Template_osal_s *const osal,
                                                const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete(%p, %p)",
                        (void *)osal, (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreDelete(osal, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Wait indefinitely for one counting-semaphore count.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreWait(Template_osal_s *const osal,
                                              const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreWait(%p, %p)",
                        (void *)osal, (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreWait -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreWait -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreWait == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreWait -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreWait(osal, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreWait -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                              const Template_osalTimeMs_t timeoutMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphorePend(%p, %p, %u)",
                        (void *)osal, (void *)semaphoreHandle, (unsigned int)timeoutMs);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphorePend -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphorePend -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphorePend == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphorePend -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphorePend(osal, semaphoreHandle, timeoutMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphorePend -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Post one count to a counting semaphore.
 *
 * \param osal             OSAL instance pointer.
 * \param semaphoreHandle  Counting-semaphore handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphorePost(Template_osal_s *const osal,
                                              const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphorePost(%p, %p)",
                        (void *)osal, (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphorePost -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphorePost -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphorePost == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphorePost -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphorePost(osal, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphorePost -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                  Template_osalSemaphoreCount_t *const semaphoreCount)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet(%p, %p, %p)",
                        (void *)osal, (void *)semaphoreHandle, (void *)semaphoreCount);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL) ||
        (semaphoreCount == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreCountGet == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreCountGet(osal, semaphoreHandle, semaphoreCount);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                   Template_osalSemaphoreHandle_t *const semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet(%p, %lu, %p)",
                        (void *)osal, (unsigned long)semaphoreSlotInd, (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL) ||
        (semaphoreSlotInd >= TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the semaphore handle from the specified slot */
    *semaphoreHandle = osal->semaphoreObjHandle[semaphoreSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}
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
                                                 Template_osalEventFlagsHandle_t *const eventFlagsHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsCreate(%p, %p)",
                        (void *)osal, (void *)eventFlagsHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (eventFlagsHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->eventFlagsCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->eventFlagsCreate(osal, eventFlagsHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsCreate -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Delete an event flags object and release its registry slot.
 *
 * \param osal              OSAL instance pointer.
 * \param eventFlagsHandle  Event flags handle to delete.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalEventFlagsDelete(Template_osal_s *const osal,
                                                 const Template_osalEventFlagsHandle_t eventFlagsHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsDelete(%p, %p)",
                        (void *)osal, (void *)eventFlagsHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (eventFlagsHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->eventFlagsDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->eventFlagsDelete(osal, eventFlagsHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsDelete -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                              const uint32_t flags)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsSet(%p, %p, 0x%08x)",
                        (void *)osal, (void *)eventFlagsHandle, (unsigned int)flags);

    /* Validate args */
    if ((osal == NULL) ||
        (eventFlagsHandle == NULL) ||
        (flags == 0u))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsSet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsSet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->eventFlagsSet == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsSet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->eventFlagsSet(osal, eventFlagsHandle, flags);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsSet -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                const uint32_t flags)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsClear(%p, %p, 0x%08x)",
                        (void *)osal, (void *)eventFlagsHandle, (unsigned int)flags);

    /* Validate args */
    if ((osal == NULL) ||
        (eventFlagsHandle == NULL) ||
        (flags == 0u))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsClear -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsClear -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->eventFlagsClear == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsClear -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->eventFlagsClear(osal, eventFlagsHandle, flags);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsClear -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                              uint32_t *const flags)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsGet(%p, %p, %p)",
                        (void *)osal, (void *)eventFlagsHandle, (void *)flags);

    /* Validate args */
    if ((osal == NULL) ||
        (eventFlagsHandle == NULL) ||
        (flags == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->eventFlagsGet == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->eventFlagsGet(osal, eventFlagsHandle, flags);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsGet -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                               uint32_t *const actualFlags)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsWait(%p, %p, 0x%08x, 0x%02x, %u, %p)",
                        (void *)osal,
                        (void *)eventFlagsHandle,
                        (unsigned int)flags,
                        (unsigned int)options,
                        (unsigned int)timeoutMs,
                        (void *)actualFlags);

    /* Validate args */
    const uint32_t validOptions = TEMPLATE_OSAL_EVENT_FLAGS_WAIT_ALL |
                                  TEMPLATE_OSAL_EVENT_FLAGS_NO_CLEAR;
    if ((osal == NULL) ||
        (eventFlagsHandle == NULL) ||
        (flags == 0u) ||
        (actualFlags == NULL) ||
        (((uint32_t)options & ~validOptions) != 0u))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsWait -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsWait -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->eventFlagsWait == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsWait -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->eventFlagsWait(osal,
                                     eventFlagsHandle,
                                     flags,
                                     options,
                                     timeoutMs,
                                     actualFlags);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsWait -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                    Template_osalEventFlagsHandle_t *const eventFlagsHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsHandleGet(%p, %lu, %p)",
                        (void *)osal,
                        (unsigned long)eventFlagsSlotInd,
                        (void *)eventFlagsHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (eventFlagsHandle == NULL) ||
        (eventFlagsSlotInd >= TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalEventFlagsHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the event flags handle from the specified slot */
    *eventFlagsHandle = osal->eventFlagsObjHandle[eventFlagsSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalEventFlagsHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}
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
                                             Template_osalThreadAttr_s threadAttr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadCreate(%p, %p, {%p,%s,%lu,%p,%d})",
                        (void *)osal, (void *)threadHandle,
                        (void *)threadAttr.worker,
                        (threadAttr.name ? threadAttr.name : "(null)"),
                        (unsigned long)threadAttr.stackSize, threadAttr.args, (int)threadAttr.prio);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->threadCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Attempt to create the thread and get the status */
    const Template_osalErr_e retStatus =
        osal->vtable->threadCreate(osal, threadHandle, threadAttr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadCreate -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                             const Template_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelete(%p, %p)", (void *)osal, (void *)threadHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if (osal->validFlag != true)
    {
        TEMPLATE_OSAL_TRACE("template_osalThreadDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if ((osal->vtable == NULL) ||
        (osal->vtable->threadDelete == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Attempt to delete the thread */
    const Template_osalErr_e retStatus = osal->vtable->threadDelete(osal, threadHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelete -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Suspend the thread.
 *
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to suspend.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadSuspend(Template_osal_s *const osal,
                                              const Template_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadSuspend(%p, %p)", (void *)osal, (void *)threadHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadSuspend -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if (osal->validFlag != true)
    {
        TEMPLATE_OSAL_TRACE("template_osalThreadSuspend -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if ((osal->vtable == NULL) ||
        (osal->vtable->threadSuspend == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadSuspend -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Attempt to suspend the thread */
    const Template_osalErr_e retStatus = osal->vtable->threadSuspend(osal, threadHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadSuspend -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Resume the thread.
 *
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to resume.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadResume(Template_osal_s *const osal,
                                             const Template_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadResume(%p, %p)", (void *)osal, (void *)threadHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadResume -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        TEMPLATE_OSAL_TRACE("template_osalThreadResume -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->threadResume == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadResume -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Attempt to resume the thread */
    const Template_osalErr_e retStatus = osal->vtable->threadResume(osal, threadHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadResume -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Delay the execution of the current thread.
 *
 * \param osal     Pointer to OSAL instance.
 * \param delayMs  Delay duration in milliseconds.
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadDelay(Template_osal_s *const osal,
                                            const Template_osalTimeMs_t delayMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelay(%p, %u)", (void *)osal, (unsigned)delayMs);

    /* Validate parameters */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelay -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        TEMPLATE_OSAL_TRACE("template_osalThreadDelay -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->threadDelay == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelay -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delay the current thread */
    const Template_osalErr_e retStatus = osal->vtable->threadDelay(osal, delayMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelay -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                 const Template_osalTimeMs_t periodMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelayUntil(%p, %p, %u)",
                        (void *)osal, (void *)previousWakeTimeMs, (unsigned int)periodMs);

    /* Validate args */
    if ((osal == NULL) ||
        (previousWakeTimeMs == NULL) ||
        (periodMs == 0u))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelayUntil -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelayUntil -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->threadDelayUntil == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelayUntil -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->threadDelayUntil(osal, previousWakeTimeMs, periodMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelayUntil -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Terminate the calling thread (does not return).
 *
 * \param osal  Pointer to OSAL instance (must be valid).
 *
 * \note This function never returns control to the caller.
 */
void template_osalThreadExit(Template_osal_s *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadExit(%p)", (void *)osal);

    /* Validate invariants */
    TEMPLATE_OSAL_ASSERT(osal != NULL);
    TEMPLATE_OSAL_ASSERT(osal->validFlag == true);
    TEMPLATE_OSAL_ASSERT(osal->vtable != NULL);
    TEMPLATE_OSAL_ASSERT(osal->vtable->threadExit != NULL);

    /* Terminate the current thread (must not return) */
    osal->vtable->threadExit(osal);

    /* Should never reach here */
    TEMPLATE_OSAL_ASSERT(0);

    /* Placeholder to prevent formatter collapsing and to satisfy analyzers */
    while (1)
    {
        /* no-return placeholder */
        (void)0;
    }
}


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
                                                Template_osalThreadHandle_t *const threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet(%p, %lu, %p)",
                        (void *)osal, (unsigned long)threadSlotInd, (void *)threadHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL) ||
        (TEMPLATE_OSAL_THREAD_SLOTS_NUM <= threadSlotInd))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Copy the thread handle from the specified slot */
    *threadHandle = osal->threadObjHandle[threadSlotInd].handle;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: summary status returned
}
// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section -------------------------------*/
/**
 * \brief Enter a short OS critical section.
 *
 * \param osal  OSAL instance pointer.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalCriticalSectionEnter(Template_osal_s *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter(%p)", (void *)osal);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->criticalSectionEnter == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->criticalSectionEnter(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Exit a previously entered OS critical section.
 *
 * \param osal  OSAL instance pointer.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalCriticalSectionExit(Template_osal_s *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit(%p)", (void *)osal);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->criticalSectionExit == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->criticalSectionExit(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*-------------------------------- Software timers --------------------------------*/
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
                                                    Template_osalSoftwareTimerAttr_s timerAttr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate(%p, %p, {%s, %p, %p, %d, %u})",
                        (void *)osal, (void *)timerHandle,
                        (timerAttr.name != NULL) ? timerAttr.name : "(null)",
                        timerAttr.timerParam,
                        (void *)(uintptr_t)timerAttr.timerExpiredCb,
                        (int)timerAttr.autoReload,
                        (unsigned int)timerAttr.periodMs);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL) ||
        (timerAttr.timerExpiredCb == NULL) ||
        (timerAttr.periodMs == 0u) ||
        (timerAttr.periodMs == TEMPLATE_OSAL_INFINITY_TOUT))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerCreate(osal, timerHandle, timerAttr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Delete a software timer.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerDelete(Template_osal_s *const osal,
                                                    const Template_osalSoftwareTimerHandle_t timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete(%p, %p)",
                        (void *)osal, (void *)timerHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerDelete(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Start a software timer using its configured period.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerStart(Template_osal_s *const osal,
                                                   const Template_osalSoftwareTimerHandle_t timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart(%p, %p)",
                        (void *)osal, (void *)timerHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerStart == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerStart(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Stop a software timer.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerStop(Template_osal_s *const osal,
                                                  const Template_osalSoftwareTimerHandle_t timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop(%p, %p)",
                        (void *)osal, (void *)timerHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerStop == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerStop(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


/**
 * \brief Reset a software timer and restart its configured period.
 *
 * \param osal         OSAL instance pointer.
 * \param timerHandle  Software timer handle.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerReset(Template_osal_s *const osal,
                                                   const Template_osalSoftwareTimerHandle_t timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset(%p, %p)",
                        (void *)osal, (void *)timerHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerReset == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerReset(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                                       Template_osalSoftwareTimerHandle_t *const timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet(%p, %lu, %p)",
                        (void *)osal, (unsigned long)timerSlotInd, (void *)timerHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL) ||
        (timerSlotInd >= TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the resource handle from the specified slot */
    *timerHandle = osal->softwareTimerObj[timerSlotInd].handle;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


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
                                          Template_osalTimeMs_t *const osTimeMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalTimeMsGet(%p, %p)", (void *)osal, (void *)osTimeMs);

    /* Validate parameters */
    if ((osal == NULL) ||
        (osTimeMs == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalTimeMsGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalTimeMsGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->timeMsGet == NULL)
    {
        TEMPLATE_OSAL_TRACE("template_osalTimeMsGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Get the current OS time */
    const Template_osalErr_e retStatus = osal->vtable->timeMsGet(osal, osTimeMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalTimeMsGet -> %d", retStatus);

    return retStatus;  // Exit: Success: summary status returned
}
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
                                       void **const memPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMalloc(%p, %lu, %p)", (void *)osal, (unsigned long)size, (void *)memPtr);

    /* Validate parameters */
    if ((osal == NULL) ||
        (memPtr == NULL) ||
        (size == 0u))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  /// Exit: Error: not initialized
    }

    if (osal->vtable->memAlloc == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delegate allocation (only now) */
    void *ptr                    = NULL;
    Template_osalErr_e retStatus = osal->vtable->memAlloc(osal, size, &ptr);
    if ((retStatus != TEMPLATE_OSAL_NO_ERR) ||
        (ptr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d",
                            (retStatus != TEMPLATE_OSAL_NO_ERR) ? retStatus : TEMPLATE_OSAL_MEM_ALLOCATION_ERR);

        return (retStatus != TEMPLATE_OSAL_NO_ERR) ? retStatus  // Exit: Success: operation completed
                                                   : TEMPLATE_OSAL_MEM_ALLOCATION_ERR;  // Exit: Error: backend failed or returned NULL
    }

    *memPtr = ptr;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}


/**
 * \brief Free memory via the OSAL backend and unregister the pointer internally.
 *
 * \param osal    Pointer to OSAL instance.
 * \param memPtr  Pointer to the memory block to free (must not be NULL and must be registered).
 *
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalFree(Template_osal_s *const osal,
                                     void *const memPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalFree(%p, %p)", (void *)osal, memPtr);

    /* Validate parameters */
    if ((osal == NULL) ||
        (memPtr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalFree -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalFree -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->memFree == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalFree -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: backend-specific operation failed
    }

    /* Delegate free to backend */
    const Template_osalErr_e retStatus = osal->vtable->memFree(osal, memPtr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalFree -> %d", retStatus);

    return retStatus;  // Exit: Success: backend status returned
}


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
                                          void **const memPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMemPtrGet(%p, %lu, %p)",
                        (void *)osal, (unsigned long)memSlotInd, (void *)memPtr);

    /* Validate parameters */
    if ((osal == NULL) ||
        (memPtr == NULL) ||
        (TEMPLATE_OSAL_MEM_SLOTS_NUM <= memSlotInd))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMemPtrGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMemPtrGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    *memPtr = osal->memPtr[memSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMemPtrGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Success: operation completed
}
// END MEMORY

/*============================================================================[ PRIVATE FUNCTIONS ]============================================================================*/

/**
 * \brief Reset (clear) the internal OSAL objects registry
 *
 * \param osal  OSAL instance pointer.
 */
static void template_osalRegReset(Template_osal_s *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegReset(%p)", (void *)osal);

    /* Validate by caller */
    TEMPLATE_OSAL_ASSERT(osal != NULL);

    /* Keep release builds warning-free when all registry-backed groups are disabled. */
    (void)osal;

    // BEGIN QUEUE
    /* Reset queue slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        osal->queueObjHandle[i] = NULL;
    }

    // END QUEUE

    // BEGIN STREAM_BUFFER
    /* Reset stream buffer slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM; ++i)
    {
        osal->streamBufferObjHandle[i] = NULL;
    }

    // END STREAM_BUFFER

    // BEGIN MUTEX
    /* Reset mutex slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_MUTEX_SLOTS_NUM; ++i)
    {
        osal->mutexHandle[i] = NULL;
    }

    // END MUTEX

    // BEGIN SEMAPHORE
    /* Reset counting semaphore slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM; ++i)
    {
        osal->semaphoreObjHandle[i] = NULL;
    }

    // END SEMAPHORE

    // BEGIN EVENT_FLAGS
    /* Reset event flags slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM; ++i)
    {
        osal->eventFlagsObjHandle[i] = NULL;
    }

    // END EVENT_FLAGS

    // BEGIN THREAD
    /* Reset thread slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        osal->threadObjHandle[i].attr.worker    = NULL;
        osal->threadObjHandle[i].attr.name      = NULL;
        osal->threadObjHandle[i].attr.stackSize = 0u;
        osal->threadObjHandle[i].attr.args      = NULL;
        osal->threadObjHandle[i].attr.prio      = TEMPLATE_OSAL_THREAD_PRIO_LOW;
        osal->threadObjHandle[i].handle        = NULL;
    }

    // END THREAD

    // BEGIN SOFTWARE_TIMER
    /* Reset software timer slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM; ++i)
    {
        osal->softwareTimerObj[i].handle             = NULL;
        osal->softwareTimerObj[i].attr.name           = NULL;
        osal->softwareTimerObj[i].attr.timerParam     = NULL;
        osal->softwareTimerObj[i].attr.timerExpiredCb = NULL;
        osal->softwareTimerObj[i].attr.autoReload     = false;
        osal->softwareTimerObj[i].attr.periodMs       = 0u;
    }

    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    /* Reset memory registry slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        osal->memPtr[i] = NULL;
    }

    // END MEMORY

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegReset -> ok");
}


// BEGIN QUEUE
/*-------------------------------- Registry: Queues -------------------------------*/

/**
 * \brief Find a free queue slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegQueueFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegQueueFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osal->queueObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegQueueFreeSlotFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegQueueFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find queue handle.
 *
 * \param osalPort     Derived OSAL pointer.
 * \param queueHandle  Handle to search.
 *
 * \return Queue ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegQueueHandleFind(void *const osalPort,
                                              const Template_osalQueueHandle_t queueHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegQueueHandleFind(%p, %p)", osalPort, (void *)queueHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(queueHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osal->queueObjHandle[i] == queueHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegQueueHandleFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegQueueHandleFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}
// END QUEUE

// BEGIN STREAM_BUFFER
/*--------------------------- Registry: Stream buffers ---------------------------*/

/**
 * \brief Find a free stream-buffer slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegStreamBufferFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM; ++i)
    {
        if (osal->streamBufferObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferFreeSlotFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find a stream-buffer handle.
 *
 * \param osalPort            Derived OSAL pointer.
 * \param streamBufferHandle  Handle to search.
 *
 * \return Stream-buffer ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegStreamBufferHandleFind(void *const osalPort,
                                                     const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferHandleFind(%p, %p)",
                        osalPort, (void *)streamBufferHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(streamBufferHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM; ++i)
    {
        if (osal->streamBufferObjHandle[i] == streamBufferHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferHandleFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferHandleFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


// END STREAM_BUFFER

// BEGIN MUTEX
/*-------------------------------- Registry: Mutexes --------------------------------*/

/**
 * \brief Find a free mutex slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegMutexFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegMutexFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_MUTEX_SLOTS_NUM; ++i)
    {
        if (osal->mutexHandle[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegMutexFreeSlotFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegMutexFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find mutex handle.
 *
 * \param osalPort     Derived OSAL pointer.
 * \param mutexHandle  Handle to search.
 *
 * \return Mutex ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegMutexHandleFind(void *const osalPort,
                                             const Template_osalMutexHandle_t mutexHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegMutexHandleFind(%p, %p)", osalPort, (void *)mutexHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(mutexHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_MUTEX_SLOTS_NUM; ++i)
    {
        if (osal->mutexHandle[i] == mutexHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegMutexHandleFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegMutexHandleFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}
// END MUTEX

// BEGIN SEMAPHORE
/*------------------------- Registry: Counting semaphores -------------------------*/

/**
 * \brief Find a free counting-semaphore slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegSemaphoreFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM; ++i)
    {
        if (osal->semaphoreObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreFreeSlotFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find a counting-semaphore handle.
 *
 * \param osalPort         Derived OSAL pointer.
 * \param semaphoreHandle  Handle to search.
 *
 * \return Semaphore ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegSemaphoreHandleFind(void *const osalPort,
                                                  const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreHandleFind(%p, %p)", osalPort, (void *)semaphoreHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(semaphoreHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM; ++i)
    {
        if (osal->semaphoreObjHandle[i] == semaphoreHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreHandleFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreHandleFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


// END SEMAPHORE

// BEGIN EVENT_FLAGS
/*------------------------------- Registry: Event flags -------------------------------*/

/**
 * \brief Find a free event-flags slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegEventFlagsFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegEventFlagsFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    /* Search for an empty registry slot */
    for (size_t i = 0; i < TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM; ++i)
    {
        if (osal->eventFlagsObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegEventFlagsFreeSlotFind -> %lu",
                                (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegEventFlagsFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find an event-flags handle.
 *
 * \param osalPort          Derived OSAL pointer.
 * \param eventFlagsHandle  Handle to search.
 *
 * \return Event-flags ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegEventFlagsHandleFind(void *const osalPort,
                                                   const Template_osalEventFlagsHandle_t eventFlagsHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegEventFlagsHandleFind(%p, %p)",
                        osalPort, (void *)eventFlagsHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(eventFlagsHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    /* Search for the registered handle */
    for (size_t i = 0; i < TEMPLATE_OSAL_EVENT_FLAGS_SLOTS_NUM; ++i)
    {
        if (osal->eventFlagsObjHandle[i] == eventFlagsHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegEventFlagsHandleFind -> %lu",
                                (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegEventFlagsHandleFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}
// END EVENT_FLAGS

// BEGIN THREAD
/*------------------------------- Registry: Threads -------------------------------*/

/**
 * \brief Find a free thread slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegThreadFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osal->threadObjHandle[i].handle == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegThreadFreeSlotFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find thread handle.
 *
 * \param osalPort      Derived OSAL pointer.
 * \param threadHandle  Handle to search.
 *
 * \return Thread ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegThreadHandleFind(void *const osalPort,
                                               const Template_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadHandleFind(%p, %p)", osalPort, (void *)threadHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(threadHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osal->threadObjHandle[i].handle == threadHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegThreadHandleFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadHandleFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Clear a thread registry slot.
 *
 * \param osalPort   Derived OSAL pointer.
 * \param threadIdx  Zero-based thread registry index.
 */
static void template_osalRegThreadSlotClear(void *const osalPort,
                                            const size_t threadIdx)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadSlotClear(%p, %lu)", osalPort, (unsigned long)threadIdx);

    /* Validate input args */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(threadIdx < TEMPLATE_OSAL_THREAD_SLOTS_NUM);

    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    /* Clear the thread registry slot */
    osal->threadObjHandle[threadIdx].attr.worker    = NULL;
    osal->threadObjHandle[threadIdx].attr.name      = NULL;
    osal->threadObjHandle[threadIdx].attr.stackSize = 0u;
    osal->threadObjHandle[threadIdx].attr.args      = NULL;
    osal->threadObjHandle[threadIdx].attr.prio      = TEMPLATE_OSAL_THREAD_PRIO_LOW;
    osal->threadObjHandle[threadIdx].handle        = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Trace returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadSlotClear -> ok");
}
// END THREAD

// BEGIN SOFTWARE_TIMER
/*--------------------------- Registry: Software timers ---------------------------*/

/**
 * \brief Find a free software-timer slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegSoftwareTimerFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM; ++i)
    {
        if (osal->softwareTimerObj[i].handle == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerFreeSlotFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find a software-timer handle.
 *
 * \param osalPort     Derived OSAL pointer.
 * \param timerHandle  Handle to search.
 *
 * \return Software-timer ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegSoftwareTimerHandleFind(void *const osalPort,
                                                      const Template_osalSoftwareTimerHandle_t timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerHandleFind(%p, %p)", osalPort, (void *)timerHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(timerHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM; ++i)
    {
        if (osal->softwareTimerObj[i].handle == timerHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerHandleFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerHandleFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


// END SOFTWARE_TIMER


// BEGIN MEMORY
/*-------------------------------- Registry: Memory --------------------------------*/

/**
 * \brief Find a free memory slot.
 *
 * \param osalPort  Derived OSAL pointer.
 *
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegMemFreeSlotFind(void *const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegMemFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osal->memPtr[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegMemFreeSlotFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegMemFreeSlotFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}


/**
 * \brief Find a registered memory pointer in the internal registry.
 *
 * \param osalPort  Derived OSAL pointer.
 * \param memPtr    Memory pointer to search.
 *
 * \return Memory slot ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegMemPtrFind(void *const osalPort,
                                         const void *const memPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegMemPtrFind(%p, %p)", osalPort, memPtr);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(memPtr != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osal->memPtr[i] == memPtr)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegMemPtrFind -> %lu", (unsigned long)(i + 1u));

            return i + 1u;  // Exit: Success: matching registry slot found
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegMemPtrFind -> %d", 0);

    return 0u;  // Exit: Error: matching registry slot was not found
}
// END MEMORY
