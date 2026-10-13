/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef KIWI_OSAL_POSIX_IT_TEST_FIXTURE_HPP_
#define KIWI_OSAL_POSIX_IT_TEST_FIXTURE_HPP_

#include "osal_it_test_utils.hpp"

#include <gtest/gtest.h>

extern "C"
{
#include "kiwi_test_osal_posix.h"
}

class OsalPosixItTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(kiwiTest_osalPosixInit(&port_, "posix-it", nullptr, nullptr),
                  KIWI_TEST_OSAL_NO_ERR);
        initialized_ = true;
    }

    void TearDown() override
    {
        if (secondaryInitialized_)
        {
            EXPECT_EQ(kiwiTest_osalPosixDeinit(&secondaryPort_), KIWI_TEST_OSAL_NO_ERR);
            secondaryInitialized_ = false;
        }

        if (initialized_)
        {
            EXPECT_EQ(kiwiTest_osalPosixDeinit(&port_), KIWI_TEST_OSAL_NO_ERR);
            initialized_ = false;
        }
    }

    KiwiTest_osal_s *osal()
    {
        return &port_.base;
    }

    bool initializeSecondaryOsal()
    {
        const KiwiTest_osalErr_e status =
            kiwiTest_osalPosixInit(&secondaryPort_, "posix-it-secondary", nullptr, nullptr);
        if (status != KIWI_TEST_OSAL_NO_ERR)
        {
            EXPECT_EQ(status, KIWI_TEST_OSAL_NO_ERR);
            return false;
        }

        secondaryInitialized_ = true;
        return true;
    }

    KiwiTest_osal_s *secondaryOsal()
    {
        return &secondaryPort_.base;
    }

    KiwiTest_osalPosix_s port_ = {};
    KiwiTest_osalPosix_s secondaryPort_ = {};
    bool initialized_ = false;
    bool secondaryInitialized_ = false;
};

#endif /* KIWI_OSAL_POSIX_IT_TEST_FIXTURE_HPP_ */
