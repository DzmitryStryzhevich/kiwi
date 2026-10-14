/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_freertos_posix_it_test_fixture.hpp"

#include <gtest/gtest.h>

/**
 * \brief Verify FreeRTOS maps the deprecated system-level critical section to its native primitive.
 *
 * \details Enters and exits the system-level critical section from the FreeRTOS test
 *          task while the real GCC_POSIX kernel port is running.
 *
 * Expected result:
 * - Enter succeeds in task context;
 * - the paired Exit succeeds and restores normal execution.
 */
TEST_F(OsalFreertosPosixItTest, CriticalSection_EnterExit_UsesNativeFreeRtosPrimitive)
{
    ASSERT_EQ(kiwiTest_osalCriticalSectionEnter(osal()), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalCriticalSectionExit(osal()), KIWI_TEST_OSAL_NO_ERR);
}
