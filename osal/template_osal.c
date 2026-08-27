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
// END THREAD

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
 * \details Provides unified helpers for slot search and handle lookup for queues, locks, threads, and memory.
 *          IDs are 1-based (index + 1). Zero value indicates "not found"/"no free slot".
 */
static const Template_osalPtable_s template_osalPtable =
{
    // BEGIN QUEUE
    /*------------------------------- Queues --------------------------------*/
    .queueFreeSlotFind = template_osalRegQueueFreeSlotFind,
    .queueHandleFind   = template_osalRegQueueHandleFind,
    // END QUEUE

    // BEGIN LOCK
    /*-------------------------------- Locks --------------------------------*/
    .lockObjFreeSlotFind = template_osalRegLockFreeSlotFind,
    .lockObjHandleFind   = template_osalRegLockHandleFind,
    // END LOCK

    // BEGIN THREAD
    /*------------------------------- Threads -------------------------------*/
    .threadFreeSlotFind = template_osalRegThreadFreeSlotFind,
    .threadHandleFind   = template_osalRegThreadHandleFind,
    // END THREAD

    // BEGIN MEMORY
    /*-------------------------------- Memory -------------------------------*/
    .memFreeSlotFind = template_osalRegMemFreeSlotFind,
    .memHandleFind   = template_osalRegMemHandleFind
    // END MEMORY
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

    // BEGIN QUEUE
    /* Reset queue slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        osal->queueObjHandle[i] = NULL;
    }
    // END QUEUE

    // BEGIN LOCK
    /* Reset lock object slots */
    for (size_t i = 0; i < TEMPLATE_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        osal->lockObjHandle[i] = NULL;
    }
    // END LOCK

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
// END THREAD


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
