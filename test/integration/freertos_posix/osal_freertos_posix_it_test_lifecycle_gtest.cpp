/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_freertos_posix_it_test_fixture.hpp"

#include <gtest/gtest.h>

/**
 * \brief Verify the FreeRTOS POSIX-port fixture publishes a valid isolated OSAL instance.
 *
 * \details Uses the fixture-owned initialization path and checks the generic validity
 *          predicate together with the configured test-instance name.
 *
 * Expected result:
 * - the backend-backed OSAL instance is valid for the duration of the test;
 * - fixture metadata is available through the generic API.
 */
TEST_F(OsalFreertosPosixItTest, Lifecycle_FixtureProvidesValidIsolatedInstance)
{
    const char *name = nullptr;

    /* Verify the instance created by SetUp */
    EXPECT_TRUE(kiwiTest_osalIsValid(osal()));
    ASSERT_EQ(kiwiTest_osalNameGet(osal(), &name), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_STREQ(name, "freertos-posix-it");
}


/**
 * \brief Verify a FreeRTOS POSIX-port OSAL instance can be deinitialized and initialized again cleanly.
 *
 * \details Explicitly releases the fixture instance, confirms the generic validity
 *          predicate becomes false, then initializes the same storage again before teardown.
 *
 * Expected result:
 * - deinitialization invalidates the instance;
 * - reinitialization succeeds and restores a valid backend state.
 */
TEST_F(OsalFreertosPosixItTest, Lifecycle_DeinitReinit_RestoresCleanBackendState)
{
    /* Release the fixture-owned instance */
    ASSERT_EQ(kiwiTest_osalFreertosDeinit(&port_), KIWI_TEST_OSAL_NO_ERR);
    initialized_ = false;
    EXPECT_FALSE(kiwiTest_osalIsValid(osal()));

    /* Reinitialize the same storage for a fresh backend lifetime */
    ASSERT_EQ(kiwiTest_osalFreertosInit(&port_, "freertos-posix-it", nullptr, nullptr), KIWI_TEST_OSAL_NO_ERR);
    initialized_ = true;
    EXPECT_TRUE(kiwiTest_osalIsValid(osal()));
}
