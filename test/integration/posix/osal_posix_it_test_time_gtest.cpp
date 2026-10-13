/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_posix_it_test_fixture.hpp"

#include <gtest/gtest.h>

/**
 * \brief Verify native POSIX monotonic time advances across a finite thread delay.
 *
 * \details Reads OSAL time, performs a short backend delay and reads time again.
 *          The comparison uses elapsed ordering rather than exact wall-clock equality.
 *
 * Expected result:
 * - both time queries and the delay complete successfully;
 * - the elapsed OSAL time is at least the requested delay interval.
 */
TEST_F(OsalPosixItTest, Time_DelayAdvancesMonotonicClock)
{
    KiwiTest_osalTimeMs_t before = 0u;
    KiwiTest_osalTimeMs_t after = 0u;
    constexpr KiwiTest_osalTimeMs_t delayMs = 20u;

    /* Capture the initial backend time */
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &before), KIWI_TEST_OSAL_NO_ERR);

    /* Delay through the real backend */
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), delayMs), KIWI_TEST_OSAL_NO_ERR);

    /* Verify monotonic elapsed time */
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &after), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_GE(static_cast<KiwiTest_osalTimeMs_t>(after - before), delayMs);
}


