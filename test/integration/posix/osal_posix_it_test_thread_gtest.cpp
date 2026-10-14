/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_posix_it_test_fixture.hpp"

#include <gtest/gtest.h>

#include <atomic>

namespace
{
struct ThreadWorkerContext
{
    KiwiTest_osal_s   *osal = nullptr;
    std::atomic<bool> entered{false};
    std::atomic<bool> completed{false};
};

void selfExitWorker(void *const args)
{
    auto *const context = static_cast<ThreadWorkerContext *>(args);

    context->entered.store(true);
    context->completed.store(true);
    kiwiTest_osalThreadExit(context->osal);
}


struct ParkWorkerContext
{
    KiwiTest_osal_s   *osal = nullptr;
    std::atomic<bool> started{false};
};

void parkedWorker(void *const args)
{
    auto *const context = static_cast<ParkWorkerContext *>(args);

    context->started.store(true);
    kiwi::test::parkWorker(context->osal);
}
} // namespace

/**
 * \brief Verify explicit thread yield is supported by the native POSIX backend runtime.
 *
 * \details Calls the dedicated generic ThreadYield operation from the test execution
 *          context without coupling the assertion to any scheduler-specific switch order.
 *
 * Expected result:
 * - ThreadYield completes successfully.
 */
TEST_F(OsalPosixItTest, Thread_Yield_CompletesSuccessfully)
{
    EXPECT_EQ(kiwiTest_osalThreadYield(osal()), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify zero-duration delay remains a successful no-op on native POSIX.
 *
 * \details Calls ThreadDelay with zero and verifies the backend accepts the explicit
 *          no-op contract separately from ThreadYield.
 *
 * Expected result:
 * - ThreadDelay(0) returns success.
 */
TEST_F(OsalPosixItTest, Thread_DelayZero_CompletesAsNoOp)
{
    EXPECT_EQ(kiwiTest_osalThreadDelay(osal(), 0u), KIWI_TEST_OSAL_NO_ERR);
}


/**
 * \brief Verify a real native POSIX worker executes and releases its slot through ThreadExit.
 *
 * \details Occupies one of the two generated thread slots with a parked worker, then
 *          starts a second worker that marks its execution and calls ThreadExit. A third
 *          worker is created only after the self-terminating worker releases its registry
 *          slot, so slot reuse is verified without reading backend registry storage directly.
 *
 * Expected result:
 * - the self-terminating worker executes and reaches its completion marker;
 * - a replacement worker can be created after ThreadExit releases the second slot.
 */
TEST_F(OsalPosixItTest, Thread_CreateWorker_ExecutesAndSelfTerminates)
{
    /* Arrange: occupy one thread slot for the complete scenario */
    ParkWorkerContext occupiedContext{};

    occupiedContext.osal = osal();
    KiwiTest_osalThreadHandle_t occupiedWorker = nullptr;
    kiwi::test::RegisteredThreadGuard occupiedGuard(osal());
    const auto occupiedAttr = kiwi::test::workerAttr(parkedWorker, &occupiedContext, "worker-occupied");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &occupiedWorker, occupiedAttr), KIWI_TEST_OSAL_NO_ERR);
    occupiedGuard.set(occupiedWorker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&occupiedContext]() {
        return occupiedContext.started.load();
    }));

    ThreadWorkerContext exitContext{};
    exitContext.osal = osal();
    KiwiTest_osalThreadHandle_t exitWorker = nullptr;
    const auto exitAttr                    = kiwi::test::workerAttr(selfExitWorker, &exitContext, "worker-exit");

    /* Act: fill the remaining slot with a worker that self-terminates */
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &exitWorker, exitAttr), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_NE(exitWorker, nullptr);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&exitContext]() {
        return exitContext.entered.load();
    }));
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&exitContext]() {
        return exitContext.completed.load();
    }));

    /* Wait until ThreadExit makes the second registry slot reusable */
    ParkWorkerContext replacementContext{};
    replacementContext.osal = osal();
    KiwiTest_osalThreadHandle_t replacementWorker = nullptr;
    kiwi::test::RegisteredThreadGuard replacementGuard(osal());
    const auto replacementAttr = kiwi::test::workerAttr(parkedWorker, &replacementContext, "worker-replacement");

    KiwiTest_osalErr_e createStatus = KIWI_TEST_OSAL_THREAD_CREATE_ERR;
    KiwiTest_osalTimeMs_t startMs   = 0u;
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &startMs), KIWI_TEST_OSAL_NO_ERR);

    while (createStatus == KIWI_TEST_OSAL_THREAD_CREATE_ERR)
    {
        createStatus = kiwiTest_osalThreadCreate(osal(), &replacementWorker, replacementAttr);
        if (createStatus == KIWI_TEST_OSAL_NO_ERR)
        {
            break;
        }

        KiwiTest_osalTimeMs_t nowMs = 0u;
        ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &nowMs), KIWI_TEST_OSAL_NO_ERR);
        ASSERT_TRUE(static_cast<KiwiTest_osalTimeMs_t>(nowMs - startMs) < kiwi::test::kScenarioTimeoutMs);
        ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), kiwi::test::kPollDelayMs), KIWI_TEST_OSAL_NO_ERR);
    }

    /* Assert: successful replacement creation proves slot release by ThreadExit */
    ASSERT_EQ(createStatus, KIWI_TEST_OSAL_NO_ERR);
    replacementGuard.set(replacementWorker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&replacementContext]() {
        return replacementContext.started.load();
    }));
    EXPECT_TRUE(exitContext.entered.load());
    EXPECT_TRUE(exitContext.completed.load());

    /* Cleanup the parked workers owned by the test thread */
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), replacementWorker), KIWI_TEST_OSAL_NO_ERR);
    replacementGuard.release();
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), occupiedWorker), KIWI_TEST_OSAL_NO_ERR);
    occupiedGuard.release();
}

/**
 * \brief Verify a live worker thread cannot be deleted through a foreign OSAL instance.
 *
 * \details Creates a parked worker owned by the fixture instance, attempts to delete
 *          its handle through a second initialized OSAL instance and then deletes the
 *          same worker through its real owner.
 *
 * Expected result:
 * - the foreign instance rejects the thread handle with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - the worker remains registered and can be deleted cleanly by the owner.
 */
TEST_F(OsalPosixItTest, Thread_ForeignInstance_CannotDeleteOwnedWorker)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    ParkWorkerContext context{};
    context.osal = osal();
    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(parkedWorker, &context, "owned-worker");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() {
        return context.started.load();
    }));

    /* Act */
    EXPECT_EQ(kiwiTest_osalThreadDelete(secondaryOsal(), worker), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert that the owner still controls the live worker */
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();
}
