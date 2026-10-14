/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef KIWI_OSAL_FREERTOS_POSIX_IT_TEST_FIXTURE_HPP_
#define KIWI_OSAL_FREERTOS_POSIX_IT_TEST_FIXTURE_HPP_

#include "osal_it_test_utils.hpp"

#include <gtest/gtest.h>

extern "C"
{
#include "kiwi_test_osal_freertos.h"
}

class OsalFreertosPosixItTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(kiwiTest_osalFreertosInit(&port_, "freertos-posix-it", nullptr, nullptr),
                  KIWI_TEST_OSAL_NO_ERR);
        initialized_ = true;
    }

    void TearDown() override
    {
        if (secondaryInitialized_)
        {
            EXPECT_EQ(kiwiTest_osalFreertosDeinit(&secondaryPort_), KIWI_TEST_OSAL_NO_ERR);
            secondaryInitialized_ = false;
        }

        if (initialized_)
        {
            EXPECT_EQ(kiwiTest_osalFreertosDeinit(&port_), KIWI_TEST_OSAL_NO_ERR);
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
            kiwiTest_osalFreertosInit(&secondaryPort_, "freertos-posix-it-secondary", nullptr, nullptr);

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

    KiwiTest_osalFreertos_s port_          = {};
    KiwiTest_osalFreertos_s secondaryPort_ = {};
    bool initialized_                      = false;
    bool secondaryInitialized_             = false;
};

#endif /* KIWI_OSAL_FREERTOS_POSIX_IT_TEST_FIXTURE_HPP_ */
