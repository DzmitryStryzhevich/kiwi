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
struct ThreadHandleDummy
{
    int value;
};

void threadWorker(void *)
{
}
}

/**
 * \brief Verify thread wrappers reject invalid public arguments.
 *
 * \details Exercises null OSAL/handle arguments, invalid DelayUntil storage and
 *          zero DelayUntil period without invoking the backend.
 *
 * Expected result:
 * - every invalid request returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST_F(OsalUnitTest, Thread_InvalidArgs)
{
    KiwiTest_osalThreadHandle_t handle = nullptr;
    KiwiTest_osalThreadAttr_s attr     = {threadWorker, "worker", 4096u, nullptr, KIWI_TEST_OSAL_THREAD_PRIO_NORMAL};
    KiwiTest_osalTimeMs_t wake         = 10u;

    EXPECT_EQ(kiwiTest_osalThreadCreate(nullptr, &handle, attr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadCreate(osal(), nullptr, attr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadDelete(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadSuspend(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadResume(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadYield(nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadDelay(nullptr, 0u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadDelayUntil(osal(), nullptr, 10u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadDelayUntil(osal(), &wake, 0u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalThreadHandleGet(osal(), KIWI_TEST_OSAL_THREAD_SLOTS_NUM, &handle),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify thread creation forwards the complete generic attribute object.
 *
 * \details Supplies a worker, name, stack size, argument and priority and checks
 *          that the mock backend observes every field unchanged.
 *
 * Expected result:
 * - thread attributes reach the backend unchanged;
 * - the backend-written handle and status are propagated unchanged.
 */
TEST_F(OsalUnitTest, Thread_Create_DispatchesCompleteAttributes)
{
    ThreadHandleDummy dummy                  = {};
    auto expectedHandle                      = static_cast<KiwiTest_osalThreadHandle_t>(&dummy);
    KiwiTest_osalThreadHandle_t actualHandle = nullptr;
    int workerArg                            = 33;
    KiwiTest_osalThreadAttr_s attr           = {
        threadWorker,
        "worker",
        8192u,
        &workerArg,
        KIWI_TEST_OSAL_THREAD_PRIO_HIGH
    };

    EXPECT_CALL(mock_, threadCreate(osal(), _, _))
    .WillOnce([&](void *, KiwiTest_osalThreadHandle_t *handle, KiwiTest_osalThreadAttr_s actualAttr)
    {
        EXPECT_EQ(actualAttr.worker, attr.worker);
        EXPECT_STREQ(actualAttr.name, attr.name);
        EXPECT_EQ(actualAttr.stackSize, attr.stackSize);
        EXPECT_EQ(actualAttr.args, attr.args);
        EXPECT_EQ(actualAttr.prio, attr.prio);
        *handle = expectedHandle;

        return KIWI_TEST_OSAL_THREAD_CREATE_ERR;
    });

    EXPECT_EQ(kiwiTest_osalThreadCreate(osal(), &actualHandle, attr), KIWI_TEST_OSAL_THREAD_CREATE_ERR);
    EXPECT_EQ(actualHandle, expectedHandle);
}


/**
 * \brief Verify thread lifecycle control wrappers dispatch to the backend.
 *
 * \details Exercises suspend, resume and delete for one stable thread handle and
 *          forces distinct backend status values.
 *
 * Expected result:
 * - each wrapper calls only its matching backend operation;
 * - backend status values are propagated unchanged.
 */
