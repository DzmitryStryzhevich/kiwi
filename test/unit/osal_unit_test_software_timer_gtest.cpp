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
struct TimerHandleDummy
{
    int value;
};

void timerCallback(void *)
{
}
}

/**
 * \brief Verify software-timer creation validates callback and period contract.
 *
 * \details Exercises null output storage, null callback, zero period and the
 *          reserved infinite timeout value before backend dispatch.
 *
 * Expected result:
 * - every invalid timer configuration returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST_F(OsalUnitTest, SoftwareTimer_Create_InvalidArgs)
{
    KiwiTest_osalSoftwareTimerHandle_t out = nullptr;
    KiwiTest_osalSoftwareTimerAttr_s attr  = {"timer", nullptr, timerCallback, false, 25u};

    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(nullptr, &out, attr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(), nullptr, attr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    attr.timerExpiredCb = nullptr;
    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(), &out, attr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    attr.timerExpiredCb = timerCallback;
    attr.periodMs       = 0u;
    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(), &out, attr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    attr.periodMs = KIWI_TEST_OSAL_INFINITY_TOUT;
    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(), &out, attr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify software-timer creation forwards the complete attribute snapshot.
 *
 * \details Supplies name, callback parameter, callback, auto-reload policy and
 *          period and checks all values observed by the mock backend.
 *
 * Expected result:
 * - every timer attribute reaches the backend unchanged;
 * - the output handle and backend status are propagated unchanged.
 */
TEST_F(OsalUnitTest, SoftwareTimer_Create_DispatchesCompleteAttributes)
{
    TimerHandleDummy dummy                 = {};
    auto handle                            = static_cast<KiwiTest_osalSoftwareTimerHandle_t>(&dummy);
    KiwiTest_osalSoftwareTimerHandle_t out = nullptr;
    int callbackParam                      = 9;
    KiwiTest_osalSoftwareTimerAttr_s attr  = {"periodic", &callbackParam, timerCallback, true, 125u};

    EXPECT_CALL(mock_, softwareTimerCreate(osal(), _, _))
    .WillOnce([&](void *, KiwiTest_osalSoftwareTimerHandle_t *timerHandle,
                  KiwiTest_osalSoftwareTimerAttr_s actualAttr)
    {
        EXPECT_STREQ(actualAttr.name, attr.name);
        EXPECT_EQ(actualAttr.timerParam, attr.timerParam);
        EXPECT_EQ(actualAttr.timerExpiredCb, attr.timerExpiredCb);
        EXPECT_EQ(actualAttr.autoReload, attr.autoReload);
        EXPECT_EQ(actualAttr.periodMs, attr.periodMs);
        *timerHandle = handle;

        return KIWI_TEST_OSAL_SOFTWARE_TIMER_CREATE_ERR;
    });

    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(), &out, attr),
              KIWI_TEST_OSAL_SOFTWARE_TIMER_CREATE_ERR);
    EXPECT_EQ(out, handle);
}


/**
 * \brief Verify software-timer control operations dispatch independently.
 *
 * \details Exercises start, stop, reset and delete through one stable handle and
 *          forces each backend method to return its dedicated error status.
 *
 * Expected result:
 * - each public control operation calls the matching backend method;
 * - each backend status is propagated unchanged.
 */
TEST_F(OsalUnitTest, SoftwareTimer_ControlOperations_DispatchAndPropagateStatus)
{
    TimerHandleDummy dummy = {};
    auto handle            = static_cast<KiwiTest_osalSoftwareTimerHandle_t>(&dummy);

    EXPECT_CALL(mock_, softwareTimerStart(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_SOFTWARE_TIMER_START_ERR));
    EXPECT_CALL(mock_, softwareTimerStop(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_SOFTWARE_TIMER_STOP_ERR));
    EXPECT_CALL(mock_, softwareTimerReset(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_SOFTWARE_TIMER_RESET_ERR));
    EXPECT_CALL(mock_, softwareTimerDelete(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    EXPECT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), handle), KIWI_TEST_OSAL_SOFTWARE_TIMER_START_ERR);
    EXPECT_EQ(kiwiTest_osalSoftwareTimerStop(osal(), handle), KIWI_TEST_OSAL_SOFTWARE_TIMER_STOP_ERR);
    EXPECT_EQ(kiwiTest_osalSoftwareTimerReset(osal(), handle), KIWI_TEST_OSAL_SOFTWARE_TIMER_RESET_ERR);
    EXPECT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify software-timer handle lookup returns the registered native handle.
 *
 * \details Writes a complete timer registry object into a known slot and retrieves
 *          the opaque handle through the public generic lookup API.
 *
 * Expected result:
 * - HandleGet returns the handle stored in the selected timer registry object.
 */
TEST_F(OsalUnitTest, SoftwareTimer_HandleGet_ReturnsRegisteredHandle)
{
    TimerHandleDummy dummy                 = {};
    auto handle                            = static_cast<KiwiTest_osalSoftwareTimerHandle_t>(&dummy);
    KiwiTest_osalSoftwareTimerHandle_t out = nullptr;

    osal()->softwareTimerObj[1].handle = handle;

    ASSERT_EQ(kiwiTest_osalSoftwareTimerHandleGet(osal(), 1u, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, handle);
}


/**
 * \brief Verify software-timer API rejects a valid request on an uninitialized instance.
 *
 * \details Supplies a valid callback and finite period with a zero-initialized
 *          OSAL object so only generic instance-state validation fails.
 *
 * Expected result:
 * - SoftwareTimerCreate returns KIWI_TEST_OSAL_NOT_INIT_ERR.
 */
TEST_F(OsalUnitTest, SoftwareTimer_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized             = {};
    KiwiTest_osalSoftwareTimerHandle_t handle = nullptr;
    KiwiTest_osalSoftwareTimerAttr_s attr     = {};

    attr.timerExpiredCb = [](void *const) {
                          };
    attr.periodMs = 10u;

    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(&uninitialized, &handle, attr),
              KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify software-timer API reports an unavailable backend operation.
 *
 * \details Removes SoftwareTimerCreate from an otherwise valid backend vtable and
 *          performs a valid creation request.
 *
 * Expected result:
 * - SoftwareTimerCreate returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR.
 */
TEST_F(OsalUnitTest, SoftwareTimer_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;

    vtable.softwareTimerCreate = nullptr;
    osal()->vtable             = &vtable;
    KiwiTest_osalSoftwareTimerHandle_t handle = nullptr;
    KiwiTest_osalSoftwareTimerAttr_s attr     = {};
    attr.timerExpiredCb = [](void *const) {
                          };
    attr.periodMs = 10u;

    EXPECT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(), &handle, attr),
              KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
