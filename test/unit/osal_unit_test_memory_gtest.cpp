/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_unit_test_fixture.hpp"

#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Return;

/**
 * \brief Verify generic memory allocation validates requests and backend results.
 *
 * \details Exercises zero-size/null-output validation, successful allocation,
 *          explicit backend failure and the defensive conversion of a successful
 *          backend status with a null pointer into MEM_ALLOCATION_ERR.
 *
 * Expected result:
 * - invalid requests are rejected before backend dispatch;
 * - successful non-null allocation is published;
 * - backend failures and null-success anomalies produce the expected status.
 */
TEST_F(OsalUnitTest, Memory_Alloc_ValidatesBackendResult)
{
    int storage = 0;
    void *out = nullptr;

    EXPECT_EQ(kiwiTest_osalMalloc(osal(), 0u, &out), KIWI_TEST_OSAL_INVALID_ARGS_ERR);
    EXPECT_EQ(kiwiTest_osalMalloc(osal(), 16u, nullptr), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    EXPECT_CALL(mock_, memAlloc(osal(), 16u, _))
        .WillOnce([&](void *, size_t, void **ptr)
        {
            *ptr = &storage;
            return KIWI_TEST_OSAL_NO_ERR;
        });
    ASSERT_EQ(kiwiTest_osalMalloc(osal(), 16u, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, &storage);

    EXPECT_CALL(mock_, memAlloc(osal(), 32u, _))
        .WillOnce(Return(KIWI_TEST_OSAL_MEM_ALLOCATION_ERR));
    EXPECT_EQ(kiwiTest_osalMalloc(osal(), 32u, &out), KIWI_TEST_OSAL_MEM_ALLOCATION_ERR);

    EXPECT_CALL(mock_, memAlloc(osal(), 64u, _))
        .WillOnce([](void *, size_t, void **ptr)
        {
            *ptr = nullptr;
            return KIWI_TEST_OSAL_NO_ERR;
        });
    EXPECT_EQ(kiwiTest_osalMalloc(osal(), 64u, &out), KIWI_TEST_OSAL_MEM_ALLOCATION_ERR);
}


/**
 * \brief Verify memory free and registered-pointer lookup use their intended layers.
 *
 * \details Stores a pointer in the generic memory registry, retrieves it through
 *          MemPtrGet and then verifies Free delegates the pointer to the backend.
 *
 * Expected result:
 * - MemPtrGet returns the registered pointer;
 * - Free forwards the same pointer and propagates backend status.
 */
TEST_F(OsalUnitTest, Memory_FreeAndHandleGet_UseRegistryAndBackend)
{
    int storage = 0;
    void *out = nullptr;

    osal()->memPtr[1] = &storage;
    ASSERT_EQ(kiwiTest_osalMemPtrGet(osal(), 1u, &out), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(out, &storage);

    EXPECT_CALL(mock_, memFree(osal(), &storage)).WillOnce(Return(KIWI_TEST_OSAL_PORT_SPECIFIC_ERR));
    EXPECT_EQ(kiwiTest_osalFree(osal(), &storage), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);
}



/**
 * \brief Verify memory API rejects a valid request on an uninitialized instance.
 *
 * \details Uses a zero-initialized OSAL object with otherwise valid allocation
 *          size and output storage.
 *
 * Expected result:
 * - Malloc returns KIWI_TEST_OSAL_NOT_INIT_ERR.
 */
TEST_F(OsalUnitTest, Memory_NotInitialized_ReturnsNotInit)
{
    KiwiTest_osal_s uninitialized = {};
    void *memory = nullptr;

    EXPECT_EQ(kiwiTest_osalMalloc(&uninitialized, 16u, &memory), KIWI_TEST_OSAL_NOT_INIT_ERR);
}

/**
 * \brief Verify memory API reports an unavailable backend operation.
 *
 * \details Removes MemAlloc from an otherwise valid backend vtable and performs
 *          a valid generic allocation request.
 *
 * Expected result:
 * - Malloc returns KIWI_TEST_OSAL_PORT_SPECIFIC_ERR.
 */
TEST_F(OsalUnitTest, Memory_MissingBackendOperation_ReturnsPortSpecific)
{
    KiwiTest_osalVtable_s vtable = kOsalMockVtable;
    void *memory = nullptr;

    vtable.memAlloc = nullptr;
    osal()->vtable = &vtable;

    EXPECT_EQ(kiwiTest_osalMalloc(osal(), 16u, &memory), KIWI_TEST_OSAL_PORT_SPECIFIC_ERR);

    osal()->vtable = &kOsalMockVtable;
}
