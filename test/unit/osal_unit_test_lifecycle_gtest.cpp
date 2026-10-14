/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_unit_test_fixture.hpp"

#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Return;

/**
 * \brief Verify generic OSAL initialization rejects a null instance pointer.
 *
 * \details Calls the lifecycle entry point without storage for an OSAL object.
 *          The generic layer must reject the request before touching any state.
 *
 * Expected result:
 * - initialization returns KIWI_TEST_OSAL_INVALID_ARGS_ERR.
 */
TEST(OsalLifecycleUnitTest, Init_InvalidArgs)
{
    EXPECT_EQ(kiwiTest_osalInit(nullptr, "unit", nullptr),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);
}


/**
 * \brief Verify generic lifecycle state and metadata initialization.
 *
 * \details Initializes a standalone generic OSAL object and verifies that the
 *          name, parent and protected helper table are published while the
 *          backend vtable remains unbound until a port is attached.
 *
 * Expected result:
 * - initialization succeeds and marks the base object valid;
 * - metadata matches the supplied values;
 * - ptable is available and vtable remains null;
 * - deinitialization clears the generic lifecycle state.
 */
TEST(OsalLifecycleUnitTest, InitDeinit_PublishesAndClearsBaseState)
{
    KiwiTest_osal_s osal = {};
    int parent           = 42;

    /* Initialize an isolated generic OSAL object */
    ASSERT_EQ(kiwiTest_osalInit(&osal, "generic", &parent), KIWI_TEST_OSAL_NO_ERR);

    /* Verify the state published by generic initialization */
    EXPECT_TRUE(osal.validFlag);
    EXPECT_STREQ(osal.name, "generic");
    EXPECT_EQ(osal.parent, &parent);
    EXPECT_NE(osal.ptable, nullptr);
    EXPECT_EQ(osal.vtable, nullptr);

    /* Deinitialize and verify generic lifecycle state is cleared */
    ASSERT_EQ(kiwiTest_osalDeinit(&osal), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_FALSE(osal.validFlag);
    EXPECT_EQ(osal.name, nullptr);
    EXPECT_EQ(osal.parent, nullptr);
    EXPECT_EQ(osal.vtable, nullptr);
}


/**
 * \brief Verify deinitialization rejects an object that is not initialized.
 *
 * \details Calls generic deinitialization for zero-initialized storage without
 *          a preceding initialization call.
 *
 * Expected result:
 * - deinitialization returns KIWI_TEST_OSAL_NOT_INIT_ERR.
 */
TEST(OsalLifecycleUnitTest, Deinit_NotInitialized)
{
    KiwiTest_osal_s osal = {};

    EXPECT_EQ(kiwiTest_osalDeinit(&osal), KIWI_TEST_OSAL_NOT_INIT_ERR);
}


/**
 * \brief Verify the public validity predicate delegates to the bound backend.
 *
 * \details Uses the mock-backed fixture and configures the backend predicate
 *          to return false and then true for the same otherwise-valid base object.
 *
 * Expected result:
 * - the generic predicate returns the backend predicate result;
 * - a null OSAL pointer is reported invalid without backend dispatch.
 */
TEST_F(OsalUnitTest, IsValid_DelegatesToBackendPredicate)
{
    EXPECT_CALL(mock_, isValid(osal()))
    .WillOnce(Return(false))
    .WillOnce(Return(true));

    EXPECT_FALSE(kiwiTest_osalIsValid(osal()));
    EXPECT_TRUE(kiwiTest_osalIsValid(osal()));
    EXPECT_FALSE(kiwiTest_osalIsValid(nullptr));
}


/**
 * \brief Verify generic parent and name metadata accessors.
 *
 * \details Replaces both metadata values through the public setters and reads
 *          them back through the corresponding getters.
 *
 * Expected result:
 * - setters complete successfully;
 * - getters return the newly assigned parent and name values.
 */
TEST_F(OsalUnitTest, Metadata_SetGet)
{
    int parent          = 7;
    void *parentOut     = nullptr;
    const char *nameOut = nullptr;

    /* Replace metadata through the public generic API */
    ASSERT_EQ(kiwiTest_osalParentSet(osal(), &parent), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalNameSet(osal(), "renamed"), KIWI_TEST_OSAL_NO_ERR);

    /* Read the resulting metadata */
    ASSERT_EQ(kiwiTest_osalParentGet(osal(), &parentOut), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalNameGet(osal(), &nameOut), KIWI_TEST_OSAL_NO_ERR);

    EXPECT_EQ(parentOut, &parent);
    EXPECT_STREQ(nameOut, "renamed");
}
