/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_unit_test_fixture.hpp"

#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Return;

/**
 * \brief Verify time query validates storage and propagates backend output.
 *
 * \details Checks the null-output path and then configures the backend to publish
 *          a deterministic millisecond value through the same caller-owned storage.
 *
 * Expected result:
 * - invalid output storage is rejected by the generic layer;
 * - valid requests return the backend timestamp and status unchanged.
 */
TEST_F(OsalUnitTest, TimeMsGet_ValidatesAndDispatches)
{
    KiwiTest_osalTimeMs_t timeMs = 0u;

    EXPECT_EQ(kiwiTest_osalTimeMsGet(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    EXPECT_CALL(mock_, timeMsGet(osal(), &timeMs))
    .WillOnce([&](void *, KiwiTest_osalTimeMs_t *out)
    {
        *out = 123456u;

        return KIWI_TEST_OSAL_NO_ERR;
    });

    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &timeMs), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(timeMs, 123456u);
}


/**
 * \brief Verify time API rejects a valid request on an uninitialized instance.
 *
 * \details Uses a zero-initialized OSAL object with otherwise valid TimeMsGet
 *          output storage.
 *
 * Expected result:
 * - TimeMsGet returns KIWI_TEST_OSAL_NOT_INIT_ERR.
 */
TEST_F(OsalUnitTest, Time_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized = {};
    KiwiTest_osalTimeMs_t timeMs  = 0u;

    EXPECT_EQ(kiwiTest_osalTimeMsGet(&uninitialized, &timeMs), KIWI_TEST_OSAL_NOT_INIT_ERR);
}

/**
 * \brief Verify time API reports an unavailable backend operation.
 *
 * \details Removes TimeMsGet from an otherwise valid backend vtable and performs
 *          a valid generic time request.
 *
 * Expected result:
 * - TimeMsGet returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR.
 */
TEST_F(OsalUnitTest, Time_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;
    KiwiTest_osalTimeMs_t timeMs = 0u;

    vtable.timeMsGet = nullptr;
    osal()->vtable   = &vtable;

    EXPECT_EQ(kiwiTest_osalTimeMsGet(osal(), &timeMs), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
