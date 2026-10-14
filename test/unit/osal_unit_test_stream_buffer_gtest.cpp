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
struct StreamBufferHandleDummy
{
    int value;
};
}

/**
 * \brief Verify stream-buffer wrappers reject invalid public arguments.
 *
 * \details Exercises invalid capacity/trigger combinations, null data/output
 *          pointers, zero-length transfers and invalid registry slot access.
 *
 * Expected result:
 * - every invalid request returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST_F(OsalUnitTest, StreamBuffer_InvalidArgs)
{
    StreamBufferHandleDummy dummy         = {};
    auto handle                           = static_cast<KiwiTest_osalStreamBufferHandle_t>(&dummy);
    KiwiTest_osalStreamBufferHandle_t out = nullptr;
    uint8_t data[4]                       = {};
    size_t count                          = 0u;

    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(nullptr, 8u, 1u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 0u, 1u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 0u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 9u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 1u, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferDelete(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferPut(osal(), handle, nullptr, sizeof(data), &count),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferPut(osal(), handle, data, 0u, &count), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferPut(osal(), handle, data, sizeof(data), nullptr),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferPost(osal(), handle, data, 0u, 10u, &count), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferGet(osal(), handle, nullptr, sizeof(data), &count),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferWait(osal(), handle, data, 0u, &count), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferPend(osal(), handle, data, sizeof(data), 10u, nullptr),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferReset(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferHandleGet(osal(),
                                                 KIWI_TEST_OSAL_STREAM_BUFFER_SLOTS_NUM,
                                                 &out),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify stream-buffer creation dispatches capacity and trigger configuration.
 *
 * \details Creates a mock stream buffer with explicit capacity and trigger level
 *          and forces the backend to return a create failure after publishing a handle.
 *
 * Expected result:
 * - capacity, trigger level and output storage reach the backend unchanged;
 * - the backend-written handle and status are propagated unchanged.
 */
TEST_F(OsalUnitTest, StreamBuffer_Create_DispatchesConfigurationAndStatus)
{
    StreamBufferHandleDummy dummy         = {};
    auto handle                           = static_cast<KiwiTest_osalStreamBufferHandle_t>(&dummy);
    KiwiTest_osalStreamBufferHandle_t out = nullptr;

    EXPECT_CALL(mock_, streamBufferCreate(osal(), 64u, 8u, _))
    .WillOnce(DoAll(SetArgPointee<3>(handle), Return(KIWI_TEST_OSAL_STREAM_BUFFER_CREATE_ERR)));

    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 64u, 8u, &out),
              KIWI_TEST_OSAL_STREAM_BUFFER_CREATE_ERR);
    EXPECT_EQ(out, handle);
}


/**
 * \brief Verify stream-buffer write wrappers preserve transfer parameters.
 *
 * \details Exercises non-blocking Put and timed Post with the same source data
 *          while the mock backend reports partial progress and distinct statuses.
 *
 * Expected result:
 * - data pointer, requested length and timeout are forwarded unchanged;
 * - bytesPut and backend statuses are propagated unchanged.
 */
TEST_F(OsalUnitTest, StreamBuffer_WriteOperations_DispatchCompleteTransferContract)
{
    StreamBufferHandleDummy dummy = {};
    auto handle                   = static_cast<KiwiTest_osalStreamBufferHandle_t>(&dummy);
    const uint8_t data[6]         = {1u, 2u, 3u, 4u, 5u, 6u};
    size_t bytesPut               = 0u;

    EXPECT_CALL(mock_, streamBufferPut(osal(), handle, data, sizeof(data), _))
    .WillOnce(DoAll(SetArgPointee<4>(3u), Return(KIWI_TEST_OSAL_NO_ERR)));
    EXPECT_CALL(mock_, streamBufferPost(osal(), handle, data, sizeof(data), 40u, _))
    .WillOnce(DoAll(SetArgPointee<5>(2u), Return(KIWI_TEST_OSAL_STREAM_BUFFER_IS_FULL_ERR)));

    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), handle, data, sizeof(data), &bytesPut),
              KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(bytesPut, 3u);

    bytesPut = 0u;
    EXPECT_EQ(kiwiTest_osalStreamBufferPost(osal(), handle, data, sizeof(data), 40u, &bytesPut),
              KIWI_TEST_OSAL_STREAM_BUFFER_IS_FULL_ERR);
    EXPECT_EQ(bytesPut, 2u);
}


