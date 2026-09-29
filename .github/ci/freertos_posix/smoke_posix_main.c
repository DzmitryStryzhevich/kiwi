/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "ci_osal_osal_posix.h"

int main(void)
{
    CiOsal_osalPosix_s osal = {0};

    if (ciOsal_osalPosixInit(&osal, "ci", NULL, NULL) != CI_OSAL_OSAL_NO_ERR)
    {
        return 1;
    }

    if (ciOsal_osalThreadYield(&osal.base) != CI_OSAL_OSAL_NO_ERR)
    {
        return 2;
    }

    if (ciOsal_osalCriticalSectionEnter(&osal.base) != CI_OSAL_OSAL_PORT_SPECIFIC_ERR)
    {
        return 3;
    }

    if (ciOsal_osalPosixDeinit(&osal) != CI_OSAL_OSAL_NO_ERR)
    {
        return 4;
    }

    return 0;
}
