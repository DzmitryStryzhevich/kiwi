/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef KIWI_OSAL_UNIT_TEST_FIXTURE_HPP_
#define KIWI_OSAL_UNIT_TEST_FIXTURE_HPP_

#include "mocks/osal_mock.hpp"

#include <gtest/gtest.h>

#include <memory>

class OsalUnitTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(kiwiTest_osalInit(&osal_, "unit", nullptr), KIWI_TEST_OSAL_NO_ERR);
        binder_ = std::make_unique<OsalMockBinder>(&osal_, &mock_);

        ON_CALL(mock_, isValid(::testing::_))
        .WillByDefault(::testing::Return(true));
    }

    void TearDown() override
    {
        binder_.reset();

        if (osal_.validFlag)
        {
            ASSERT_EQ(kiwiTest_osalDeinit(&osal_), KIWI_TEST_OSAL_NO_ERR);
        }
    }

    KiwiTest_osal_s *osal()
    {
        return &osal_;
    }

    ::testing::NiceMock<OsalMock> mock_;
    KiwiTest_osal_s osal_ = {};
    std::unique_ptr<OsalMockBinder> binder_;
};

#endif /* KIWI_OSAL_UNIT_TEST_FIXTURE_HPP_ */
