/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef TEMPLATE_OSAL_POSIX_H_
#define TEMPLATE_OSAL_POSIX_H_

#ifdef __cplusplus
    extern "C" {
#endif

/*================================================================[INCLUDE]=================================================*/

#include "template_osal.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*===========================================================[MACRO DEFINITIONS]============================================*/

/* None */

/*========================================================[DATA TYPES DEFINITIONS]==========================================*/

/**
 * \struct  Template_osalPosixParam_s
 * \brief   POSIX-specific OSAL instance parameters.
 * \details Parameters are optional. Passing NULL to template_osalPosixInit() selects the
 *          default POSIX-port behavior. The opaque handle is reserved for parent-project
 *          integration and is not interpreted by the backend.
 */
typedef struct
{
    void *handle; /*!< Optional user-defined integration context. */
} Template_osalPosixParam_s;

/**
 * \struct  Template_osalPosix_s
 * \brief   Template POSIX OSAL structure.
 * \details POSIX-specific extension of the Template OSAL. The base object must remain the first field.
 *          resourceMutex protects internal resource registries and is never exposed as a client mutex.
 */
typedef struct
{
    Template_osal_s           base;           /*!< Base OSAL object; must remain first. */
    Template_osalPosixParam_s param;          /*!< Normalized POSIX-specific instance configuration. */
    pthread_mutex_t           resourceMutex;  /*!< Backend-owned registry synchronization mutex. */
    bool                      validFlag;      /*!< Backend validation flag. */
} Template_osalPosix_s;

/*===========================================================[PUBLIC INTERFACE]=============================================*/

/**
 * \brief Initialize the Template POSIX OSAL instance.
 *
 * \details Initializes the generic OSAL base object, creates the backend-owned
 *          resource mutex and binds the POSIX vtable. Passing NULL as param
 *          selects the default POSIX-port behavior.
 *
 * \param osalPosix  Pointer to the POSIX-specific OSAL instance.
 * \param name       Optional instance name; may be NULL.
 * \param parent     Optional parent object pointer; may be NULL.
 * \param param      Optional POSIX instance parameters; NULL selects the default behavior.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
Template_osalErr_e template_osalPosixInit(Template_osalPosix_s *const osalPosix,
                                          const char *const name,
                                          void *const parent,
                                          const Template_osalPosixParam_s *const param);

/**
 * \brief Deinitialize the Template POSIX OSAL instance.
 *
 * \details Releases registered resources on a best-effort basis, destroys the
 *          backend-owned resource mutex and deinitializes the generic OSAL base object.
 *
 * \param osalPosix  Pointer to the POSIX-specific OSAL instance.
 *
 * \return Template_osalErr_e, zero value means success, otherwise an error
 *         has occurred.
 */
Template_osalErr_e template_osalPosixDeinit(Template_osalPosix_s *const osalPosix);

#ifdef __cplusplus
    }
#endif

#endif /* TEMPLATE_OSAL_POSIX_H_ */
