/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_freertos_posix_it_test_fixture.hpp"
#include "osal_it_test_utils.hpp"

#include <atomic>
#include <cstdint>

namespace
{

struct QueueWaitContext
{
    KiwiTest_osal_s *osal = nullptr;
    KiwiTest_osalQueueHandle_t queue = nullptr;
    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    KiwiTest_osalErr_e status = KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
    uint32_t value = 0u;
};

void queueWaitWorker(void *const args)
{
    auto *const context = static_cast<QueueWaitContext *>(args);
    context->started.store(true);
    context->status = kiwiTest_osalQueueItemWait(context->osal, context->queue, &context->value);
    context->completed.store(true);
    kiwi::test::parkWorker(context->osal);
}

} // namespace

/**
 * \brief Verify FIFO ordering and full/empty behavior of a real queue.
 *
 * \details
 * Creates a two-element queue, fills it in a known order and verifies that a
 * third non-blocking put reports a full queue. Reads both elements back and
 * verifies FIFO ordering, then confirms that a non-blocking get on the empty
 * queue reports the empty state.
 *
 * Expected result:
 * - two items are accepted in insertion order;
 * - an additional non-blocking put reports KIWI_TEST_OSAL_QUEUE_IS_FULL_ERR;
 * - reads return the original FIFO sequence;
 * - a read after draining the queue reports KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR.
 */
TEST_F(OsalFreertosPosixItTest, Queue_FifoAndBoundaryStates_ArePreserved)
{
    /* Arrange */
    KiwiTest_osalQueueHandle_t queue = nullptr;
    ASSERT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 2u, &queue), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_NE(queue, nullptr);

    const uint32_t first = 0x11223344u;
    const uint32_t second = 0x55667788u;
    const uint32_t overflow = 0xAABBCCDDu;

    /* Act: fill the queue and exercise the non-blocking full boundary */
    ASSERT_EQ(kiwiTest_osalQueueItemPut(osal(), queue, &first), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalQueueItemPut(osal(), queue, &second), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(kiwiTest_osalQueueItemPut(osal(), queue, &overflow), KIWI_TEST_OSAL_QUEUE_IS_FULL_ERR);

    uint32_t receivedFirst = 0u;
    uint32_t receivedSecond = 0u;
    uint32_t emptyRead = 0u;
    ASSERT_EQ(kiwiTest_osalQueueItemGet(osal(), queue, &receivedFirst), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalQueueItemGet(osal(), queue, &receivedSecond), KIWI_TEST_OSAL_NO_ERR);

    /* Assert: verify ordering and the final empty boundary */
    EXPECT_EQ(receivedFirst, first);
    EXPECT_EQ(receivedSecond, second);
    EXPECT_EQ(kiwiTest_osalQueueItemGet(osal(), queue, &emptyRead), KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR);

    ASSERT_EQ(kiwiTest_osalQueueDelete(osal(), queue), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that an indefinitely waiting consumer is released by a real queue put.
 *
 * \details
 * Starts a worker blocked in QueueItemWait on an empty queue. Confirms that the
 * worker remains blocked before data is published, then places one item into
 * the queue, waits for the worker to complete the receive, then deletes the
 * parked worker through the normal external thread-lifecycle path.
 *
 * Expected result:
 * - the consumer remains blocked while the queue is empty;
 * - publishing an item wakes the consumer;
 * - the consumer receives the exact published value and returns success.
 */
TEST_F(OsalFreertosPosixItTest, Queue_WaitingConsumer_WakesAfterPut)
{
    /* Arrange */
    KiwiTest_osalQueueHandle_t queue = nullptr;
    ASSERT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 1u, &queue), KIWI_TEST_OSAL_NO_ERR);

    QueueWaitContext context{};
    context.osal = osal();
    context.queue = queue;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(queueWaitWorker, &context, "queue-wait");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.started.load(); }));

    /* Verify that the consumer is genuinely blocked on the empty queue */
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));

    /* Act: publish data from the producer side */
    const uint32_t expected = 0xCAFEBABEu;
    ASSERT_EQ(kiwiTest_osalQueueItemPut(osal(), queue, &expected), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load(); }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(context.value, expected);
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalQueueDelete(osal(), queue), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify the finite receive timeout of an empty real queue.
 *
 * \details
 * Calls QueueItemPend on an empty queue with a finite timeout and measures the
 * elapsed OSAL time around the blocking operation.
 *
 * Expected result:
 * - the operation does not complete successfully without data;
 * - KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR is returned after the requested timeout;
 * - elapsed time is not shorter than the configured timeout.
 */
