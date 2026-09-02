/*
 * SPDX-License-Identifier: MIT
 */

/**
 * \file      template_osal.c
 * \brief     OSAL layer interface implementation for Template.
 * \details   The concrete OSAL port is supplied via the vtable vtable in the RTOS layer.

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
 * \brief  Reset OSAL objects such as queues, lock objects, threads and memory slots.
 */
static void template_osalResetObjects(Template_osal_s *const osal);

// BEGIN QUEUE
/**
 * \brief  Find a free queue slot.
 */
static size_t template_osalRegQueueFreeSlotFind(void * const osalPort);

/**
 * \brief  Find queue handle.
 */
static size_t template_osalRegQueueHandleFind(void * const osalPort,
                                              const Template_osalQueueHandle_t queueHandle);
// END QUEUE

// BEGIN STREAM_BUFFER
/**
 * \brief  Find a free stream-buffer slot.
 */
static size_t template_osalRegStreamBufferFreeSlotFind(void * const osalPort);

/**
 * \brief  Find a stream-buffer handle.
 */
static size_t template_osalRegStreamBufferHandleFind(void * const osalPort,
                                                     const Template_osalStreamBufferHandle_t streamBufferHandle);
// END STREAM_BUFFER

// BEGIN LOCK
/**
 * \brief  Find a free lock object slot.
 */
static size_t template_osalRegLockFreeSlotFind(void * const osalPort);

/**
 * \brief  Find lock object handle.
 */
static size_t template_osalRegLockHandleFind(void * const osalPort,
                                             const Template_osalLockObjHandle_t lockObjHandle);
// END LOCK

// BEGIN SEMAPHORE
/**
 * \brief  Find a free counting-semaphore slot.
 */
static size_t template_osalRegSemaphoreFreeSlotFind(void * const osalPort);

/**
 * \brief  Find a counting-semaphore handle.
 */
static size_t template_osalRegSemaphoreHandleFind(void * const osalPort,
                                                  const Template_osalSemaphoreHandle_t semaphoreHandle);
// END SEMAPHORE

// BEGIN THREAD
/**
 * \brief  Find a free thread slot.
 */
static size_t template_osalRegThreadFreeSlotFind(void * const osalPort);

/**
 * \brief  Find thread handle.
 */
static size_t template_osalRegThreadHandleFind(void * const osalPort,
                                               const Template_osalThreadHandle_t threadHandle);

/**
 * \brief  Clear a thread registry slot.
 */
static void template_osalRegThreadSlotClear(void * const osalPort,
                                            const size_t threadIdx);
// END THREAD

// BEGIN SOFTWARE_TIMER
/**
 * \brief  Find a free software-timer slot.
 */
static size_t template_osalRegSoftwareTimerFreeSlotFind(void * const osalPort);

/**
 * \brief  Find a software-timer handle.
 */
static size_t template_osalRegSoftwareTimerHandleFind(void * const osalPort,
                                                      const Template_osalSoftwareTimerHandle_t timerHandle);
// END SOFTWARE_TIMER

// BEGIN MEMORY
/**
 * \brief  Find a free memory slot.
 */
static size_t template_osalRegMemFreeSlotFind(void * const osalPort);

/**
 * \brief  Find pointer in memory registry.
 */
static size_t template_osalRegMemHandleFind(void * const osalPort,
                                            const void * const ptr);
// END MEMORY

/**
 * \brief   Protected registry helpers vtable (for backend ports).
 * \details Provides unified helpers for slot search and handle lookup for all registry-backed primitive groups.
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

    // BEGIN LOCK
    /*-------------------------------- Locks --------------------------------*/
    .lockObjFreeSlotFind = template_osalRegLockFreeSlotFind,
    .lockObjHandleFind   = template_osalRegLockHandleFind,
    // END LOCK

    // BEGIN SEMAPHORE
    /*--------------------------- Counting semaphores --------------------------*/
    .semaphoreFreeSlotFind = template_osalRegSemaphoreFreeSlotFind,
    .semaphoreHandleFind   = template_osalRegSemaphoreHandleFind,
    // END SEMAPHORE

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
    .memHandleFind   = template_osalRegMemHandleFind,
    // END MEMORY

    .reserved = 0u
};


/*=======================================================================[ PUBLIC INTERFACE FUNCTIONS ]================================================================*/

/*---------------------------------- Lifecycle --------------------------------------*/

/**
 * \brief  Initialize Template OSAL instance; set name/parent and clear internal objects.
 * \param  osal    OSAL instance pointer.
 * \param  name    Optional instance name (may be NULL).
 * \param  parent  Optional parent pointer (opaque; may be NULL).
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    /* Init instance */
    osal->validFlag = false;
    osal->name      = name;
    osal->parent    = parent;

    /* Reset internals */
    template_osalResetObjects(osal);

    /* Reset methods table before use */
    osal->vtable = NULL;

    /* Assign ptable methods table */
    osal->ptable = &template_osalPtable;

    /* Mark as valid */
    osal->validFlag = true;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalInit -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Error: Error: Success
}


