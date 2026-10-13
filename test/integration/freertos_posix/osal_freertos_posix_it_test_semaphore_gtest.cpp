/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_freertos_posix_it_test_fixture.hpp"
#include "osal_it_test_utils.hpp"

#include <atomic>

namespace
{

struct SemaphoreWaitContext
{
    KiwiTest_osal_s *osal = nullptr;
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    bool timed = false;
    KiwiTest_osalTimeMs_t timeoutMs = 0u;
    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    KiwiTest_osalErr_e status = KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
};

void semaphoreWaitWorker(void *const args)
{
    auto *const context = static_cast<SemaphoreWaitContext *>(args);
    context->started.store(true);
    context->status = context->timed
                    ? kiwiTest_osalSemaphorePend(context->osal, context->semaphore, context->timeoutMs)
                    : kiwiTest_osalSemaphoreWait(context->osal, context->semaphore);
    context->completed.store(true);
    kiwi::test::parkWorker(context->osal);
}

struct SemaphoreMultiWaitContext
{
    KiwiTest_osal_s *osal = nullptr;
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    std::atomic<unsigned int> started{0u};
    std::atomic<unsigned int> completed{0u};
};

void semaphoreMultiWaitWorker(void *const args)
{
    auto *const context = static_cast<SemaphoreMultiWaitContext *>(args);
    context->started.fetch_add(1u);
    if (kiwiTest_osalSemaphoreWait(context->osal, context->semaphore) == KIWI_TEST_OSAL_NO_ERR)
    {
        context->completed.fetch_add(1u);
    }
    kiwi::test::parkWorker(context->osal);
}

} // namespace

/**
 * \brief Verify that the configured initial semaphore count is immediately consumable.
 *
 * \details
 * Creates a counting semaphore with two initial tokens, reads its count, consumes
 * one token using the indefinite wait API and reads the count again.
 *
 * Expected result:
 * - the initial count equals the creation value;
 * - one successful wait consumes exactly one token.
 */
