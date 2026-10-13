/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_posix_it_test_fixture.hpp"
#include "osal_it_test_utils.hpp"

#include <atomic>

namespace
{

struct MutexWorkerContext
{
    KiwiTest_osal_s *osal = nullptr;
    KiwiTest_osalMutexHandle_t mutex = nullptr;
    KiwiTest_osalTimeMs_t timeoutMs = 0u;
    bool timed = false;
    bool unlockAfterAcquire = false;
    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    KiwiTest_osalErr_e status = KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
};

void mutexWorker(void *const args)
{
    auto *const context = static_cast<MutexWorkerContext *>(args);
    context->started.store(true);
    context->status = context->timed
                    ? kiwiTest_osalMutexPendLock(context->osal, context->mutex, context->timeoutMs)
                    : kiwiTest_osalMutexTryLock(context->osal, context->mutex);

    if ((context->status == KIWI_TEST_OSAL_NO_ERR) && context->unlockAfterAcquire)
    {
        (void)kiwiTest_osalMutexUnlock(context->osal, context->mutex);
    }

    context->completed.store(true);
    kiwi::test::parkWorker(context->osal);
}

} // namespace

/**
 * \brief Verify recursive ownership required by the generic mutex contract.
 *
 * \details
 * Acquires the same real mutex twice from the owning test thread and releases
 * it with the matching number of unlock operations.
 *
 * Expected result:
 * - both recursive lock operations succeed without deadlock;
 * - both matching unlock operations succeed.
 */
TEST_F(OsalPosixItTest, Mutex_RecursiveLocking_RequiresMatchingUnlocks)
{
    /* Arrange */
    KiwiTest_osalMutexHandle_t mutex = nullptr;
    ASSERT_EQ(kiwiTest_osalMutexCreate(osal(), &mutex), KIWI_TEST_OSAL_NO_ERR);

    /* Act and Assert */
    EXPECT_EQ(kiwiTest_osalMutexLock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalMutexLock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalMutexUnlock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalMutexUnlock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);

    ASSERT_EQ(kiwiTest_osalMutexDelete(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that TryLock does not steal a mutex owned by another thread.
 *
 * \details
 * Locks the mutex in the test thread, then starts a worker that attempts a
 * non-blocking TryLock against the same object.
 *
 * Expected result:
 * - the worker completes without blocking;
 * - TryLock reports KIWI_TEST_OSAL_MUTEX_LOCK_ERR while the mutex is owned;
 * - the original owner can still unlock the mutex normally.
 */
TEST_F(OsalPosixItTest, Mutex_TryLockDuringContention_FailsWithoutBlocking)
{
    /* Arrange */
    KiwiTest_osalMutexHandle_t mutex = nullptr;
    ASSERT_EQ(kiwiTest_osalMutexCreate(osal(), &mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalMutexLock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);

    MutexWorkerContext context{};
    context.osal = osal();
    context.mutex = mutex;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(mutexWorker, &context, "mutex-try");

    /* Act */
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load(); }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_MUTEX_LOCK_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalMutexUnlock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalMutexDelete(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that a timed mutex waiter resumes after the owner unlocks.
 *
 * \details
 * Locks the mutex in the test thread and starts a worker using PendLock with a
 * finite scenario timeout. Confirms that the worker remains blocked while the
 * mutex is owned, then unlocks it and waits for successful acquisition.
 *
 * Expected result:
 * - the worker stays blocked while the original owner holds the mutex;
 * - unlocking wakes the worker before its timeout expires;
 * - the worker acquires and releases the mutex successfully.
 */
TEST_F(OsalPosixItTest, Mutex_PendLock_WakesAfterOwnerUnlocks)
{
    /* Arrange */
    KiwiTest_osalMutexHandle_t mutex = nullptr;
    ASSERT_EQ(kiwiTest_osalMutexCreate(osal(), &mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalMutexLock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);

    MutexWorkerContext context{};
    context.osal = osal();
    context.mutex = mutex;
    context.timeoutMs = kiwi::test::kScenarioTimeoutMs;
    context.timed = true;
    context.unlockAfterAcquire = true;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(mutexWorker, &context, "mutex-pend");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.started.load(); }));

    /* Verify real contention before releasing ownership */
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));

    /* Act */
    ASSERT_EQ(kiwiTest_osalMutexUnlock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load(); }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalMutexDelete(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that PendLock reports timeout while another thread keeps ownership.
 *
 * \details
 * Holds the real mutex in the test thread and starts a worker with a short
 * finite PendLock timeout without releasing the mutex during the wait.
 *
 * Expected result:
 * - the worker cannot acquire the owned mutex;
 * - PendLock completes with KIWI_TEST_OSAL_MUTEX_LOCK_ERR after the timeout.
 */
TEST_F(OsalPosixItTest, Mutex_PendLock_ExpiresDuringContention)
{
    /* Arrange */
    KiwiTest_osalMutexHandle_t mutex = nullptr;
    ASSERT_EQ(kiwiTest_osalMutexCreate(osal(), &mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalMutexLock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);

    MutexWorkerContext context{};
    context.osal = osal();
    context.mutex = mutex;
    context.timeoutMs = kiwi::test::kShortTimeoutMs;
    context.timed = true;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(mutexWorker, &context, "mutex-timeout");

    /* Act */
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load(); }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_MUTEX_LOCK_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalMutexUnlock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalMutexDelete(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify a mutex handle cannot be used through a different OSAL instance.
 *
 * \details Creates a real mutex through the fixture instance and passes its handle
 *          to a second initialized OSAL instance before exercising the same mutex
 *          through its original owner.
 *
 * Expected result:
 * - the foreign instance rejects the mutex handle with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - the owner can still lock, unlock and delete the mutex successfully.
 */
TEST_F(OsalPosixItTest, Mutex_ForeignInstance_CannotUseOwnedHandle)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    KiwiTest_osalMutexHandle_t mutex = nullptr;
    ASSERT_EQ(kiwiTest_osalMutexCreate(osal(), &mutex), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    EXPECT_EQ(kiwiTest_osalMutexTryLock(secondaryOsal(), mutex), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert that the owner retains a valid native mutex */
    ASSERT_EQ(kiwiTest_osalMutexLock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalMutexUnlock(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalMutexDelete(osal(), mutex), KIWI_TEST_OSAL_NO_ERR);
}

