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

/* FreeRTOS core (tskIDLE_PRIORITY, etc.) */
#include "FreeRTOS.h"

/*===========================================================[MACRO DEFINITIONS]============================================*/

/**
 * \brief Template FreeRTOS-specific infinity timeout value.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_INFINITY_TIMEOUT
    #define TEMPLATE_OSAL_FREERTOS_INFINITY_TIMEOUT    UINT32_MAX
#endif

// BEGIN THREAD
/**
 * \brief Template FreeRTOS-specific priority levels.
 */
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_LOW
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_LOW       (tskIDLE_PRIORITY + 1)
#endif
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_MIDDLE
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_MIDDLE    (tskIDLE_PRIORITY + 2)
#endif
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_HIGH
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_HIGH      (tskIDLE_PRIORITY + 3)
#endif
#ifndef TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_ULTRA
    #define TEMPLATE_OSAL_FREERTOS_THREAD_PRIO_ULTRA     (tskIDLE_PRIORITY + 4)
#endif
// END THREAD

/*========================================================[DATA TYPES DEFINITIONS]==========================================*/

/**
 * \brief   FreeRTOS parameters structure.
 * \details Optional user-defined parameters for initializing the FreeRTOS-specific OSAL instance.
 */
typedef struct
{
    void *handle;   /*!< Optional user-defined handle (integration/extension). */
} Template_osalFreertosParam_s;

/**
 * \struct  Template_osalFreertos_s
 * \brief   Template FreeRTOS OSAL structure.
 * \details FreeRTOS-specific extension of the Template OSAL. The base must be the first field.
 */
typedef struct
{
    Template_osal_s              base;       /*!< Base OSAL object.                        */
    Template_osalFreertosParam_s param;      /*!< FreeRTOS-specific configuration (opt.).  */
    bool                         validFlag;     /*!< Validation flag.                         */
} Template_osalFreertos_s;

/*===========================================================[PUBLIC INTERFACE]=============================================*/

/**
 * \brief   Initialize the Template FreeRTOS OSAL instance.
 * \param   osalFreertos  Pointer to the FreeRTOS-specific OSAL instance (must not be NULL).
 * \param   name          Optional name string (may be NULL).
 * \param   parent        Optional parent object pointer (may be NULL).
 * \param   param         Optional FreeRTOS parameter structure (may be NULL).
 * \return  Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalFreertosInit(Template_osalFreertos_s *const osalFreertos,
                                             const char *name,
                                             void *const parent,
                                             const Template_osalFreertosParam_s *const param);

/**
 * \brief   Deinitialize the Template FreeRTOS OSAL instance.
 * \param   osalFreertos  Pointer to the FreeRTOS-specific OSAL instance.
 * \return  Template_osalErr_e error code, non-zero indicates error.
 */
Template_osalErr_e template_osalFreertosDeinit(Template_osalFreertos_s *const osalFreertos);

#ifdef __cplusplus
    }
#endif

#endif /* TEMPLATE_OSAL_FREERTOS_H_ */