TEST_F(OsalFreertosPosixItTest, Semaphore_InitialCount_IsAvailableAndConsumable)
{
    /* Arrange */
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    ASSERT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 3u, 2u, &semaphore), KIWI_TEST_OSAL_NO_ERR);

    KiwiTest_osalSemaphoreCount_t count = 0u;
    ASSERT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), semaphore, &count), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(count, 2u);

    /* Act */
    ASSERT_EQ(kiwiTest_osalSemaphoreWait(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    ASSERT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), semaphore, &count), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(count, 1u);

    ASSERT_EQ(kiwiTest_osalSemaphoreDelete(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify count increment and maximum-count enforcement of a real semaphore.
 *
 * \details
 * Creates a semaphore one token below its maximum, posts once to reach the
 * maximum and then attempts one additional post.
 *
 * Expected result:
 * - the first post raises the count to the configured maximum;
 * - posting beyond the maximum reports KIWI_TEST_OSAL_SEMAPHORE_POST_ERR;
 * - the count remains capped at the configured maximum.
 */
TEST_F(OsalFreertosPosixItTest, Semaphore_Post_StopsAtConfiguredMaximum)
{
    /* Arrange */
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    ASSERT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 2u, 1u, &semaphore), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    ASSERT_EQ(kiwiTest_osalSemaphorePost(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalSemaphorePost(osal(), semaphore), KIWI_TEST_OSAL_SEMAPHORE_POST_ERR);

    /* Assert */
    KiwiTest_osalSemaphoreCount_t count = 0u;
    ASSERT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), semaphore, &count), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(count, 2u);

    ASSERT_EQ(kiwiTest_osalSemaphoreDelete(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that posting a token wakes a worker blocked on an empty semaphore.
 *
 * \details
 * Starts a worker waiting indefinitely on a zero-count semaphore, confirms that
 * it remains blocked, then posts a single token.
 *
 * Expected result:
 * - the worker stays blocked while the count is zero;
 * - one post releases the worker;
 * - the released worker consumes the token and reports success.
 */
TEST_F(OsalFreertosPosixItTest, Semaphore_WaitingThread_WakesAfterPost)
{
    /* Arrange */
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    ASSERT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 1u, 0u, &semaphore), KIWI_TEST_OSAL_NO_ERR);

    SemaphoreWaitContext context{};
    context.osal = osal();
    context.semaphore = semaphore;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(semaphoreWaitWorker, &context, "sem-wait");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.started.load(); }));

    /* Verify that no token means the worker is genuinely blocked */
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));

    /* Act */
    ASSERT_EQ(kiwiTest_osalSemaphorePost(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load(); }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_NO_ERR);
    KiwiTest_osalSemaphoreCount_t count = 1u;
    ASSERT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), semaphore, &count), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(count, 0u);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalSemaphoreDelete(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify finite timeout behavior when no semaphore token becomes available.
 *
 * \details
 * Executes SemaphorePend against a zero-count semaphore and measures elapsed
 * OSAL time around the real blocking operation.
 *
 * Expected result:
 * - the wait expires with KIWI_TEST_OSAL_SEMAPHORE_WAIT_ERR;
 * - elapsed time is not shorter than the requested timeout;
 * - no semaphore token is created by the timeout path.
 */
TEST_F(OsalFreertosPosixItTest, Semaphore_PendWithoutToken_ExpiresAfterTimeout)
{
    /* Arrange */
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    ASSERT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 1u, 0u, &semaphore), KIWI_TEST_OSAL_NO_ERR);

    KiwiTest_osalTimeMs_t startMs = 0u;
    KiwiTest_osalTimeMs_t endMs = 0u;
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &startMs), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    const auto status = kiwiTest_osalSemaphorePend(osal(), semaphore, kiwi::test::kShortTimeoutMs);
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &endMs), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(status, KIWI_TEST_OSAL_SEMAPHORE_WAIT_ERR);
    EXPECT_GE(static_cast<KiwiTest_osalTimeMs_t>(endMs - startMs), kiwi::test::kShortTimeoutMs);

    KiwiTest_osalSemaphoreCount_t count = 1u;
    ASSERT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), semaphore, &count), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(count, 0u);

    ASSERT_EQ(kiwiTest_osalSemaphoreDelete(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify token accounting with two simultaneously blocked semaphore waiters.
 *
 * \details
 * Starts two workers waiting on a zero-count semaphore. Publishes one token and
 * verifies that exactly one waiter completes, then publishes a second token and
 * verifies that the remaining waiter completes.
 *
 * Expected result:
 * - one posted token releases exactly one waiter;
 * - a second token releases the remaining waiter;
 * - no token is duplicated or lost while waking multiple threads.
 */
TEST_F(OsalFreertosPosixItTest, Semaphore_MultipleWaiters_ConsumeOneTokenEach)
{
    /* Arrange */
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    ASSERT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 2u, 0u, &semaphore), KIWI_TEST_OSAL_NO_ERR);

    SemaphoreMultiWaitContext context{};
    context.osal = osal();
    context.semaphore = semaphore;

    KiwiTest_osalThreadHandle_t firstWorker = nullptr;
    KiwiTest_osalThreadHandle_t secondWorker = nullptr;
    kiwi::test::RegisteredThreadGuard firstGuard(osal());
    kiwi::test::RegisteredThreadGuard secondGuard(osal());
    const auto firstAttr = kiwi::test::workerAttr(semaphoreMultiWaitWorker, &context, "sem-wait-1");
    const auto secondAttr = kiwi::test::workerAttr(semaphoreMultiWaitWorker, &context, "sem-wait-2");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &firstWorker, firstAttr), KIWI_TEST_OSAL_NO_ERR);
    firstGuard.set(firstWorker);
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &secondWorker, secondAttr), KIWI_TEST_OSAL_NO_ERR);
    secondGuard.set(secondWorker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.started.load() == 2u; }));

    /* Act: one token must release only one waiter */
    ASSERT_EQ(kiwiTest_osalSemaphorePost(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load() >= 1u; }));
    EXPECT_EQ(context.completed.load(), 1u);
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 10u), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(context.completed.load(), 1u);

    /* Publish the second token for the remaining waiter */
    ASSERT_EQ(kiwiTest_osalSemaphorePost(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load() == 2u; }));

    /* Assert */
    EXPECT_EQ(context.completed.load(), 2u);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), firstWorker), KIWI_TEST_OSAL_NO_ERR);
    firstGuard.release();
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), secondWorker), KIWI_TEST_OSAL_NO_ERR);
    secondGuard.release();

    ASSERT_EQ(kiwiTest_osalSemaphoreDelete(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify a counting semaphore is scoped to its creating OSAL instance.
 *
 * \details Creates a real semaphore with one available token, queries it through a
 *          second OSAL instance and then repeats the query through the owning instance.
 *
 * Expected result:
 * - the foreign instance rejects the semaphore handle with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - the owner still observes the original count and can delete the semaphore.
 */
TEST_F(OsalFreertosPosixItTest, Semaphore_ForeignInstance_CannotUseOwnedHandle)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    KiwiTest_osalSemaphoreHandle_t semaphore = nullptr;
    ASSERT_EQ(kiwiTest_osalSemaphoreCreate(osal(), 2u, 1u, &semaphore), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    KiwiTest_osalSemaphoreCount_t foreignCount = 0u;
    EXPECT_EQ(kiwiTest_osalSemaphoreCountGet(secondaryOsal(), semaphore, &foreignCount),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert that the owner registry and native object remain intact */
    KiwiTest_osalSemaphoreCount_t ownerCount = 0u;
    ASSERT_EQ(kiwiTest_osalSemaphoreCountGet(osal(), semaphore, &ownerCount), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(ownerCount, 1u);
    ASSERT_EQ(kiwiTest_osalSemaphoreDelete(osal(), semaphore), KIWI_TEST_OSAL_NO_ERR);
}