/**
 * \brief Verify stream-buffer read wrappers preserve transfer and timeout parameters.
 *
 * \details Exercises non-blocking Get, indefinite Wait and timed Pend through one
 *          handle while the mock backend reports different byte counts and statuses.
 *
 * Expected result:
 * - requested destination size and timeout are forwarded unchanged;
 * - bytesGet and backend statuses are propagated unchanged.
 */
TEST_F(OsalUnitTest, StreamBuffer_ReadOperations_DispatchCompleteTransferContract)
{
    StreamBufferHandleDummy dummy = {};
    auto handle                   = static_cast<KiwiTest_osalStreamBufferHandle_t>(&dummy);
    uint8_t data[8]               = {};
    size_t bytesGet               = 0u;

    EXPECT_CALL(mock_, streamBufferGet(osal(), handle, data, sizeof(data), _))
    .WillOnce(DoAll(SetArgPointee<4>(2u), Return(KIWI_TEST_OSAL_NO_ERR)));
    EXPECT_CALL(mock_, streamBufferWait(osal(), handle, data, sizeof(data), _))
    .WillOnce(DoAll(SetArgPointee<4>(4u), Return(KIWI_TEST_OSAL_NO_ERR)));
    EXPECT_CALL(mock_, streamBufferPend(osal(), handle, data, sizeof(data), 55u, _))
    .WillOnce(DoAll(SetArgPointee<5>(0u), Return(KIWI_TEST_OSAL_STREAM_BUFFER_IS_EMPTY_ERR)));

    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(), handle, data, sizeof(data), &bytesGet),
              KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(bytesGet, 2u);

    bytesGet = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferWait(osal(), handle, data, sizeof(data), &bytesGet),
              KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(bytesGet, 4u);

    bytesGet = 99u;
    EXPECT_EQ(kiwiTest_osalStreamBufferPend(osal(), handle, data, sizeof(data), 55u, &bytesGet),
              KIWI_TEST_OSAL_STREAM_BUFFER_IS_EMPTY_ERR);
    EXPECT_EQ(bytesGet, 0u);
}


/**
 * \brief Verify stream-buffer reset, delete and registry lookup remain distinct operations.
 *
 * \details Stores a handle in the generic registry, reads it through HandleGet,
 *          then dispatches reset and delete through the backend vtable.
 *
 * Expected result:
 * - HandleGet returns the registered handle;
 * - reset and delete dispatch to their matching backend methods.
 */
TEST_F(OsalUnitTest, StreamBuffer_ResetDeleteAndHandleGet_UseExpectedLayers)
{
    StreamBufferHandleDummy dummy         = {};
    auto handle                           = static_cast<KiwiTest_osalStreamBufferHandle_t>(&dummy);
    KiwiTest_osalStreamBufferHandle_t out = nullptr;

    osal()->streamBufferObjHandle[0] = handle;
    EXPECT_CALL(mock_, streamBufferReset(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_STREAM_BUFFER_RESET_ERR));
    EXPECT_CALL(mock_, streamBufferDelete(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    ASSERT_EQ(kiwiTest_osalStreamBufferHandleGet(osal(), 0u, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, handle);
    EXPECT_EQ(kiwiTest_osalStreamBufferReset(osal(), handle), KIWI_TEST_OSAL_STREAM_BUFFER_RESET_ERR);
    EXPECT_EQ(kiwiTest_osalStreamBufferDelete(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify streambuffer API rejects a valid request on an uninitialized OSAL instance.
 *
 * \details Uses otherwise valid public arguments with a zero-initialized OSAL
 *          object so generic instance-state validation is exercised directly.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_NOT_INIT_ERR;
 * - no backend dispatch is required.
 */
TEST_F(OsalUnitTest, StreamBuffer_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized            = {};
    KiwiTest_osalStreamBufferHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(&uninitialized, 8u, 1u, &handle), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify streambuffer API reports an unavailable backend operation.
 *
 * \details Removes the representative operation from an otherwise valid backend
 *          vtable and performs a valid generic request.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
 * - the generic layer does not call through a null backend method.
 */
TEST_F(OsalUnitTest, StreamBuffer_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;

    vtable.streamBufferCreate = nullptr;
    osal()->vtable            = &vtable;
    KiwiTest_osalStreamBufferHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 1u, &handle), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
