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
struct QueueHandleDummy
{
    int value;
};
}

/**
 * \brief Verify queue wrappers reject invalid public arguments.
 *
 * \details Exercises representative invalid combinations for create, data
 *          transfer, reset, delete and handle lookup without invoking the backend.
 *
 * Expected result:
 * - every invalid request returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST_F(OsalUnitTest, Queue_InvalidArgs)
{
    QueueHandleDummy dummy = {};
    auto handle = static_cast<KiwiTest_osalQueueHandle_t>(&dummy);
    int item = 1;
    KiwiTest_osalQueueHandle_t out = nullptr;

    EXPECT_EQ(kiwiTest_osalQueueCreate(nullptr, sizeof(item), 2u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(item), 2u, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueDelete(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemPut(osal(), nullptr, &item), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemPut(osal(), handle, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemPost(osal(), handle, nullptr, 10u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemGet(osal(), handle, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemWait(osal(), handle, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemPend(osal(), handle, nullptr, 10u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueReset(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueHandleGet(osal(), KIWI_TEST_OSAL_QUEUE_SLOTS_NUM, &out),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalQueueHandleGet(osal(), 0u, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify queue creation dispatches all arguments and propagates backend status.
 *
 * \details Configures the mock backend to publish a stable queue handle and
 *          verifies the generic wrapper forwards item size, depth and output storage.
 *
 * Expected result:
 * - the backend receives the original arguments exactly once;
 * - the created handle is returned unchanged;
 * - the backend status is propagated by the wrapper.
 */
TEST_F(OsalUnitTest, Queue_Create_DispatchesAndPropagatesStatus)
{
    QueueHandleDummy dummy = {};
    auto expectedHandle = static_cast<KiwiTest_osalQueueHandle_t>(&dummy);
    KiwiTest_osalQueueHandle_t actualHandle = nullptr;

    EXPECT_CALL(mock_, queueCreate(osal(), sizeof(uint32_t), 4u, _))
        .WillOnce(DoAll(SetArgPointee<3>(expectedHandle), Return(KIWI_TEST_OSAL_QUEUE_CREATE_ERR)));

    EXPECT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 4u, &actualHandle),
              KIWI_TEST_OSAL_QUEUE_CREATE_ERR);
    EXPECT_EQ(actualHandle, expectedHandle);
}


/**
 * \brief Verify queue transfer operations dispatch to their matching backend methods.
 *
 * \details Invokes non-blocking, indefinite and timed queue operations using one
 *          stable handle and checks that the generic wrappers preserve pointers and timeouts.
 *
 * Expected result:
 * - each wrapper calls only its matching backend method;
 * - backend error values are returned unchanged.
 */
TEST_F(OsalUnitTest, Queue_Operations_DispatchAndPropagateBackendStatus)
{
    QueueHandleDummy dummy = {};
    auto handle = static_cast<KiwiTest_osalQueueHandle_t>(&dummy);
    uint32_t item = 0x12345678u;
    uint32_t out = 0u;

    EXPECT_CALL(mock_, queueItemPut(osal(), handle, &item)).WillOnce(Return(KIWI_TEST_OSAL_QUEUE_IS_FULL_ERR));
    EXPECT_CALL(mock_, queueItemPost(osal(), handle, &item, 25u)).WillOnce(Return(KIWI_TEST_OSAL_QUEUE_OVERFLOW_ERR));
    EXPECT_CALL(mock_, queueItemGet(osal(), handle, &out)).WillOnce(Return(KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR));
    EXPECT_CALL(mock_, queueItemWait(osal(), handle, &out)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));
    EXPECT_CALL(mock_, queueItemPend(osal(), handle, &out, 50u)).WillOnce(Return(KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR));
    EXPECT_CALL(mock_, queueReset(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));
    EXPECT_CALL(mock_, queueDelete(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    EXPECT_EQ(kiwiTest_osalQueueItemPut(osal(), handle, &item), KIWI_TEST_OSAL_QUEUE_IS_FULL_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemPost(osal(), handle, &item, 25u), KIWI_TEST_OSAL_QUEUE_OVERFLOW_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemGet(osal(), handle, &out), KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemWait(osal(), handle, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemPend(osal(), handle, &out, 50u), KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR);
    EXPECT_EQ(kiwiTest_osalQueueReset(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalQueueDelete(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify queue handle lookup reads the generic registry by slot index.
 *
 * \details Places a handle into a known generic registry slot and retrieves it
 *          through the public handle lookup API without backend dispatch.
 *
 * Expected result:
 * - the exact registered handle is returned for the requested slot.
 */
TEST_F(OsalUnitTest, Queue_HandleGet_ReturnsRegisteredHandle)
{
    QueueHandleDummy dummy = {};
    auto handle = static_cast<KiwiTest_osalQueueHandle_t>(&dummy);
    KiwiTest_osalQueueHandle_t actual = nullptr;

    osal()->queueObjHandle[1] = handle;

    ASSERT_EQ(kiwiTest_osalQueueHandleGet(osal(), 1u, &actual), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(actual, handle);
}


/**
 * \brief Verify queue API rejects a valid request on an uninitialized OSAL instance.
 *
 * \details Uses otherwise valid public arguments with a zero-initialized OSAL
 *          object so generic instance-state validation is exercised directly.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_NOT_INIT_ERR;
 * - no backend dispatch is required.
 */
TEST_F(OsalUnitTest, Queue_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized = {};
    KiwiTest_osalQueueHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalQueueCreate(&uninitialized, sizeof(uint32_t), 1u, &handle), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify queue API reports an unavailable backend operation.
 *
 * \details Removes the representative operation from an otherwise valid backend
 *          vtable and performs a valid generic request.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
 * - the generic layer does not call through a null backend method.
 */
TEST_F(OsalUnitTest, Queue_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;
    vtable.queueCreate = nullptr;
    osal()->vtable = &vtable;
    KiwiTest_osalQueueHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 1u, &handle), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
