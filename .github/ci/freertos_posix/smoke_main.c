/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "ci_osal_osal_freertos.h"

int main(void)
{
    CiOsal_osalFreertos_s osal = {0};

    /*
     * This executable is linked, not executed by CI. Referencing the public
     * initialization function forces the complete generated FreeRTOS object
     * into the final link so unresolved native FreeRTOS dependencies are caught.
     */
    return (int)ciOsal_osalFreertosInit(&osal, "ci", NULL, NULL);
}
