/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_unit_test_fixture.hpp"

#include <gtest/gtest.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace
{
struct SemaphoreHandleDummy
{
    int value;
};
}

/**
 * \brief Verify counting-semaphore wrappers reject invalid public arguments.
 *
 * \details Exercises invalid create bounds, null handles, invalid output storage
 *          and out-of-range registry lookup without backend dispatch.
 *
 * Expected result:
 * - every invalid request returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST_F(OsalUnitTest, Semaphore_InvalidArgs)
{
    KiwiTest_osalSemaphoreHandle_t out  = nullptr;
    KiwiTest_osalSemaphoreCount_t count = 0u;

    EXPECT_EQ(kiwiTest_osalSemaphoreCreate(nullptr, 2u, 0u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 0u, 0u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 1u, 2u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 2u, 0u, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphoreDelete(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphoreWait(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphorePend(osal(), nullptr, 10u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphorePost(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), nullptr, &count), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphoreHandleGet(osal(), KIWI_TEST_OSAL_SEMAPHORE_SLOTS_NUM, &out),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify counting-semaphore create forwards count configuration and output storage.
 *
 * \details Creates a mock handle through the generic API with explicit maximum
 *          and initial counts and forces the backend to return a create error.
 *
 * Expected result:
 * - maxCount and initialCount reach the backend unchanged;
 * - the output handle written by the backend is preserved;
 * - the backend status is propagated unchanged.
 */
TEST_F(OsalUnitTest, Semaphore_Create_DispatchesConfigurationAndStatus)
{
    SemaphoreHandleDummy dummy         = {};
    auto handle                        = static_cast<KiwiTest_osalSemaphoreHandle_t>(&dummy);
    KiwiTest_osalSemaphoreHandle_t out = nullptr;

    EXPECT_CALL(mock_, semaphoreCreate(osal(), 5u, 2u, _))
    .WillOnce(DoAll(SetArgPointee<3>(handle), Return(KIWI_TEST_OSAL_SEMAPHORE_CREATE_ERR)));

    EXPECT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 5u, 2u, &out),
              KIWI_TEST_OSAL_SEMAPHORE_CREATE_ERR);
    EXPECT_EQ(out, handle);
}


/**
 * \brief Verify counting-semaphore operations dispatch to their matching backend methods.
 *
 * \details Exercises indefinite wait, timed pend, post, count query and delete
 *          through one stable mock handle.
 *
 * Expected result:
 * - each generic wrapper forwards the original handle and timeout;
 * - the count output and backend statuses are preserved.
 */
TEST_F(OsalUnitTest, Semaphore_Operations_DispatchAndPropagateBackendStatus)
{
    SemaphoreHandleDummy dummy          = {};
    auto handle                         = static_cast<KiwiTest_osalSemaphoreHandle_t>(&dummy);
    KiwiTest_osalSemaphoreCount_t count = 0u;

    EXPECT_CALL(mock_, semaphoreWait(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_SEMAPHORE_WAIT_ERR));
    EXPECT_CALL(mock_, semaphorePend(osal(), handle, 75u)).WillOnce(Return(KIWI_TEST_OSAL_SEMAPHORE_WAIT_ERR));
    EXPECT_CALL(mock_, semaphorePost(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_SEMAPHORE_POST_ERR));
    EXPECT_CALL(mock_, semaphoreCountGet(osal(), handle, _))
    .WillOnce(DoAll(SetArgPointee<2>(3u), Return(KIWI_TEST_OSAL_NO_ERR)));
    EXPECT_CALL(mock_, semaphoreDelete(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    EXPECT_EQ(kiwiTest_osalSemaphoreWait(osal(), handle), KIWI_TEST_OSAL_SEMAPHORE_WAIT_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphorePend(osal(), handle, 75u), KIWI_TEST_OSAL_SEMAPHORE_WAIT_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphorePost(osal(), handle), KIWI_TEST_OSAL_SEMAPHORE_POST_ERR);
    ASSERT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), handle, &count), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(kiwiTest_osalSemaphoreDelete(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify counting-semaphore handle lookup reads the generic registry.
 *
 * \details Places a stable handle in a known semaphore registry slot and retrieves
 *          it through the public generic lookup API.
 *
 * Expected result:
 * - lookup returns the exact handle stored in the requested slot.
 */
TEST_F(OsalUnitTest, Semaphore_HandleGet_ReturnsRegisteredHandle)
{
    SemaphoreHandleDummy dummy         = {};
    auto handle                        = static_cast<KiwiTest_osalSemaphoreHandle_t>(&dummy);
    KiwiTest_osalSemaphoreHandle_t out = nullptr;

    osal()->semaphoreObjHandle[0] = handle;

    ASSERT_EQ(kiwiTest_osalSemaphoreHandleGet(osal(), 0u, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, handle);
}


/**
 * \brief Verify semaphore API rejects a valid request on an uninitialized OSAL instance.
 *
 * \details Uses otherwise valid public arguments with a zero-initialized OSAL
 *          object so generic instance-state validation is exercised directly.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_NOT_INIT_ERR;
 * - no backend dispatch is required.
 */
TEST_F(OsalUnitTest, Semaphore_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized         = {};
    KiwiTest_osalSemaphoreHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalSemaphoreCreate(&uninitialized, 2u, 0u, &handle), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify semaphore API reports an unavailable backend operation.
 *
 * \details Removes the representative operation from an otherwise valid backend
 *          vtable and performs a valid generic request.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
 * - the generic layer does not call through a null backend method.
 */
TEST_F(OsalUnitTest, Semaphore_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;

    vtable.semaphoreCreate = nullptr;
    osal()->vtable         = &vtable;
    KiwiTest_osalSemaphoreHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 2u, 0u, &handle), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