TEST_F(OsalFreertosPosixItTest, Queue_PendOnEmptyQueue_ExpiresAfterTimeout)
{
    /* Arrange */
    KiwiTest_osalQueueHandle_t queue = nullptr;
    ASSERT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 1u, &queue), KIWI_TEST_OSAL_NO_ERR);

    KiwiTest_osalTimeMs_t startMs = 0u;
    KiwiTest_osalTimeMs_t endMs = 0u;
    uint32_t value = 0u;
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &startMs), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    const auto status = kiwiTest_osalQueueItemPend(osal(), queue, &value, kiwi::test::kShortTimeoutMs);
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &endMs), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(status, KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR);
    EXPECT_GE(static_cast<KiwiTest_osalTimeMs_t>(endMs - startMs), kiwi::test::kShortTimeoutMs);

    ASSERT_EQ(kiwiTest_osalQueueDelete(osal(), queue), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify the finite post timeout of a full real queue.
 *
 * \details
 * Fills a single-element queue and attempts a timed QueueItemPost while no
 * consumer releases capacity.
 *
 * Expected result:
 * - the queued item remains intact;
 * - the timed post expires with KIWI_TEST_OSAL_QUEUE_OVERFLOW_ERR;
 * - the operation waits at least for the requested timeout.
 */
TEST_F(OsalFreertosPosixItTest, Queue_PostToFullQueue_ExpiresAfterTimeout)
{
    /* Arrange */
    KiwiTest_osalQueueHandle_t queue = nullptr;
    ASSERT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 1u, &queue), KIWI_TEST_OSAL_NO_ERR);
    const uint32_t queued = 0x12345678u;
    const uint32_t pending = 0x87654321u;
    ASSERT_EQ(kiwiTest_osalQueueItemPut(osal(), queue, &queued), KIWI_TEST_OSAL_NO_ERR);

    KiwiTest_osalTimeMs_t startMs = 0u;
    KiwiTest_osalTimeMs_t endMs = 0u;
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &startMs), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    const auto status = kiwiTest_osalQueueItemPost(osal(), queue, &pending, kiwi::test::kShortTimeoutMs);
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &endMs), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(status, KIWI_TEST_OSAL_QUEUE_OVERFLOW_ERR);
    EXPECT_GE(static_cast<KiwiTest_osalTimeMs_t>(endMs - startMs), kiwi::test::kShortTimeoutMs);

    uint32_t received = 0u;
    ASSERT_EQ(kiwiTest_osalQueueItemGet(osal(), queue, &received), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(received, queued);

    ASSERT_EQ(kiwiTest_osalQueueDelete(osal(), queue), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that resetting a real queue discards all queued items.
 *
 * \details
 * Places data into a queue, resets the queue, then performs a non-blocking get.
 *
 * Expected result:
 * - reset succeeds for the live queue;
 * - no pre-reset item remains available afterwards.
 */
TEST_F(OsalFreertosPosixItTest, Queue_Reset_DiscardsQueuedItems)
{
    /* Arrange */
    KiwiTest_osalQueueHandle_t queue = nullptr;
    ASSERT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 2u, &queue), KIWI_TEST_OSAL_NO_ERR);
    const uint32_t value = 0xA5A5A5A5u;
    ASSERT_EQ(kiwiTest_osalQueueItemPut(osal(), queue, &value), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    ASSERT_EQ(kiwiTest_osalQueueReset(osal(), queue), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    uint32_t received = 0u;
    EXPECT_EQ(kiwiTest_osalQueueItemGet(osal(), queue, &received), KIWI_TEST_OSAL_QUEUE_IS_EMPTY_ERR);

    ASSERT_EQ(kiwiTest_osalQueueDelete(osal(), queue), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify a queue handle remains owned by the OSAL instance that created it.
 *
 * \details Creates a real queue through the fixture instance, initializes a second
 *          independent OSAL instance and tries to operate on the first queue through
 *          that foreign instance before using the original owner again.
 *
 * Expected result:
 * - the foreign instance rejects the queue handle with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - the owning instance can still use and delete the queue afterwards.
 */
TEST_F(OsalFreertosPosixItTest, Queue_ForeignInstance_CannotUseOwnedHandle)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    KiwiTest_osalQueueHandle_t queue = nullptr;
    ASSERT_EQ(kiwiTest_osalQueueCreate(osal(), sizeof(uint32_t), 2u, &queue), KIWI_TEST_OSAL_NO_ERR);

    /* Act: present an owner-registered handle to another OSAL instance */
    EXPECT_EQ(kiwiTest_osalQueueReset(secondaryOsal(), queue), KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert that ownership rejection does not invalidate the real queue */
    const uint32_t value = 0x13579BDFu;
    uint32_t received = 0u;
    ASSERT_EQ(kiwiTest_osalQueueItemPut(osal(), queue, &value), KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalQueueItemGet(osal(), queue, &received), KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(received, value);
    ASSERT_EQ(kiwiTest_osalQueueDelete(osal(), queue), KIWI_TEST_OSAL_NO_ERR);
}

