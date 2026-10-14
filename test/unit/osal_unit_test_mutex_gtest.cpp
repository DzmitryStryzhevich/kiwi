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
struct MutexHandleDummy
{
    int value;
};
}

/**
 * \brief Verify mutex wrappers reject invalid public arguments.
 *
 * \details Exercises create, lock, timed lock, unlock, delete and handle lookup
 *          with null handles or invalid output storage.
 *
 * Expected result:
 * - every invalid request returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST_F(OsalUnitTest, Mutex_InvalidArgs)
{
    KiwiTest_osalMutexHandle_t out = nullptr;

    EXPECT_EQ(kiwiTest_osalMutexCreate(nullptr, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMutexCreate(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMutexDelete(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMutexLock(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMutexTryLock(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMutexPendLock(osal(), nullptr, 10u), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMutexUnlock(osal(), nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMutexHandleGet(osal(), KIWI_TEST_OSAL_MUTEX_SLOTS_NUM, &out),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify mutex lifecycle and lock wrappers delegate to the backend.
 *
 * \details Creates a mock mutex handle and exercises lock, try-lock, timed-lock,
 *          unlock and delete through the generic API.
 *
 * Expected result:
 * - the create output handle is forwarded unchanged;
 * - every operation dispatches to the matching backend method;
 * - backend status values are propagated unchanged.
 */
TEST_F(OsalUnitTest, Mutex_LifecycleAndOperations_DispatchToBackend)
{
    MutexHandleDummy dummy         = {};
    auto handle                    = static_cast<KiwiTest_osalMutexHandle_t>(&dummy);
    KiwiTest_osalMutexHandle_t out = nullptr;

    EXPECT_CALL(mock_, mutexCreate(osal(), _))
    .WillOnce(DoAll(SetArgPointee<1>(handle), Return(KIWI_TEST_OSAL_NO_ERR)));
    EXPECT_CALL(mock_, mutexLock(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));
    EXPECT_CALL(mock_, mutexTryLock(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_MUTEX_LOCK_ERR));
    EXPECT_CALL(mock_, mutexPendLock(osal(), handle, 30u)).WillOnce(Return(KIWI_TEST_OSAL_MUTEX_LOCK_ERR));
    EXPECT_CALL(mock_, mutexUnlock(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));
    EXPECT_CALL(mock_, mutexDelete(osal(), handle)).WillOnce(Return(KIWI_TEST_OSAL_NO_ERR));

    ASSERT_EQ(kiwiTest_osalMutexCreate(osal(), &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, handle);
    EXPECT_EQ(kiwiTest_osalMutexLock(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalMutexTryLock(osal(), handle), KIWI_TEST_OSAL_MUTEX_LOCK_ERR);
    EXPECT_EQ(kiwiTest_osalMutexPendLock(osal(), handle, 30u), KIWI_TEST_OSAL_MUTEX_LOCK_ERR);
    EXPECT_EQ(kiwiTest_osalMutexUnlock(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalMutexDelete(osal(), handle), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify mutex API rejects a valid request on an uninitialized OSAL instance.
 *
 * \details Uses otherwise valid public arguments with a zero-initialized OSAL
 *          object so generic instance-state validation is exercised directly.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_NOT_INIT_ERR;
 * - no backend dispatch is required.
 */
TEST_F(OsalUnitTest, Mutex_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized     = {};
    KiwiTest_osalMutexHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalMutexCreate(&uninitialized, &handle), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify mutex API reports an unavailable backend operation.
 *
 * \details Removes the representative operation from an otherwise valid backend
 *          vtable and performs a valid generic request.
 *
 * Expected result:
 * - the operation returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
 * - the generic layer does not call through a null backend method.
 */
TEST_F(OsalUnitTest, Mutex_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;

    vtable.mutexCreate = nullptr;
    osal()->vtable     = &vtable;
    KiwiTest_osalMutexHandle_t handle = nullptr;

    EXPECT_EQ(kiwiTest_osalMutexCreate(osal(), &handle), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
