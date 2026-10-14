/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_posix_it_test_fixture.hpp"
#include "osal_it_test_utils.hpp"

#include <atomic>
#include <cstdint>

namespace
{
struct EventWaitContext
{
    KiwiTest_osal_s                  *osal          = nullptr;
    KiwiTest_osalEventFlagsHandle_t  eventFlags     = nullptr;
    uint32_t                         requestedFlags = 0u;
    KiwiTest_osalEventFlagsOptions_e options        = KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ANY;
    KiwiTest_osalTimeMs_t            timeoutMs      = kiwi::test::kScenarioTimeoutMs;
    std::atomic<bool>                started{false};
    std::atomic<bool>                completed{false};
    KiwiTest_osalErr_e               status      = KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
    uint32_t                         actualFlags = 0u;
};

void eventWaitWorker(void *const args)
{
    auto *const context = static_cast<EventWaitContext *>(args);

    context->started.store(true);
    context->status = kiwiTest_osalEventFlagsWait(context->osal,
                                                  context->eventFlags,
                                                  context->requestedFlags,
                                                  context->options,
                                                  context->timeoutMs,
                                                  &context->actualFlags);
    context->completed.store(true);
    kiwi::test::parkWorker(context->osal);
}
} // namespace

/**
 * \brief Verify that set, get and clear operations manipulate real event-flag state.
 *
 * \details
 * Sets two bits in a newly created event-flags object, reads the native state,
 * clears one bit and reads the state again.
 *
 * Expected result:
 * - both set bits are observable after EventFlagsSet;
 * - clearing one mask removes only the selected bit;
 * - unrelated set bits remain unchanged.
 */
TEST_F(OsalPosixItTest, EventFlags_SetGetClear_PreserveExpectedBitState)
{
    /* Arrange */
    KiwiTest_osalEventFlagsHandle_t eventFlags = nullptr;

    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &eventFlags), KIWI_TEST_OSAL_NO_ERR);
    constexpr uint32_t kFirst  = 0x01u;
    constexpr uint32_t kSecond = 0x04u;

    /* Act: publish both bits and inspect real state */
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, kFirst | kSecond), KIWI_TEST_OSAL_NO_ERR);
    uint32_t flags = 0u;
    ASSERT_EQ(kiwiTest_osalEventFlagsGet(osal(), eventFlags, &flags), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(flags & (kFirst | kSecond), kFirst | kSecond);

    /* Clear only one selected flag */
    ASSERT_EQ(kiwiTest_osalEventFlagsClear(osal(), eventFlags, kFirst), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalEventFlagsGet(osal(), eventFlags, &flags), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(flags & kFirst, 0u);
    EXPECT_EQ(flags & kSecond, kSecond);

    ASSERT_EQ(kiwiTest_osalEventFlagsDelete(osal(), eventFlags), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that WAIT_ANY wakes when one requested event bit is published.
 *
 * \details
 * Starts a worker waiting for either of two bits, confirms that it remains
 * blocked initially and then sets one requested bit.
 *
 * Expected result:
 * - the waiter remains blocked before a requested bit is set;
 * - setting either requested bit releases the waiter;
 * - actualFlags contains the bit that satisfied the wait.
 */
TEST_F(OsalPosixItTest, EventFlags_WaitAny_WakesOnMatchingFlag)
{
    /* Arrange */
    KiwiTest_osalEventFlagsHandle_t eventFlags = nullptr;

    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &eventFlags), KIWI_TEST_OSAL_NO_ERR);

    EventWaitContext context{};
    context.osal           = osal();
    context.eventFlags     = eventFlags;
    context.requestedFlags = 0x01u | 0x02u;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(eventWaitWorker, &context, "flags-any");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() {
        return context.started.load();
    }));
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));

    /* Act */
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, 0x02u), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() {
        return context.completed.load();
    }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_NO_ERR);
    EXPECT_NE(context.actualFlags & 0x02u, 0u);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalEventFlagsDelete(osal(), eventFlags), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that WAIT_ALL ignores partial and unrelated flag publication.
 *
 * \details
 * Starts a worker waiting for two required bits with WAIT_ALL semantics. Sets an
 * unrelated bit and then only one requested bit, verifying that neither state
 * releases the worker. Publishes the final requested bit afterwards.
 *
 * Expected result:
 * - unrelated flags do not satisfy the waiter;
 * - a partial requested mask does not satisfy WAIT_ALL;
 * - the complete requested mask releases the waiter successfully.
 */
TEST_F(OsalPosixItTest, EventFlags_WaitAll_WakesOnlyOnCompleteMask)
{
    /* Arrange */
    KiwiTest_osalEventFlagsHandle_t eventFlags = nullptr;

    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &eventFlags), KIWI_TEST_OSAL_NO_ERR);

    EventWaitContext context{};
    context.osal           = osal();
    context.eventFlags     = eventFlags;
    context.requestedFlags = 0x01u | 0x02u;
    context.options        = KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ALL;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(eventWaitWorker, &context, "flags-all");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() {
        return context.started.load();
    }));

    /* Publish states that must not satisfy WAIT_ALL */
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, 0x04u), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, 0x01u), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));

    /* Act: complete the requested mask */
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, 0x02u), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() {
        return context.completed.load();
    }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(context.actualFlags & context.requestedFlags, context.requestedFlags);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalEventFlagsDelete(osal(), eventFlags), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify default clear-on-success behavior of EventFlagsWait.
 *
 * \details
 * Preloads a requested bit together with an unrelated bit and performs an
 * immediate WAIT_ANY using the default options. Reads the event state after the
 * successful wait.
 *
 * Expected result:
 * - the preloaded requested bit satisfies the wait immediately;
 * - the requested bit is cleared after successful consumption;
 * - the unrelated bit remains set.
 */
