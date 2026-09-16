/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef TEMPLATE_OSAL_FREERTOS_H_
#define TEMPLATE_OSAL_FREERTOS_H_

#ifdef __cplusplus
    extern "C" {
#endif

/*================================================================[INCLUDE]=================================================*/

#include "template_osal.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* FreeRTOS core and native semaphore handle used by the backend. */
#include "FreeRTOS.h"
#include "semphr.h"
// BEGIN THREAD
#include "task.h"
// END THREAD

/*===========================================================[MACRO DEFINITIONS]============================================*/

// BEGIN THREAD
/**
 * \brief Default FreeRTOS priority assigned to TEMPLATE_OSAL_THREAD_PRIO_LOW.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_LOW
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_LOW         (tskIDLE_PRIORITY + 1)
#endif

/**
 * \brief Default FreeRTOS priority assigned to TEMPLATE_OSAL_THREAD_PRIO_NORMAL.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_NORMAL
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_NORMAL      (tskIDLE_PRIORITY + 2)
#endif

/**
 * \brief Default FreeRTOS priority assigned to TEMPLATE_OSAL_THREAD_PRIO_HIGH.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_HIGH
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_HIGH        (tskIDLE_PRIORITY + 3)
#endif

/**
 * \brief Default FreeRTOS priority assigned to TEMPLATE_OSAL_THREAD_PRIO_CRITICAL.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_CRITICAL
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_CRITICAL    (tskIDLE_PRIORITY + 4)
#endif
// END THREAD

/*========================================================[DATA TYPES DEFINITIONS]==========================================*/

/**
 * \struct  Template_osalFreertosParam_s
 * \brief   FreeRTOS-specific OSAL instance parameters.
 * \details Parameters are optional. Passing NULL to template_osalFreertosInit() selects the
 *          default FreeRTOS-port behavior. Individual parameter groups may also fall back to
 *          their defaults by keeping the corresponding hasParam flag false.
 */
typedef struct
{
    void *handle; /*!< Optional user-defined integration context. */

    // BEGIN THREAD
    struct
    {
        bool        hasParam;                                    /*!< true when a custom priority policy is supplied. */
        UBaseType_t policy[TEMPLATE_OSAL_THREAD_PRIO_MAX_COUNT]; /*!< OSAL priority to FreeRTOS priority mapping. */
    } prio;

#ifdef TEMPLATE_OSAL_FREERTOS_USE_SMP
    struct
    {
        bool        hasParam;                                           /*!< true when a custom core-affinity policy is supplied. */
        UBaseType_t coreAffinityMask[TEMPLATE_OSAL_THREAD_SLOTS_NUM];  /*!< Thread-slot to FreeRTOS core-affinity mapping. */
    } smp;
#endif
    // END THREAD

#ifdef TEMPLATE_OSAL_FREERTOS_USE_MPU
    struct
    {
        bool hasParam; /*!< Reserved for FreeRTOS MPU-specific instance parameters. */
    } mpu;
#endif
} Template_osalFreertosParam_s;

/**
 * \struct  Template_osalFreertos_s
 * \brief   Template FreeRTOS OSAL structure.
 * \details FreeRTOS-specific extension of the Template OSAL. The base object must remain the first field.
 *          resourceMutex protects internal resource registries and is never exposed as a client mutex.
 */
typedef struct
{
    Template_osal_s              base;          /*!< Base OSAL object; must remain first. */
    Template_osalFreertosParam_s param;         /*!< Normalized FreeRTOS-specific instance configuration. */
    SemaphoreHandle_t            resourceMutex; /*!< Backend-owned registry synchronization mutex. */
    bool                         validFlag;     /*!< Backend validation flag. */
} Template_osalFreertos_s;

/*===========================================================[PUBLIC INTERFACE]=============================================*/

/**
 * \brief Initialize the Template FreeRTOS OSAL instance.
 *
 * \param osalFreertos  Pointer to the FreeRTOS-specific OSAL instance.
 * \param name          Optional instance name; may be NULL.
 * \param parent        Optional parent object pointer; may be NULL.
 * \param param         Optional instance parameters. NULL selects the default port behavior.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalFreertosInit(Template_osalFreertos_s *const osalFreertos,
                                             const char *const name,
                                             void *const parent,
                                             const Template_osalFreertosParam_s *const param);

/**
 * \brief Deinitialize the Template FreeRTOS OSAL instance.
 *
 * \param osalFreertos  Pointer to the FreeRTOS-specific OSAL instance.
 *
 * \return Template_osalErr_e, zero value = success, otherwise an error has occurred.
 */
Template_osalErr_e template_osalFreertosDeinit(Template_osalFreertos_s *const osalFreertos);

#ifdef __cplusplus
    }
#endif

#endif /* TEMPLATE_OSAL_FREERTOS_H_ */
