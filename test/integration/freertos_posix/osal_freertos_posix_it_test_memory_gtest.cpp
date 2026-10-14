/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_freertos_posix_it_test_fixture.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

/**
 * \brief Verify FreeRTOS POSIX-port memory allocation returns usable storage and releases it cleanly.
 *
 * \details Allocates one block through the backend, writes a deterministic byte pattern,
 *          verifies the content and releases the same registered pointer.
 *
 * Expected result:
 * - allocation returns non-null writable storage;
 * - data written through the returned pointer remains intact;
 * - free completes successfully.
 */
TEST_F(OsalFreertosPosixItTest, Memory_AllocatedBlock_IsWritableAndReleasable)
{
    void *memory          = nullptr;
    constexpr size_t size = 64u;

    /* Allocate registered backend memory */
    ASSERT_EQ(kiwiTest_osalMalloc(osal(), size, &memory), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_NE(memory, nullptr);

    /* Write and verify a deterministic pattern */
    std::memset(memory, 0xA5, size);
    const auto *bytes = static_cast<const uint8_t *>(memory);

    for (size_t i = 0u; i < size; ++i)
    {
        EXPECT_EQ(bytes[i], 0xA5u);
    }

    /* Release the registered allocation */
    EXPECT_EQ(kiwiTest_osalFree(osal(), memory), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify allocated memory can only be released through its owning OSAL instance.
 *
 * \details Allocates registered backend memory through the fixture instance and
 *          attempts to free the same pointer through a second initialized OSAL instance
 *          before releasing it through the original owner.
 *
 * Expected result:
 * - the foreign instance rejects the allocation with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - the allocation remains writable and can still be released by the owner.
 */
TEST_F(OsalFreertosPosixItTest, Memory_ForeignInstance_CannotFreeOwnedAllocation)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    void *memory          = nullptr;
    constexpr size_t size = 32u;
    ASSERT_EQ(kiwiTest_osalMalloc(osal(), size, &memory), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_NE(memory, nullptr);

    /* Act */
    EXPECT_EQ(kiwiTest_osalFree(secondaryOsal(), memory), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert that foreign rejection did not release the allocation */
    std::memset(memory, 0x5A, size);
    const auto *bytes = static_cast<const uint8_t *>(memory);
    EXPECT_EQ(bytes[0], 0x5Au);
    EXPECT_EQ(bytes[size - 1u], 0x5Au);
    ASSERT_EQ(kiwiTest_osalFree(osal(), memory), KIWI_TEST_OSAL_NO_ERR);
}
