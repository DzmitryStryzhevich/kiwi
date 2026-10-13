/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_posix_it_test_fixture.hpp"
#include "osal_it_test_utils.hpp"

#include <atomic>

namespace
{

struct TimerCallbackContext
{
    std::atomic<unsigned int> count{0u};
    std::atomic<bool> parameterObserved{false};
};

void timerCallback(void *const timerParam)
{
    auto *const context = static_cast<TimerCallbackContext *>(timerParam);
    if (context != nullptr)
    {
        context->parameterObserved.store(true);
        context->count.fetch_add(1u);
    }
}

KiwiTest_osalSoftwareTimerAttr_s timerAttr(TimerCallbackContext *const context,
                                           const KiwiTest_osalTimeMs_t periodMs,
                                           const bool autoReload,
                                           const char *const name)
{
    KiwiTest_osalSoftwareTimerAttr_s attr = {};
    attr.name = name;
    attr.timerParam = context;
    attr.timerExpiredCb = timerCallback;
    attr.autoReload = autoReload;
    attr.periodMs = periodMs;

    return attr;
}

} // namespace

/**
 * \brief Verify that a real one-shot software timer expires exactly once.
 *
 * \details
 * Starts a one-shot timer, waits for its first callback and then keeps the test
 * thread alive for several additional timer periods.
 *
 * Expected result:
 * - one callback is delivered within the scenario timeout;
 * - the callback receives the configured timer parameter;
 * - no additional callback is generated after the one-shot expiration.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_OneShot_ExpiresExactlyOnce)
{
    /* Arrange */
    TimerCallbackContext context{};
    KiwiTest_osalSoftwareTimerHandle_t timer = nullptr;
    constexpr KiwiTest_osalTimeMs_t kPeriodMs = 30u;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &timer,
                                               timerAttr(&context, kPeriodMs, false, "one-shot")),
              KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.count.load() >= 1u; }));
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 3u * kPeriodMs), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(context.count.load(), 1u);
    EXPECT_TRUE(context.parameterObserved.load());

    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify periodic auto-reload behavior and stopping of a real software timer.
 *
 * \details
 * Starts an auto-reload timer and waits until several expirations are observed.
 * Stops the timer, allows any callback already dispatched at the stop boundary
 * to settle, then verifies that the callback count remains stable afterwards.
 *
 * Expected result:
 * - the timer produces repeated callbacks while running;
 * - Stop succeeds for the running periodic timer;
 * - no further expirations occur after the stopped state has settled.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_PeriodicTimer_RepeatsUntilStopped)
{
    /* Arrange */
    TimerCallbackContext context{};
    KiwiTest_osalSoftwareTimerHandle_t timer = nullptr;
    constexpr KiwiTest_osalTimeMs_t kPeriodMs = 20u;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &timer,
                                               timerAttr(&context, kPeriodMs, true, "periodic")),
              KIWI_TEST_OSAL_NO_ERR);

    /* Act: observe repeated expiration */
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.count.load() >= 3u; }));
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStop(osal(), timer), KIWI_TEST_OSAL_NO_ERR);

    /* Allow an already dispatched callback at the stop boundary to complete */
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), kPeriodMs), KIWI_TEST_OSAL_NO_ERR);
    const unsigned int countAfterStop = context.count.load();
    ASSERT_GE(countAfterStop, 3u);
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 3u * kPeriodMs), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(context.count.load(), countAfterStop);

    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that stopping a one-shot timer prevents its pending expiration.
 *
 * \details
 * Starts a timer with a comfortably long period, stops it well before the
 * expiration point and waits beyond the original period.
 *
 * Expected result:
 * - Stop succeeds before expiration;
 * - the configured callback is never invoked after the timer is disarmed.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_Stop_PreventsPendingExpiration)
{
    /* Arrange */
    TimerCallbackContext context{};
    KiwiTest_osalSoftwareTimerHandle_t timer = nullptr;
    constexpr KiwiTest_osalTimeMs_t kPeriodMs = 100u;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &timer,
                                               timerAttr(&context, kPeriodMs, false, "stop")),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), timer), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 20u), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStop(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), kPeriodMs + 30u), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(context.count.load(), 0u);

    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that Reset restarts a running timer from a new full period.
 *
 * \details
 * Starts a one-shot timer, waits for part of its period and resets it before the
 * original expiration. Waits beyond the original expiration point while still
 * remaining before the new reset-relative expiration, then waits for the real
 * callback from the restarted period.
 *
 * Expected result:
 * - the original expiration schedule is cancelled by Reset;
 * - no callback occurs at the old expiration point;
 * - exactly one callback occurs after a new full period measured from Reset.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_Reset_RestartsFullPeriod)
{
    /* Arrange */
    TimerCallbackContext context{};
    KiwiTest_osalSoftwareTimerHandle_t timer = nullptr;
    constexpr KiwiTest_osalTimeMs_t kPeriodMs = 120u;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &timer,
                                               timerAttr(&context, kPeriodMs, false, "reset")),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), timer), KIWI_TEST_OSAL_NO_ERR);

    /* Act: move the expiration point forward before the original one is reached */
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 50u), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerReset(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 80u), KIWI_TEST_OSAL_NO_ERR);

    /* Verify that the old expiration point did not fire */
    EXPECT_EQ(context.count.load(), 0u);

    /* Wait for the reset-relative expiration */
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.count.load() >= 1u; }));
    EXPECT_EQ(context.count.load(), 1u);

    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that a stopped one-shot timer can be started again.
 *
 * \details
 * Starts and stops a timer before expiration, confirms the stopped interval is
 * quiet, then starts the same timer handle again and waits for expiration.
 *
 * Expected result:
 * - no callback occurs during the stopped interval;
 * - restarting the same timer schedules a fresh expiration;
 * - one callback is delivered after restart.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_RestartAfterStop_ExpiresAgain)
{
    /* Arrange */
    TimerCallbackContext context{};
    KiwiTest_osalSoftwareTimerHandle_t timer = nullptr;
    constexpr KiwiTest_osalTimeMs_t kPeriodMs = 60u;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &timer,
                                               timerAttr(&context, kPeriodMs, false, "restart")),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 15u), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStop(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), kPeriodMs + 20u), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(context.count.load(), 0u);

    /* Act */
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.count.load() >= 1u; }));

    /* Assert */
    EXPECT_EQ(context.count.load(), 1u);
    EXPECT_TRUE(context.parameterObserved.load());

    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that two real software timers expire independently.
 *
 * \details
 * Creates both available timer objects with different callback contexts and
 * periods, starts both and waits until each context observes its own expiration.
 *
 * Expected result:
 * - each timer invokes only its configured callback context;
 * - both one-shot timers expire once without interfering with each other.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_IndependentTimers_DoNotInterfere)
{
    /* Arrange */
    TimerCallbackContext firstContext{};
    TimerCallbackContext secondContext{};
    KiwiTest_osalSoftwareTimerHandle_t firstTimer = nullptr;
    KiwiTest_osalSoftwareTimerHandle_t secondTimer = nullptr;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &firstTimer,
                                               timerAttr(&firstContext, 30u, false, "timer-a")),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &secondTimer,
                                               timerAttr(&secondContext, 50u, false, "timer-b")),
              KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), firstTimer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), secondTimer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(),
                                      [&firstContext, &secondContext]()
                                      {
                                          return (firstContext.count.load() >= 1u) &&
                                                 (secondContext.count.load() >= 1u);
                                      }));

    /* Assert */
    EXPECT_EQ(firstContext.count.load(), 1u);
    EXPECT_EQ(secondContext.count.load(), 1u);
    EXPECT_TRUE(firstContext.parameterObserved.load());
    EXPECT_TRUE(secondContext.parameterObserved.load());

    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), firstTimer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), secondTimer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that deleting a timer before expiration prevents future callback delivery.
 *
 * \details
 * Starts a long one-shot timer, deletes it well before its expiration point and
 * waits beyond the original period.
 *
 * Expected result:
 * - deletion succeeds for the armed timer;
 * - no callback is observed after the deleted timer's former expiration point.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_DeleteBeforeExpiration_PreventsFutureCallback)
{
    /* Arrange */
    TimerCallbackContext context{};
    KiwiTest_osalSoftwareTimerHandle_t timer = nullptr;
    constexpr KiwiTest_osalTimeMs_t kPeriodMs = 100u;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &timer,
                                               timerAttr(&context, kPeriodMs, false, "delete")),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerStart(osal(), timer), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), 20u), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalThreadDelay(osal(), kPeriodMs + 30u), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(context.count.load(), 0u);
}

/**
 * \brief Verify a software-timer handle is owned by the OSAL instance that created it.
 *
 * \details Creates a stopped timer through the fixture instance and attempts to
 *          start that timer through a second initialized OSAL instance before deleting
 *          it through its original owner.
 *
 * Expected result:
 * - the foreign instance rejects the timer handle with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - ownership rejection does not prevent the owner from deleting the timer cleanly.
 */
TEST_F(OsalPosixItTest, SoftwareTimer_ForeignInstance_CannotUseOwnedHandle)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    TimerCallbackContext context{};
    KiwiTest_osalSoftwareTimerHandle_t timer = nullptr;
    ASSERT_EQ(kiwiTest_osalSoftwareTimerCreate(osal(),
                                               &timer,
                                               timerAttr(&context, 50u, false, "ownership")),
              KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    EXPECT_EQ(kiwiTest_osalSoftwareTimerStart(secondaryOsal(), timer),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert */
    EXPECT_EQ(context.count.load(), 0u);
    ASSERT_EQ(kiwiTest_osalSoftwareTimerDelete(osal(), timer), KIWI_TEST_OSAL_NO_ERR);
}