TEST_F(OsalUnitTest, Thread_LifecycleControls_DispatchAndPropagateStatus)
{
    ThreadHandleDummy dummy = {};
    auto handle             = static_cast<KiwiTest_osalThreadHandle_t>(&dummy);

    EXPECT_CALL(mock_, threadSuspend(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_PORT_SPECIFIC_ERR));
    EXPECT_CALL(mock_, threadResume(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_PORT_SPECIFIC_ERR));
    EXPECT_CALL(mock_, threadDelete(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    EXPECT_EQ(kiwiTest_osalThreadSuspend(osal(), handle), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);
    EXPECT_EQ(kiwiTest_osalThreadResume(osal(), handle), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);
    EXPECT_EQ(kiwiTest_osalThreadDelete(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify explicit yield and delay operations remain separate backend calls.
 *
 * \details Invokes ThreadYield, ThreadDelay(0) and a finite delay and verifies
 *          that zero-duration delay is still forwarded to ThreadDelay rather than
 *          being translated into ThreadYield by the generic layer.
 *
 * Expected result:
 * - ThreadYield calls only the yield backend method;
 * - zero and finite delays call the delay backend method with the exact value.
 */
TEST_F(OsalUnitTest, Thread_YieldAndDelay_DispatchAsDistinctOperations)
{
    EXPECT_CALL(mock_, threadYield(osal())).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));
    EXPECT_CALL(mock_, threadDelay(osal(), 0u)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));
    EXPECT_CALL(mock_, threadDelay(osal(), 20u)).WillOnce(Return(KIWI_TEST_OSAL_PORT_SPECIFIC_ERR));

    EXPECT_EQ(kiwiTest_osalThreadYield(osal()), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalThreadDelay(osal(), 0u), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalThreadDelay(osal(), 20u), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);
}


/**
 * \brief Verify periodic delay forwards the wake reference and period.
 *
 * \details Provides a mutable previous wake timestamp and verifies the mock backend
 *          can update the same storage while returning its own status.
 *
 * Expected result:
 * - the exact wake pointer and period reach the backend;
 * - backend updates of previousWakeTimeMs remain visible to the caller.
 */
TEST_F(OsalUnitTest, Thread_DelayUntil_DispatchesInOutWakeReference)
{
    KiwiTest_osalTimeMs_t wake = 100u;

    EXPECT_CALL(mock_, threadDelayUntil(osal(), &wake, 25u))
    .WillOnce([&](void *, KiwiTest_osalTimeMs_t *previousWakeTimeMs, KiwiTest_osalTimeMs_t)
    {
        *previousWakeTimeMs = 125u;

        return KIWI_TEST_OSAL_NO_ERR;
    });

    ASSERT_EQ(kiwiTest_osalThreadDelayUntil(osal(), &wake, 25u), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(wake, 125u);
}


/**
 * \brief Verify thread handle lookup reads the generic registry object.
 *
 * \details Stores a handle in one thread registry slot and retrieves it through
 *          the public generic handle lookup API.
 *
 * Expected result:
 * - lookup returns the exact handle stored in the selected thread object.
 */
TEST_F(OsalUnitTest, Thread_HandleGet_ReturnsRegisteredHandle)
{
    ThreadHandleDummy dummy         = {};
    auto handle                     = static_cast<KiwiTest_osalThreadHandle_t>(&dummy);
    KiwiTest_osalThreadHandle_t out = nullptr;

    osal()->threadObjHandle[0].handle = handle;

    ASSERT_EQ(kiwiTest_osalThreadHandleGet(osal(), 0u, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, handle);
}


/**
 * \brief Verify thread API rejects a valid request on an uninitialized OSAL instance.
 *
 * \details Uses valid worker attributes and output storage with a zero-initialized
 *          OSAL object so generic instance-state validation is exercised directly.
 *
 * Expected result:
 * - ThreadCreate returns KIWI_TEST_OSAL_NOT_INIT_ERR.
 */
TEST_F(OsalUnitTest, Thread_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized      = {};
    KiwiTest_osalThreadHandle_t handle = nullptr;
    KiwiTest_osalThreadAttr_s attr     = {threadWorker, "worker", 1024u, nullptr, KIWI_TEST_OSAL_THREAD_PRIO_NORMAL};

    EXPECT_EQ(kiwiTest_osalThreadCreate(&uninitialized, &handle, attr), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify thread API reports an unavailable backend operation.
 *
 * \details Removes ThreadYield from an otherwise valid backend vtable and invokes
 *          the generic yield operation.
 *
 * Expected result:
 * - ThreadYield returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR.
 */
TEST_F(OsalUnitTest, Thread_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;

    vtable.threadYield = nullptr;
    osal()->vtable     = &vtable;

    EXPECT_EQ(kiwiTest_osalThreadYield(osal()), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