/**
 * \brief  Deinitialize Template OSAL instance.
 * \param  osal  OSAL instance pointer.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalDeinit -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    /* Clear instance */
    osal->validFlag = false;
    osal->name      = NULL;
    osal->parent    = NULL;
    osal->vtable    = NULL;

    /* Reset internals */
    template_osalResetObjects(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalDeinit -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: Error: success
}


/**
 * \brief  Validate OSAL instance (including vtable layer).
 * \param  osal  OSAL instance pointer (const).
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

        return false;  // Exit: Error: Error: not initialized
    }

    /* Check vtable table presence */
    if (osal->vtable == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", 0);

        return false;  // Exit: Error: Error: No vtable table
    }

    /* Check vtable predicate presence */
    if (osal->vtable->isValid == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", 0);

        return false;  // Exit: Error: Error: missing backend predicate
    }

    /* Delegate to backend */
    const bool ok = osal->vtable->isValid(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalIsValid -> %d", (int)ok);

    return ok;  // Exit:  return the summary validation status
}


/*---------------------------------- Metadata -----------------------------------------*/

/**
 * \brief  Get pointer to a parent of the given OSAL object.
 * \param  osal     OSAL instance pointer.
 * \param  parent   Output: pointer to parent (must not be NULL; may be set to NULL).
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalParentGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    /* Get parent */
    *parent = (void *)osal->parent;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalParentGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief  Set the parent object for the given OSAL instance.
 * \param  osal    OSAL instance pointer.
 * \param  parent  Parent pointer to be set (may be NULL).
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

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief  Get pointer to the name field of the given OSAL instance.
 * \param  osal  OSAL instance pointer.
 * \param  name  Output: pointer to name (must not be NULL; may be set to NULL).
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

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief  Set name for the given OSAL instance.
 * \param  osal  OSAL instance pointer.
 * \param  name  Pointer to name string to set (may be NULL).
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

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


// BEGIN QUEUE
/*----------------------------------- Queue -------------------------------------------*/

/**
 * \brief  Create a message queue.
 * \param  osal           OSAL instance pointer.
 * \param  queueItemSize  Size of a single queue item in bytes.
 * \param  queueDepth     Maximum number of items the queue can hold.
 * \param  queueHandle    Output: created queue handle (must not be NULL).
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueCreate(Template_osal_s *const osal,
                                            const size_t queueItemSize,
                                            const size_t queueDepth,
                                            Template_osalQueueHandle_t *const queueHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueCreate(%p, %zu, %zu, %p)",
                        (void *)osal, queueItemSize, queueDepth, (void *)queueHandle);

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

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueCreate(osal, queueItemSize, queueDepth, queueHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueCreate -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Delete a message queue.
 * \param  osal         OSAL instance pointer.
 * \param  queueHandle  Queue handle to delete.
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

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueDelete(osal, queueHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueDelete -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Put an item into a queue.
 * \param  osal          OSAL instance pointer.
 * \param  queueHandle   Queue handle.
 * \param  queueItemPtr  Pointer to the item to enqueue.
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

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemPut(osal, queueHandle, queueItemPtr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPut -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Post an item to a queue, waiting up to the requested timeout for free capacity.
 * \param  osal          OSAL instance pointer.
 * \param  queueHandle   Queue handle.
 * \param  queueItemPtr  Pointer to the item to enqueue.
 * \param  timeoutMs     Maximum wait time in milliseconds.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPost -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemPost == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemPost -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemPost(osal, queueHandle, queueItemPtr, timeoutMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPost -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Retrieve an already available item from a queue without waiting.
 * \param  osal          OSAL instance pointer.
 * \param  queueHandle   Queue handle.
 * \param  queueItemPtr  Destination buffer for the item.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemGet == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemGet(osal, queueHandle, queueItemPtr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemGet -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Wait indefinitely for an item and retrieve it from a queue.
 * \param  osal          OSAL instance pointer.
 * \param  queueHandle   Queue handle.
 * \param  queueItemPtr  Destination buffer for the item.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemWait -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemWait == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueItemWait -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemWait(osal, queueHandle, queueItemPtr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemWait -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Get an item from a queue (blocking with timeout).
 * \param  osal          OSAL instance pointer.
 * \param  queueHandle   Queue handle.
 * \param  queueItemPtr  Destination buffer for the item.
 * \param  timeoutMs     Timeout in milliseconds.
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

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueItemPend(osal, queueHandle, queueItemPtr, timeoutMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueItemPend -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Reset a queue (discard all items).
 * \param  osal         OSAL instance pointer.
 * \param  queueHandle  Queue handle to reset.
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

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->queueReset(osal, queueHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueReset -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Get a queue handle of the given OSAL object.
 * \param  osal           Pointer to OSAL instance.
 * \param  queueSlotInd   Index of queue slot.
 * \param  queueHandle    Pointer to the current queue handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalQueueHandleGet(Template_osal_s *const osal,
                                               const size_t queueSlotInd,
                                               Template_osalQueueHandle_t *const queueHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet(%p, %zu, %p)",
                        (void *)osal, queueSlotInd, (void *)queueHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (TEMPLATE_OSAL_QUEUE_SLOTS_NUM <= queueSlotInd))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit
    }

    /* Retrieve the queue handle from the specified slot */
    *queueHandle = osal->queueObjHandle[queueSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalQueueHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;
}
// END QUEUE