TEST_F(OsalPosixItTest, EventFlags_DefaultWait_ClearsConsumedMaskOnly)
{
    /* Arrange */
    KiwiTest_osalEventFlagsHandle_t eventFlags = nullptr;

    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &eventFlags), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, 0x01u | 0x04u), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    uint32_t actualFlags = 0u;
    ASSERT_EQ(kiwiTest_osalEventFlagsWait(osal(),
                                          eventFlags,
                                          0x01u,
                                          KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ANY,
                                          0u,
                                          &actualFlags),
              KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_NE(actualFlags & 0x01u, 0u);
    uint32_t remainingFlags = 0u;
    ASSERT_EQ(kiwiTest_osalEventFlagsGet(osal(), eventFlags, &remainingFlags), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(remainingFlags & 0x01u, 0u);
    EXPECT_NE(remainingFlags & 0x04u, 0u);

    ASSERT_EQ(kiwiTest_osalEventFlagsDelete(osal(), eventFlags), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that NO_CLEAR preserves flags after a successful event wait.
 *
 * \details
 * Preloads a requested mask and performs an immediate successful wait using the
 * NO_CLEAR option before reading the event state again.
 *
 * Expected result:
 * - the wait succeeds from the already satisfied state;
 * - all requested bits remain set after the wait.
 */
TEST_F(OsalPosixItTest, EventFlags_NoClear_PreservesSatisfiedFlags)
{
    /* Arrange */
    KiwiTest_osalEventFlagsHandle_t eventFlags = nullptr;

    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &eventFlags), KIWI_TEST_OSAL_NO_ERR);
    constexpr uint32_t kMask = 0x01u | 0x02u;
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, kMask), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    uint32_t actualFlags = 0u;
    const auto options   = static_cast<KiwiTest_osalEventFlagsOptions_e>(
        KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ALL | KIWI_TEST_OSAL_EVENT_FLAGS_NO_CLEAR);
    ASSERT_EQ(kiwiTest_osalEventFlagsWait(osal(), eventFlags, kMask, options, 0u, &actualFlags),
              KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(actualFlags & kMask, kMask);
    uint32_t remainingFlags = 0u;
    ASSERT_EQ(kiwiTest_osalEventFlagsGet(osal(), eventFlags, &remainingFlags), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(remainingFlags & kMask, kMask);

    ASSERT_EQ(kiwiTest_osalEventFlagsDelete(osal(), eventFlags), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify finite timeout behavior when the requested event mask never appears.
 *
 * \details
 * Waits for an absent event bit with a finite timeout while no producer changes
 * the event-flags object.
 *
 * Expected result:
 * - the wait completes with KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ERR;
 * - elapsed OSAL time is not shorter than the requested timeout;
 * - actualFlags does not falsely report the requested bit.
 */
TEST_F(OsalPosixItTest, EventFlags_WaitWithoutMatchingFlags_ExpiresAfterTimeout)
{
    /* Arrange */
    KiwiTest_osalEventFlagsHandle_t eventFlags = nullptr;

    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &eventFlags), KIWI_TEST_OSAL_NO_ERR);
    KiwiTest_osalTimeMs_t startMs = 0u;
    KiwiTest_osalTimeMs_t endMs   = 0u;
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &startMs), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    uint32_t actualFlags = 0u;
    const auto status    = kiwiTest_osalEventFlagsWait(osal(),
                                                       eventFlags,
                                                       0x08u,
                                                       KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ANY,
                                                       kiwi::test::kShortTimeoutMs,
                                                       &actualFlags);
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &endMs), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(status, KIWI_TEST_OSAL_EVENT_FLAGS_WAIT_ERR);
    EXPECT_EQ(actualFlags & 0x08u, 0u);
    EXPECT_GE(static_cast<KiwiTest_osalTimeMs_t>(endMs - startMs), kiwi::test::kShortTimeoutMs);

    ASSERT_EQ(kiwiTest_osalEventFlagsDelete(osal(), eventFlags), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify event flags cannot be observed through a foreign OSAL instance.
 *
 * \details Creates event flags through the fixture instance, sets a known bit and
 *          attempts to read the same native object through a second initialized instance.
 *
 * Expected result:
 * - the foreign instance rejects the handle with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - the owning instance still observes the configured bit state and can delete the object.
 */
TEST_F(OsalPosixItTest, EventFlags_ForeignInstance_CannotUseOwnedHandle)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    KiwiTest_osalEventFlagsHandle_t eventFlags = nullptr;
    ASSERT_EQ(kiwiTest_osalEventFlagsCreate(osal(), &eventFlags), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalEventFlagsSet(osal(), eventFlags, 0x04u), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    uint32_t foreignFlags = 0u;
    EXPECT_EQ(kiwiTest_osalEventFlagsGet(secondaryOsal(), eventFlags, &foreignFlags),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert that the owner still controls the event-flags state */
    uint32_t ownerFlags = 0u;
    ASSERT_EQ(kiwiTest_osalEventFlagsGet(osal(), eventFlags, &ownerFlags), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(ownerFlags & 0x04u, 0x04u);
    ASSERT_EQ(kiwiTest_osalEventFlagsDelete(osal(), eventFlags), KIWI_TEST_OSAL_NO_ERR);
}
