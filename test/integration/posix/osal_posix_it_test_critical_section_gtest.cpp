/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_posix_it_test_fixture.hpp"

#include <gtest/gtest.h>

/**
 * \brief Verify POSIX reports the deprecated system-level critical section as unsupported.
 *
 * \details Calls Enter and Exit on a fully initialized POSIX OSAL instance. The
 *          backend intentionally does not substitute a component mutex for system-level
 *          interrupt/scheduler masking semantics.
 *
 * Expected result:
 * - Enter returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
 * - Exit returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR.
 */
TEST_F(OsalPosixItTest, CriticalSection_SystemLevelPrimitive_IsRuntimeUnsupported)
{
    EXPECT_EQ(kiwiTest_osalCriticalSectionEnter(osal()), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);
    EXPECT_EQ(kiwiTest_osalCriticalSectionExit(osal()), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);
}