// BEGIN STREAM_BUFFER
/*-------------------------------- Stream buffers --------------------------------*/

/**
 * \brief  Create a byte stream buffer.
 * \param  osal                 OSAL instance pointer.
 * \param  bufferSizeBytes      Stream buffer capacity in bytes.
 * \param  triggerLevelBytes    Receive trigger level in bytes.
 * \param  streamBufferHandle   Output pointer receiving the created stream buffer handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferCreate(Template_osal_s *const osal,
                                                    const size_t bufferSizeBytes,
                                                    const size_t triggerLevelBytes,
                                                    Template_osalStreamBufferHandle_t *const streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate(%p, %zu, %zu, %p)",
                        (void *)osal, bufferSizeBytes, triggerLevelBytes, (void *)streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (bufferSizeBytes == 0u) ||
        (triggerLevelBytes == 0u) ||
        (triggerLevelBytes > bufferSizeBytes))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferCreate(osal,
                                         bufferSizeBytes,
                                         triggerLevelBytes,
                                         streamBufferHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferCreate -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Delete a stream buffer.
 * \param  osal                 OSAL instance pointer.
 * \param  streamBufferHandle   Stream buffer handle to delete.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferDelete(Template_osal_s *const osal,
                                                    const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete(%p, %p)",
                        (void *)osal, streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferDelete(osal, streamBufferHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferDelete -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Send bytes to a stream buffer without waiting for capacity.
 * \param  osal                 OSAL instance pointer.
 * \param  streamBufferHandle   Target stream buffer handle.
 * \param  data                 Pointer to source bytes.
 * \param  dataLengthBytes      Number of bytes requested for transfer.
 * \param  bytesSent            Output pointer receiving the number of bytes written.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferSend(Template_osal_s *const osal,
                                                  const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                  const void *const data,
                                                  const size_t dataLengthBytes,
                                                  size_t *const bytesSent)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferSend(%p, %p, %p, %zu, %p)",
                        (void *)osal, streamBufferHandle, data, dataLengthBytes,
                        (void *)bytesSent);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (data == NULL) ||
        (dataLengthBytes == 0u) ||
        (bytesSent == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferSend -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferSend -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferSend == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferSend -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferSend(osal,
                                       streamBufferHandle,
                                       data,
                                       dataLengthBytes,
                                       bytesSent);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferSend -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Receive bytes from a stream buffer with an explicit timeout.
 * \param  osal                 OSAL instance pointer.
 * \param  streamBufferHandle   Source stream buffer handle.
 * \param  data                 Destination buffer.
 * \param  dataLengthBytes      Maximum number of bytes to receive.
 * \param  timeoutMs            Maximum wait time in milliseconds.
 * \param  bytesReceived        Output pointer receiving the number of bytes read.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferReceive(Template_osal_s *const osal,
                                                     const Template_osalStreamBufferHandle_t streamBufferHandle,
                                                     void *const data,
                                                     const size_t dataLengthBytes,
                                                     const Template_osalTimeMs_t timeoutMs,
                                                     size_t *const bytesReceived)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferReceive(%p, %p, %p, %zu, %u, %p)",
                        (void *)osal, streamBufferHandle, data, dataLengthBytes,
                        (unsigned int)timeoutMs, (void *)bytesReceived);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (data == NULL) ||
        (dataLengthBytes == 0u) ||
        (bytesReceived == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReceive -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReceive -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferReceive == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReceive -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferReceive(osal,
                                          streamBufferHandle,
                                          data,
                                          dataLengthBytes,
                                          timeoutMs,
                                          bytesReceived);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferReceive -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Reset a stream buffer to its initial empty state.
 * \param  osal                 OSAL instance pointer.
 * \param  streamBufferHandle   Stream buffer handle to reset.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferReset(Template_osal_s *const osal,
                                                   const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset(%p, %p)",
                        (void *)osal, streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->streamBufferReset == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->streamBufferReset(osal, streamBufferHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferReset -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Get a stream buffer handle from a stable registry slot.
 * \param  osal                 OSAL instance pointer.
 * \param  streamBufferSlotInd  Zero-based stream buffer registry slot index.
 * \param  streamBufferHandle   Output pointer receiving the current slot handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalStreamBufferHandleGet(Template_osal_s *const osal,
                                                       const size_t streamBufferSlotInd,
                                                       Template_osalStreamBufferHandle_t *const streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet(%p, %zu, %p)",
                        (void *)osal, streamBufferSlotInd, (void *)streamBufferHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (streamBufferHandle == NULL) ||
        (streamBufferSlotInd >= TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Retrieve the resource handle from the specified slot */
    *streamBufferHandle = osal->streamBufferObjHandle[streamBufferSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalStreamBufferHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


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
                                              Template_osalLockObjHandle_t *const lockObjHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalLockObjCreate(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        TEMPLATE_OSAL_TRACE("template_osalLockObjCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLockObjCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;
    }

    if (osal->vtable->lockObjCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLockObjCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Create the lock object */
    const Template_osalErr_e retStatus = osal->vtable->lockObjCreate(osal, lockObjHandle);

    TEMPLATE_OSAL_TRACE("template_osalLockObjCreate -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Delete a lock object.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object to delete.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalLockObjDelete(Template_osal_s *const osal,
                                              const Template_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalLockObjDelete(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate  parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLockObjDelete -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLockObjDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->lockObjDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLockObjDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Delete the lock object */
    const Template_osalErr_e retStatus = osal->vtable->lockObjDelete(osal, lockObjHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalLockObjDelete -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Lock access to the resource.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalLock(Template_osal_s *const osal,
                                     const Template_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalLock(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLock -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        TEMPLATE_OSAL_TRACE("template_osalLock -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->lock == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLock -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Lock access to the resource */
    const Template_osalErr_e retStatus = osal->vtable->lock(osal, lockObjHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalLock -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Unlock access to the resource.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalUnlock(Template_osal_s *const osal,
                                       const Template_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalUnlock(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalUnlock -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalUnlock -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->unlock == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalUnlock -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Unlock access to the resource */
    const Template_osalErr_e retStatus = osal->vtable->unlock(osal, lockObjHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalUnlock -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Get a lock object handle of the given OSAL object.
 * \param  osal            Pointer to the OSAL instance.
 * \param  lockObjSlotInd  Index of the lock object slots.
 * \param  lockObjHandle   Pointer where the lock object handle will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalLockObjHandleGet(Template_osal_s *const osal,
                                                 const size_t lockObjSlotInd,
                                                 Template_osalLockObjHandle_t *const lockObjHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalLockObjHandleGet(%p, %zu, %p)",
                        (void *)osal, lockObjSlotInd, (void *)lockObjHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL) ||
        (TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM <= lockObjSlotInd))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLockObjHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalLockObjHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the lock object handle */
    *lockObjHandle = osal->lockObjHandle[lockObjSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalLockObjHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;
}
// END LOCK

// BEGIN SEMAPHORE
/*------------------------------ Counting semaphores ------------------------------*/

/**
 * \brief  Create a counting semaphore.
 * \param  osal                 OSAL instance pointer.
 * \param  maxCount             Maximum semaphore count.
 * \param  initialCount         Initial semaphore count.
 * \param  semaphoreHandle      Output pointer receiving the created semaphore handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreCreate(Template_osal_s *const osal,
                                                 const Template_osalSemaphoreCount_t maxCount,
                                                 const Template_osalSemaphoreCount_t initialCount,
                                                 Template_osalSemaphoreHandle_t *const semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate(%p, %u, %u, %p)",
                        (void *)osal, (unsigned int)maxCount, (unsigned int)initialCount,
                        (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL) ||
        (maxCount == 0u) ||
        (initialCount > maxCount))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreCreate(osal, maxCount, initialCount, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreCreate -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Delete a counting semaphore.
 * \param  osal                 OSAL instance pointer.
 * \param  semaphoreHandle      Counting semaphore handle.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreDelete(osal, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreDelete -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Acquire a counting semaphore without waiting.
 * \param  osal                 OSAL instance pointer.
 * \param  semaphoreHandle      Counting semaphore handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreAcquire(Template_osal_s *const osal,
                                                 const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquire(%p, %p)",
                        (void *)osal, (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquire -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquire -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreAcquire == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquire -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreAcquire(osal, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquire -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Acquire a counting semaphore, waiting up to the requested timeout.
 * \param  osal                 OSAL instance pointer.
 * \param  semaphoreHandle      Counting semaphore handle.
 * \param  timeoutMs            Maximum wait time in milliseconds.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreAcquireWait(Template_osal_s *const osal,
                                                      const Template_osalSemaphoreHandle_t semaphoreHandle,
                                                      const Template_osalTimeMs_t timeoutMs)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquireWait(%p, %p, %u)",
                        (void *)osal, (void *)semaphoreHandle, (unsigned int)timeoutMs);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquireWait -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquireWait -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreAcquireWait == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquireWait -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreAcquireWait(osal, semaphoreHandle, timeoutMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreAcquireWait -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Release one count to a counting semaphore.
 * \param  osal                 OSAL instance pointer.
 * \param  semaphoreHandle      Counting semaphore handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreRelease(Template_osal_s *const osal,
                                                 const Template_osalSemaphoreHandle_t semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreRelease(%p, %p)",
                        (void *)osal, (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreRelease -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreRelease -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreRelease == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreRelease -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreRelease(osal, semaphoreHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreRelease -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Retrieve the current counting semaphore value.
 * \param  osal                 OSAL instance pointer.
 * \param  semaphoreHandle      Counting semaphore handle.
 * \param  semaphoreCount       Output pointer receiving the current semaphore count.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->semaphoreCountGet == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->semaphoreCountGet(osal, semaphoreHandle, semaphoreCount);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreCountGet -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Get a counting semaphore handle from a stable registry slot.
 * \param  osal                 OSAL instance pointer.
 * \param  semaphoreSlotInd     Zero-based semaphore registry slot index.
 * \param  semaphoreHandle      Output pointer receiving the current slot handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSemaphoreHandleGet(Template_osal_s *const osal,
                                                    const size_t semaphoreSlotInd,
                                                    Template_osalSemaphoreHandle_t *const semaphoreHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet(%p, %zu, %p)",
                        (void *)osal, semaphoreSlotInd, (void *)semaphoreHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (semaphoreHandle == NULL) ||
        (semaphoreSlotInd >= TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Retrieve the resource handle from the specified slot */
    *semaphoreHandle = osal->semaphoreObjHandle[semaphoreSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSemaphoreHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


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
                                             Template_osalThreadCfg_s threadCfg)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadCreate(%p, %p, {%p,%s,%zu,%p,%d})",
                        (void *)osal, (void *)threadHandle,
                        (void *)threadCfg.worker,
                        (threadCfg.name ? threadCfg.name : "(null)"),
                        threadCfg.stackSize, threadCfg.args, (int)threadCfg.prio);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
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

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to create the thread and get the status */
    const Template_osalErr_e retStatus =
        osal->vtable->threadCreate(osal, threadHandle, threadCfg);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadCreate -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Delete the thread.
 * \note The operation must be stopped before deleting the thread to avoid system damage.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread being deleted.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
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

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to delete the thread */
    const Template_osalErr_e retStatus = osal->vtable->threadDelete(osal, threadHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelete -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Suspend the thread.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to suspend.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if (osal->validFlag != true)
    {
        TEMPLATE_OSAL_TRACE("template_osalThreadSuspend -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if ((osal->vtable == NULL) ||
        (osal->vtable->threadSuspend == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadSuspend -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to suspend the thread */
    const Template_osalErr_e retStatus = osal->vtable->threadSuspend(osal, threadHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadSuspend -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Resume the thread.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to resume.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        TEMPLATE_OSAL_TRACE("template_osalThreadResume -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if (osal->vtable->threadResume == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadResume -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to resume the thread */
    const Template_osalErr_e retStatus = osal->vtable->threadResume(osal, threadHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadResume -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Delay the execution of the current thread.
 * \param osal     Pointer to OSAL instance.
 * \param delayMs  Delay duration in milliseconds.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        TEMPLATE_OSAL_TRACE("template_osalThreadDelay -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if (osal->vtable->threadDelay == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadDelay -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Delay the current thread */
    const Template_osalErr_e retStatus = osal->vtable->threadDelay(osal, delayMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadDelay -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Terminate the calling thread (does not return).
 * \param osal  Pointer to OSAL instance (must be valid).
 * \note  This function never returns control to the caller.
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
 * \param osal           Pointer to OSAL instance.
 * \param threadSlotInd  Index of thread slots.
 * \param threadHandle   Pointer where the thread handle will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalThreadHandleGet(Template_osal_s *const osal,
                                                const size_t threadSlotInd,
                                                Template_osalThreadHandle_t *const threadHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet(%p, %zu, %p)",
                        (void *)osal, threadSlotInd, (void *)threadHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL) ||
        (TEMPLATE_OSAL_THREAD_SLOTS_NUM <= threadSlotInd))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    /* Copy the thread handle from the specified slot */
    *threadHandle = osal->threadObjHandle[threadSlotInd].handle;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalThreadHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: return the summary status
}
// END THREAD

// BEGIN CRITICAL_SECTION
/*------------------------------- Critical section -------------------------------*/
/**
 * \brief  Enter a short OS critical section.
 * \param  osal  OSAL instance pointer.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->criticalSectionEnter == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->criticalSectionEnter(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalCriticalSectionEnter -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Exit a previously entered OS critical section.
 * \param  osal  OSAL instance pointer.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->criticalSectionExit == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus = osal->vtable->criticalSectionExit(osal);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalCriticalSectionExit -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


// END CRITICAL_SECTION

// BEGIN SOFTWARE_TIMER
/*-------------------------------- Software timers --------------------------------*/
/**
 * \brief  Create a one-shot or auto-reload software timer.
 * \param  osal         OSAL instance pointer.
 * \param  timerHandle  Output pointer receiving the created timer handle.
 * \param  timerCfg     Software timer configuration.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerCreate(Template_osal_s *const osal,
                                                     Template_osalSoftwareTimerHandle_t *const timerHandle,
                                                     Template_osalSoftwareTimerCfg_s timerCfg)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate(%p, %p, {%s, %p, %p, %d, %u})",
                        (void *)osal, (void *)timerHandle,
                        (timerCfg.name != NULL) ? timerCfg.name : "(null)",
                        timerCfg.timerParam,
                        (void *)(uintptr_t)timerCfg.timerExpiredCb,
                        (int)timerCfg.autoReload,
                        (unsigned int)timerCfg.periodMs);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL) ||
        (timerCfg.timerExpiredCb == NULL) ||
        (timerCfg.periodMs == 0u) ||
        (timerCfg.periodMs == TEMPLATE_OSAL_INFINITY_TOUT))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerCreate == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerCreate(osal, timerHandle, timerCfg);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerCreate -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Delete a software timer.
 * \param  osal                 OSAL instance pointer.
 * \param  timerHandle          Software timer handle.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerDelete == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerDelete(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerDelete -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Start a software timer using its configured period.
 * \param  osal                 OSAL instance pointer.
 * \param  timerHandle          Software timer handle.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerStart == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerStart(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStart -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Stop a software timer.
 * \param  osal                 OSAL instance pointer.
 * \param  timerHandle          Software timer handle.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerStop == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerStop(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerStop -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Reset a software timer and restart its configured period.
 * \param  osal                 OSAL instance pointer.
 * \param  timerHandle          Software timer handle.
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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->softwareTimerReset == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: backend method is unavailable
    }

    /* Delegate to underlying OS backend */
    const Template_osalErr_e retStatus =
        osal->vtable->softwareTimerReset(osal, timerHandle);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerReset -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Get a software timer handle from a stable registry slot.
 * \param  osal                 OSAL instance pointer.
 * \param  timerSlotInd         Zero-based software timer registry slot index.
 * \param  timerHandle          Output pointer receiving the current slot handle.
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalSoftwareTimerHandleGet(Template_osal_s *const osal,
                                                        const size_t timerSlotInd,
                                                        Template_osalSoftwareTimerHandle_t *const timerHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet(%p, %zu, %p)",
                        (void *)osal, timerSlotInd, (void *)timerHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (timerHandle == NULL) ||
        (timerSlotInd >= TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: not initialized
    }

    /* Retrieve the resource handle from the specified slot */
    *timerHandle = osal->softwareTimerObj[timerSlotInd].handle;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalSoftwareTimerHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


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

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalTimeMsGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;
    }

    if (osal->vtable->timeMsGet == NULL)
    {
        TEMPLATE_OSAL_TRACE("template_osalTimeMsGet -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Port (backend) specific error
    }

    /* Get the current OS time */
    const Template_osalErr_e retStatus = osal->vtable->timeMsGet(osal, osTimeMs);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalTimeMsGet -> %d", retStatus);

    return retStatus;  // Exit: return the summary status
}
// END TIME

// BEGIN MEMORY
/*------------------------------------- Memory --------------------------------*/

/**
 * \brief Allocate memory via the OSAL backend and register the pointer internally.
 * \param osal    Pointer to OSAL instance.
 * \param size    Allocation size in bytes.
 * \param outPtr  Pointer to the allocated memory (must not be NULL).
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalMalloc(Template_osal_s *const osal,
                                       const size_t size,
                                       void **const outPtr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMalloc(%p, %zu, %p)", (void *)osal, size, (void *)outPtr);

    /* Validate parameters */
    if ((osal == NULL) ||
        (outPtr == NULL) ||
        (size == 0u))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  /// Exit: Error: Not initialized
    }

    if (osal->vtable->memAlloc == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Port (backend) specific error
    }

    /* Delegate allocation (only now) */
    void *ptr                    = NULL;
    Template_osalErr_e retStatus = osal->vtable->memAlloc(osal, size, &ptr);
    if ((retStatus != TEMPLATE_OSAL_NO_ERR) ||
        (ptr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d",
                            (retStatus != TEMPLATE_OSAL_NO_ERR) ? retStatus :
                            TEMPLATE_OSAL_MEM_ALLOCATION_ERR);

        return (retStatus != TEMPLATE_OSAL_NO_ERR) ? retStatus
                                                        : TEMPLATE_OSAL_MEM_ALLOCATION_ERR;  // Exit: Error: backend failed or returned NULL
    }

    *outPtr = ptr;

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMalloc -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief Free memory via the OSAL backend and unregister the pointer internally.
 * \param osal  Pointer to OSAL instance.
 * \param ptr   Pointer to the memory block to free (must not be NULL and must be registered).
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalFree(Template_osal_s *const osal,
                                     void *const ptr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalFree(%p, %p)", (void *)osal, ptr);

    /* Validate parameters */
    if ((osal == NULL) ||
        (ptr == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalFree -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalFree -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if (osal->vtable->memFree == NULL)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalFree -> %d", TEMPLATE_OSAL_PORT_SPECIFIC_ERR);

        return TEMPLATE_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Port (backend) specific error
    }

    /* Delegate free to backend */
    const Template_osalErr_e retStatus = osal->vtable->memFree(osal, ptr);

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalFree -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief Get a memory handle of the given OSAL object.
 * \param osal          Pointer to OSAL instance.
 * \param memSlotInd    Index of memory slot.
 * \param memHandle     Pointer where the memory handle will be copied.
 * \return Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalMemHandleGet(Template_osal_s *const osal,
                                             const size_t memSlotInd,
                                             Template_osalMemHandle_t *const memHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalMemHandleGet(%p, %zu, %p)",
                        (void *)osal, memSlotInd, (void *)memHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (memHandle == NULL) ||
        (TEMPLATE_OSAL_MEM_SLOTS_NUM <= memSlotInd))
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMemHandleGet -> %d", TEMPLATE_OSAL_INVALID_ARGS_ERR);

        return TEMPLATE_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        TEMPLATE_OSAL_TRACE("template_osalMemHandleGet -> %d", TEMPLATE_OSAL_NOT_INIT_ERR);

        return TEMPLATE_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    *memHandle = osal->memObjHandle[memSlotInd];

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalMemHandleGet -> %d", TEMPLATE_OSAL_NO_ERR);

    return TEMPLATE_OSAL_NO_ERR;  // Exit: success
}
// END MEMORY

/*============================================================================[ PRIVATE FUNCTIONS ]============================================================================*/

/**
 * \brief  Reset OSAL objects such as queues, lock objects, threads and memory slots.
 * \param  osal  OSAL instance pointer.
 * \return None.
 */
static void template_osalResetObjects(Template_osal_s *const osal)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalResetObjects(%p)", (void *)osal);

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

    // BEGIN LOCK
    /* Reset lock object slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        osal->lockObjHandle[i] = NULL;
    }
    // END LOCK

    // BEGIN SEMAPHORE
    /* Reset counting semaphore slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_SEMAPHORE_SLOTS_NUM; ++i)
    {
        osal->semaphoreObjHandle[i] = NULL;
    }
    // END SEMAPHORE

    // BEGIN THREAD
    /* Reset thread slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        osal->threadObjHandle[i].cfg.worker    = NULL;
        osal->threadObjHandle[i].cfg.name      = NULL;
        osal->threadObjHandle[i].cfg.stackSize = 0u;
        osal->threadObjHandle[i].cfg.args      = NULL;
        osal->threadObjHandle[i].cfg.prio      = TEMPLATE_OSAL_THREAD_PRIORITY_LOW;
        osal->threadObjHandle[i].handle        = NULL;
    }
    // END THREAD

    // BEGIN SOFTWARE_TIMER
    /* Reset software timer slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_SOFTWARE_TIMER_SLOTS_NUM; ++i)
    {
        osal->softwareTimerObj[i].handle             = NULL;
        osal->softwareTimerObj[i].cfg.name           = NULL;
        osal->softwareTimerObj[i].cfg.timerParam     = NULL;
        osal->softwareTimerObj[i].cfg.timerExpiredCb = NULL;
        osal->softwareTimerObj[i].cfg.autoReload     = false;
        osal->softwareTimerObj[i].cfg.periodMs       = 0u;
    }
    // END SOFTWARE_TIMER

    // BEGIN MEMORY
    /* Reset memory registry slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        osal->memObjHandle[i] = NULL;
    }
    // END MEMORY

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalResetObjects -> ok");
}


// BEGIN QUEUE
/*-------------------------------- Registry: Queues -------------------------------*/

/**
 * \brief  Find a free queue slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegQueueFreeSlotFind(void * const osalPort)
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
            TEMPLATE_OSAL_TRACE("template_osalRegQueueFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegQueueFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find queue handle.
 * \param  osalPort     Derived OSAL pointer.
 * \param  queueHandle  Handle to search.
 * \return Queue ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegQueueHandleFind(void * const osalPort,
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
            TEMPLATE_OSAL_TRACE("template_osalRegQueueHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegQueueHandleFind -> %d", 0);

    return 0u;
}
// END QUEUE

// BEGIN STREAM_BUFFER
/*--------------------------- Registry: Stream buffers ---------------------------*/

/**
 * \brief  Find a free stream-buffer slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegStreamBufferFreeSlotFind(void * const osalPort)
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
            TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find a stream-buffer handle.
 * \param  osalPort           Derived OSAL pointer.
 * \param  streamBufferHandle Handle to search.
 * \return Stream-buffer ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegStreamBufferHandleFind(void * const osalPort,
                                                     const Template_osalStreamBufferHandle_t streamBufferHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferHandleFind(%p, %p)", osalPort, (void *)streamBufferHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(streamBufferHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_STREAM_BUFFER_SLOTS_NUM; ++i)
    {
        if (osal->streamBufferObjHandle[i] == streamBufferHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegStreamBufferHandleFind -> %d", 0);

    return 0u;
}


// END STREAM_BUFFER

// BEGIN LOCK
/*-------------------------------- Registry: Locks --------------------------------*/

/**
 * \brief  Find a free lock object slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegLockFreeSlotFind(void * const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegLockFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osal->lockObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegLockFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegLockFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find lock object handle.
 * \param  osalPort      Derived OSAL pointer.
 * \param  lockObjHandle Handle to search.
 * \return Lock ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegLockHandleFind(void * const osalPort,
                                             const Template_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegLockHandleFind(%p, %p)", osalPort, (void *)lockObjHandle);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(lockObjHandle != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osal->lockObjHandle[i] == lockObjHandle)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegLockHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegLockHandleFind -> %d", 0);

    return 0u;
}
// END LOCK

// BEGIN SEMAPHORE
/*------------------------- Registry: Counting semaphores -------------------------*/

/**
 * \brief  Find a free counting-semaphore slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegSemaphoreFreeSlotFind(void * const osalPort)
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
            TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find a counting-semaphore handle.
 * \param  osalPort        Derived OSAL pointer.
 * \param  semaphoreHandle Handle to search.
 * \return Semaphore ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegSemaphoreHandleFind(void * const osalPort,
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
            TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSemaphoreHandleFind -> %d", 0);

    return 0u;
}


// END SEMAPHORE

// BEGIN THREAD
/*------------------------------- Registry: Threads -------------------------------*/

/**
 * \brief  Find a free thread slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegThreadFreeSlotFind(void * const osalPort)
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
            TEMPLATE_OSAL_TRACE("template_osalRegThreadFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find thread handle.
 * \param  osalPort     Derived OSAL pointer.
 * \param  threadHandle Handle to search.
 * \return Thread ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegThreadHandleFind(void * const osalPort,
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
            TEMPLATE_OSAL_TRACE("template_osalRegThreadHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadHandleFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Clear a thread registry slot.
 * \param  osalPort   Derived OSAL pointer.
 * \param  threadIdx  Zero-based thread registry index.
 * \return None.
 */
static void template_osalRegThreadSlotClear(void * const osalPort,
                                            const size_t threadIdx)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadSlotClear(%p, %zu)", osalPort, threadIdx);

    /* Validate input args */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(threadIdx < TEMPLATE_OSAL_THREAD_SLOTS_NUM);

    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    /* Clear the thread registry slot */
    osal->threadObjHandle[threadIdx].cfg.worker    = NULL;
    osal->threadObjHandle[threadIdx].cfg.name      = NULL;
    osal->threadObjHandle[threadIdx].cfg.stackSize = 0u;
    osal->threadObjHandle[threadIdx].cfg.args      = NULL;
    osal->threadObjHandle[threadIdx].cfg.prio      = TEMPLATE_OSAL_THREAD_PRIORITY_LOW;
    osal->threadObjHandle[threadIdx].handle        = TEMPLATE_OSAL_OBJ_HANDLE_INVALID;

    /* Trace returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegThreadSlotClear -> ok");
}
// END THREAD

// BEGIN SOFTWARE_TIMER
/*--------------------------- Registry: Software timers ---------------------------*/

/**
 * \brief  Find a free software-timer slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegSoftwareTimerFreeSlotFind(void * const osalPort)
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
            TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find a software-timer handle.
 * \param  osalPort    Derived OSAL pointer.
 * \param  timerHandle Handle to search.
 * \return Software-timer ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegSoftwareTimerHandleFind(void * const osalPort,
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
            TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegSoftwareTimerHandleFind -> %d", 0);

    return 0u;
}


// END SOFTWARE_TIMER


// BEGIN MEMORY
/*-------------------------------- Registry: Memory --------------------------------*/

/**
 * \brief  Find a free memory slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t template_osalRegMemFreeSlotFind(void * const osalPort)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegMemFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osal->memObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegMemFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegMemFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find pointer in memory registry.
 * \param  osalPort  Derived OSAL pointer.
 * \param  ptr       Pointer to search.
 * \return Memory ID (index + 1) or 0 if not found.
 */
static size_t template_osalRegMemHandleFind(void * const osalPort,
                                            const void * const ptr)
{
    /* Trace input args */
    TEMPLATE_OSAL_TRACE("template_osalRegMemHandleFind(%p, %p)", osalPort, ptr);

    /* Must be validated by the caller */
    TEMPLATE_OSAL_ASSERT(osalPort != NULL);
    TEMPLATE_OSAL_ASSERT(ptr != NULL);
    Template_osal_s *const osal = (Template_osal_s *)osalPort;

    for (size_t i = 0; i < TEMPLATE_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osal->memObjHandle[i] == ptr)
        {
            /* Trace: returned value */
            TEMPLATE_OSAL_TRACE("template_osalRegMemHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    TEMPLATE_OSAL_TRACE("template_osalRegMemHandleFind -> %d", 0);

    return 0u;
}
// END MEMORY
