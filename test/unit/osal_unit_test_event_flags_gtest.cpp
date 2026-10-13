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
struct EventFlagsHandleDummy
{
    int value;
};
}

/**
 * \brief Verify event-flags wrappers reject invalid public arguments and option bits.
 *
 * \details Exercises null handles, zero flag masks, null output storage and an
 *          option value containing bits outside the generic WAIT_ALL/NO_CLEAR mask.
 *
 * Expected result:
 * - every invalid request returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST_F(OsalUnitTest, EventFlags_InvalidArgs)
{
    EventFlagsHandleDummy dummy = {};
    auto handle = static_cast<KiwiTest_osalEventFlagsHandle_t>(&dummy);
    KiwiTest_osalEventFlagsHandle_t out = nullptr;
    uint32_t flags = 0u;

    EXPECT_EQ(kiwiTest_osalEventFlagsCreate(nullptr, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsCreate(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsDelete(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsSet(osal(), handle, 0u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsClear(osal(), handle, 0u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsGet(osal(), handle, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsWait(osal(), handle, 0u,
                                          KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ANY,
                                          10u, &flags),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsWait(osal(), handle, 0x01u,
                                          static_cast<KiwiTest_osalEventFlagsOptions_e>(0x80u),
                                          10u, &flags),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsHandleGet(osal(), KIWI_TEST_OSAL_EVENT_FLAGS_SLOTS_NUM, &out),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify event-flags lifecycle and bit operations dispatch to the backend.
 *
 * \details Creates a mock event-flags handle, exercises set, clear and get, then
 *          deletes the object while forcing distinct backend return statuses.
 *
 * Expected result:
 * - the generic API preserves handle and flag arguments;
 * - output flags written by the backend are returned to the caller;
 * - backend statuses are propagated unchanged.
 */
TEST_F(OsalUnitTest, EventFlags_LifecycleAndBitOperations_DispatchToBackend)
{
    EventFlagsHandleDummy dummy = {};
    auto handle = static_cast<KiwiTest_osalEventFlagsHandle_t>(&dummy);
    KiwiTest_osalEventFlagsHandle_t out = nullptr;
    uint32_t actual = 0u;

    EXPECT_CALL(mock_, eventFlagsCreate(osal(), _))
        .WillOnce(DoAll(SetArgPointee<1>(handle), Return(KIWI_TEST_OSAL_NO_ERR)));
    EXPECT_CALL(mock_, eventFlagsSet(osal(), handle, 0x05u)).WillOnce(Return(KIWI_TEST_OSAL_EVENT_FLAGS_SET_ERR));
    EXPECT_CALL(mock_, eventFlagsClear(osal(), handle, 0x04u)).WillOnce(Return(KIWI_TEST_OSAL_EVENT_FLAGS_CLEAR_ERR));
    EXPECT_CALL(mock_, eventFlagsGet(osal(), handle, _))
        .WillOnce(DoAll(SetArgPointee<2>(0x01u), Return(KIWI_TEST_OSAL_NO_ERR)));
    EXPECT_CALL(mock_, eventFlagsDelete(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, handle);
    EXPECT_EQ(kiwiTest_osalEventFlagsSet(osal(), handle, 0x05u), KIWI_TEST_OSAL_EVENT_FLAGS_SET_ERR);
    EXPECT_EQ(kiwiTest_osalEventFlagsClear(osal(), handle, 0x04u), KIWI_TEST_OSAL_EVENT_FLAGS_CLEAR_ERR);
    ASSERT_EQ(kiwiTest_osalEventFlagsGet(osal(), handle, &actual), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(actual, 0x01u);
    EXPECT_EQ(kiwiTest_osalEventFlagsDelete(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify event-flags wait forwards selection, clear policy and timeout.
 *
 * \details Uses a combined WAIT_ALL | NO_CLEAR option and a finite timeout so
 *          every behavior-defining argument of the generic wait contract is observed.
 *
 * Expected result:
 * - the backend receives the requested mask, options and timeout unchanged;
 * - actualFlags and the backend status are propagated unchanged.
 */
TEST_F(OsalUnitTest, EventFlags_Wait_DispatchesCompleteContract)
{
    EventFlagsHandleDummy dummy = {};
    auto handle = static_cast<KiwiTest_osalEventFlagsHandle_t>(&dummy);
    uint32_t actual = 0u;
    const auto options = static_cast<KiwiTest_osalEventFlagsOptions_e>(
        KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ALL | KIWI_TEST_OSAL_EVENT_FLAGS_NO_CLEAR);

    EXPECT_CALL(mock_, eventFlagsWait(osal(), handle, 0x03u, options, 125u, _))
        .WillOnce(DoAll(SetArgPointee<5>(0x03u), Return(KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ERR)));

    EXPECT_EQ(kiwiTest_osalEventFlagsWait(osal(), handle, 0x03u, options, 125u, &actual),
              KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ERR);
    EXPECT_EQ(actual, 0x03u);
}


/**
 * \brief Verify event-flags handle lookup reads the generic registry.
 *
 * \details Stores a stable handle in a known registry slot and retrieves it
 *          without backend dispatch.
 *
 * Expected result:
 * - lookup returns the exact handle stored in the requested slot.
 */
TEST_F(OsalUnitTest, EventFlags_HandleGet_ReturnsRegisteredHandle)
{
    EventFlagsHandleDummy dummy = {};
    auto handle = static_cast<KiwiTest_osalEventFlagsHandle_t>(&dummy);
    KiwiTest_osalEventFlagsHandle_t out = nullptr;

    osal()->eventFlagsObjHandle[1] = handle;

    ASSERT_EQ(kiwiTest_osalEventFlagsHandleGet(osal(), 1u, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, handle);
}


/**
 * \brief Verify eventflags API rejects a valid request on an uninitialized OSAL instance.
 *
 * \details Uses otherwise valid public arguments with a zero-initialized OSAL
 *          object so generic instance-state validation is exercised directly.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_NOT_INIT_ERR;
 * - no backend dispatch is required.
 */
TEST_F(OsalUnitTest, EventFlags_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized = {};
    KiwiTest_osalEventFlagsHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalEventFlagsCreate(&uninitialized, &handle), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify eventflags API reports an unavailable backend operation.
 *
 * \details Removes the representative operation from an otherwise valid backend
 *          vtable and performs a valid generic request.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
 * - the generic layer does not call through a null backend method.
 */
TEST_F(OsalUnitTest, EventFlags_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;
    vtable.eventFlagsCreate = nullptr;
    osal()->vtable = &vtable;
    KiwiTest_osalEventFlagsHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &handle), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
