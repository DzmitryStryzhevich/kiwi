/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_unit_test_fixture.hpp"

#include <gtest/gtest.h>

using ::testing::Return;

/**
 * \brief Verify deprecated critical-section wrappers validate the OSAL pointer.
 *
 * \details Calls both system-level critical-section operations with a null instance.
 *
 * Expected result:
 * - enter and exit return KIWI_TEST_OSAL_INVALID_ARGS_ERR without backend dispatch.
 */
TEST_F(OsalUnitTest, CriticalSection_InvalidArgs)
{
    EXPECT_EQ(kiwiTest_osalCriticalSectionEnter(nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalCriticalSectionExit(nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify deprecated critical-section wrappers preserve backend semantics.
 *
 * \details Forces different backend statuses for Enter and Exit to ensure the
 *          generic layer does not reinterpret this backend-specific system primitive.
 *
 * Expected result:
 * - Enter and Exit dispatch independently;
 * - backend status values are propagated unchanged.
 */
TEST_F(OsalUnitTest, CriticalSection_DispatchesAndPropagatesBackendStatus)
{
    EXPECT_CALL(mock_, criticalSectionEnter(osal())).WillOnce(Return(KIWI_TEST_OSAL_PORT_SPECIFIC_ERR));
    EXPECT_CALL(mock_, criticalSectionExit(osal())).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    EXPECT_EQ(kiwiTest_osalCriticalSectionEnter(osal()), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);
    EXPECT_EQ(kiwiTest_osalCriticalSectionExit(osal()), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify critical-section API rejects an uninitialized OSAL instance.
 *
 * \details Calls CriticalSectionEnter with a zero-initialized OSAL object after
 *          passing public pointer validation.
 *
 * Expected result:
 * - CriticalSectionEnter returns KIWI_TEST_OSAL_NOT_INIT_ERR.
 */
TEST_F(OsalUnitTest, CriticalSection_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized = {};
    EXPECT_EQ(kiwiTest_osalCriticalSectionEnter(&uninitialized), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify critical-section API reports an unavailable backend operation.
 *
 * \details Removes CriticalSectionEnter from an otherwise valid backend vtable
 *          and invokes the generic operation.
 *
 * Expected result:
 * - CriticalSectionEnter returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR.
 */
TEST_F(OsalUnitTest, CriticalSection_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;
    vtable.criticalSectionEnter = nullptr;
    osal()->vtable = &vtable;

    EXPECT_EQ(kiwiTest_osalCriticalSectionEnter(osal()), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
