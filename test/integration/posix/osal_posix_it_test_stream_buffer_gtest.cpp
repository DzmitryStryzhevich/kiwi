/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "osal_posix_it_test_fixture.hpp"
#include "osal_it_test_utils.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace
{

struct StreamReadContext
{
    KiwiTest_osal_s *osal = nullptr;
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    KiwiTest_osalErr_e status = KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
    std::array<uint8_t, 8u> data{};
    size_t bytesGet = 0u;
};

void streamWaitWorker(void *const args)
{
    auto *const context = static_cast<StreamReadContext *>(args);
    context->started.store(true);
    context->status = kiwiTest_osalStreamBufferWait(context->osal,
                                                     context->streamBuffer,
                                                     context->data.data(),
                                                     context->data.size(),
                                                     &context->bytesGet);
    context->completed.store(true);
    kiwi::test::parkWorker(context->osal);
}

struct StreamWriteContext
{
    KiwiTest_osal_s *osal = nullptr;
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    std::array<uint8_t, 4u> data{5u, 6u, 7u, 8u};
    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    KiwiTest_osalErr_e status = KIWI_TEST_OSAL_PORT_SPECIFIC_ERR;
    size_t bytesPut = 0u;
};

void streamPostWorker(void *const args)
{
    auto *const context = static_cast<StreamWriteContext *>(args);
    context->started.store(true);
    context->status = kiwiTest_osalStreamBufferPost(context->osal,
                                                     context->streamBuffer,
                                                     context->data.data(),
                                                     context->data.size(),
                                                     kiwi::test::kScenarioTimeoutMs,
                                                     &context->bytesPut);
    context->completed.store(true);
    kiwi::test::parkWorker(context->osal);
}

} // namespace

/**
 * \brief Verify byte ordering and partial non-blocking transfers of a real stream buffer.
 *
 * \details
 * Writes more data than the configured stream-buffer capacity, reads part of the
 * accepted data and then drains the remainder. Exercises both partial write and
 * partial read behavior without blocking.
 *
 * Expected result:
 * - Put accepts as many bytes as available capacity permits;
 * - another Put against a full buffer reports KIWI_TEST_OSAL_STREAM_BUFFER_IS_FULL_ERR;
 * - Get returns bytes in original stream order across partial reads.
 */
TEST_F(OsalPosixItTest, StreamBuffer_NonBlockingTransfers_PreserveOrderAndAllowPartialProgress)
{
    /* Arrange */
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 4u, 1u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);
    const std::array<uint8_t, 6u> source{1u, 2u, 3u, 4u, 5u, 6u};

    /* Act: fill the finite buffer with a larger source sequence */
    size_t bytesPut = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, source.data(), source.size(), &bytesPut),
              KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(bytesPut, 4u);

    size_t fullPut = 0u;
    EXPECT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, source.data(), 1u, &fullPut),
              KIWI_TEST_OSAL_STREAM_BUFFER_IS_FULL_ERR);
    EXPECT_EQ(fullPut, 0u);

    /* Read the stream in chunks smaller than the buffered payload */
    std::array<uint8_t, 3u> firstRead{};
    size_t firstBytes = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(),
                                           streamBuffer,
                                           firstRead.data(),
                                           firstRead.size(),
                                           &firstBytes),
              KIWI_TEST_OSAL_NO_ERR);

    std::array<uint8_t, 3u> secondRead{};
    size_t secondBytes = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(),
                                           streamBuffer,
                                           secondRead.data(),
                                           secondRead.size(),
                                           &secondBytes),
              KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(firstBytes, 3u);
    EXPECT_EQ((std::array<uint8_t, 3u>{firstRead[0], firstRead[1], firstRead[2]}),
              (std::array<uint8_t, 3u>{1u, 2u, 3u}));
    EXPECT_EQ(secondBytes, 1u);
    EXPECT_EQ(secondRead[0], 4u);

    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that an empty non-blocking stream read reports the empty state.
 *
 * \details
 * Performs StreamBufferGet immediately after creating an empty stream buffer.
 *
 * Expected result:
 * - no output bytes are produced;
 * - KIWI_TEST_OSAL_STREAM_BUFFER_IS_EMPTY_ERR is returned.
 */
TEST_F(OsalPosixItTest, StreamBuffer_GetFromEmptyBuffer_ReportsEmptyState)
{
    /* Arrange */
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 1u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    uint8_t data = 0u;
    size_t bytesGet = 1u;
    const auto status = kiwiTest_osalStreamBufferGet(osal(), streamBuffer, &data, 1u, &bytesGet);

    /* Assert */
    EXPECT_EQ(status, KIWI_TEST_OSAL_STREAM_BUFFER_IS_EMPTY_ERR);
    EXPECT_EQ(bytesGet, 0u);

    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that an empty waiting reader observes the configured trigger level.
 *
 * \details
 * Starts a worker in StreamBufferWait on an empty buffer with trigger level four.
 * Publishes only three bytes and verifies that the worker remains blocked, then
 * publishes a fourth byte to reach the trigger threshold.
 *
 * Expected result:
 * - data below the trigger level does not release the initially blocked reader;
 * - reaching the trigger level releases the reader;
 * - the reader receives the complete four-byte sequence in order.
 */
TEST_F(OsalPosixItTest, StreamBuffer_WaitingReader_WakesWhenTriggerLevelIsReached)
{
    /* Arrange */
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 4u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);

    StreamReadContext context{};
    context.osal = osal();
    context.streamBuffer = streamBuffer;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(streamWaitWorker, &context, "stream-reader");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.started.load(); }));

    /* Publish less than the trigger threshold */
    const std::array<uint8_t, 3u> belowTrigger{1u, 2u, 3u};
    size_t bytesPut = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(),
                                           streamBuffer,
                                           belowTrigger.data(),
                                           belowTrigger.size(),
                                           &bytesPut),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(bytesPut, belowTrigger.size());
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));

    /* Act: reach the configured trigger level */
    const uint8_t triggerByte = 4u;
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, &triggerByte, 1u, &bytesPut),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load(); }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(context.bytesGet, 4u);
    EXPECT_EQ((std::array<uint8_t, 4u>{context.data[0], context.data[1], context.data[2], context.data[3]}),
              (std::array<uint8_t, 4u>{1u, 2u, 3u, 4u}));
    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();

    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that a timed writer waits for enough stream-buffer capacity.
 *
 * \details
 * Prefills a six-byte stream buffer with four bytes, then starts a worker posting
 * four additional bytes. Confirms the writer remains blocked with only two free
 * bytes, reads two bytes to provide the required capacity, then verifies the
 * complete remaining stream.
 *
 * Expected result:
 * - the writer blocks while available capacity is below its requested transfer;
 * - freeing enough capacity releases the writer before timeout;
 * - original and newly posted data remain correctly ordered.
 */
TEST_F(OsalPosixItTest, StreamBuffer_Post_WakesAfterReaderProvidesRequiredCapacity)
{
    /* Arrange */
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 6u, 1u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);
    const std::array<uint8_t, 4u> initial{1u, 2u, 3u, 4u};
    size_t bytesPut = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, initial.data(), initial.size(), &bytesPut),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(bytesPut, initial.size());

    StreamWriteContext context{};
    context.osal = osal();
    context.streamBuffer = streamBuffer;

    KiwiTest_osalThreadHandle_t worker = nullptr;
    kiwi::test::RegisteredThreadGuard workerGuard(osal());
    const auto attr = kiwi::test::workerAttr(streamPostWorker, &context, "stream-writer");
    ASSERT_EQ(kiwiTest_osalThreadCreate(osal(), &worker, attr), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.set(worker);
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.started.load(); }));
    EXPECT_TRUE(kiwi::test::staysFalseFor(osal(), context.completed, 10u));

    /* Act: release exactly enough capacity for the pending write */
    std::array<uint8_t, 2u> consumed{};
    size_t bytesGet = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(), streamBuffer, consumed.data(), consumed.size(), &bytesGet),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(bytesGet, consumed.size());
    ASSERT_TRUE(kiwi::test::waitUntil(osal(), [&context]() { return context.completed.load(); }));

    /* Assert */
    EXPECT_EQ(context.status, KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(context.bytesPut, context.data.size());
    EXPECT_EQ(consumed, (std::array<uint8_t, 2u>{1u, 2u}));

    std::array<uint8_t, 6u> remaining{};
    size_t remainingBytes = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(),
                                           streamBuffer,
                                           remaining.data(),
                                           remaining.size(),
                                           &remainingBytes),
              KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(remainingBytes, remaining.size());
    EXPECT_EQ(remaining, (std::array<uint8_t, 6u>{3u, 4u, 5u, 6u, 7u, 8u}));

    ASSERT_EQ(kiwiTest_osalThreadDelete(osal(), worker), KIWI_TEST_OSAL_NO_ERR);
    workerGuard.release();
    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify finite timeout behavior of a real empty stream-buffer pend operation.
 *
 * \details
 * Calls StreamBufferPend with a finite timeout while no producer writes data and
 * measures elapsed OSAL time.
 *
 * Expected result:
 * - no bytes are returned;
 * - KIWI_TEST_OSAL_STREAM_BUFFER_IS_EMPTY_ERR is reported;
 * - elapsed time is not shorter than the requested timeout.
 */
TEST_F(OsalPosixItTest, StreamBuffer_PendOnEmptyBuffer_ExpiresAfterTimeout)
{
    /* Arrange */
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 1u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);
    std::array<uint8_t, 4u> data{};
    size_t bytesGet = 1u;
    KiwiTest_osalTimeMs_t startMs = 0u;
    KiwiTest_osalTimeMs_t endMs = 0u;
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &startMs), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    const auto status = kiwiTest_osalStreamBufferPend(osal(),
                                                      streamBuffer,
                                                      data.data(),
                                                      data.size(),
                                                      kiwi::test::kShortTimeoutMs,
                                                      &bytesGet);
    ASSERT_EQ(kiwiTest_osalTimeMsGet(osal(), &endMs), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(status, KIWI_TEST_OSAL_STREAM_BUFFER_IS_EMPTY_ERR);
    EXPECT_EQ(bytesGet, 0u);
    EXPECT_GE(static_cast<KiwiTest_osalTimeMs_t>(endMs - startMs), kiwi::test::kShortTimeoutMs);

    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify byte ordering when the native ring storage wraps around.
 *
 * \details
 * Writes six bytes to an eight-byte buffer, consumes four bytes to advance the
 * read index, then writes six more bytes so the write index crosses the end of
 * the underlying storage. Drains the resulting eight-byte stream afterwards.
 *
 * Expected result:
 * - wrap-around does not lose or duplicate bytes;
 * - the final stream contains the two unread old bytes followed by all six new bytes.
 */
TEST_F(OsalPosixItTest, StreamBuffer_WrapAround_PreservesCompleteByteSequence)
{
    /* Arrange */
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 1u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);
    const std::array<uint8_t, 6u> first{0u, 1u, 2u, 3u, 4u, 5u};
    const std::array<uint8_t, 6u> second{6u, 7u, 8u, 9u, 10u, 11u};
    size_t bytes = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, first.data(), first.size(), &bytes),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(bytes, first.size());

    std::array<uint8_t, 4u> discarded{};
    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(), streamBuffer, discarded.data(), discarded.size(), &bytes),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(bytes, discarded.size());

    /* Act: force the write index through the end of the ring */
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, second.data(), second.size(), &bytes),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(bytes, second.size());

    std::array<uint8_t, 8u> received{};
    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(), streamBuffer, received.data(), received.size(), &bytes),
              KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    EXPECT_EQ(bytes, received.size());
    EXPECT_EQ(received, (std::array<uint8_t, 8u>{4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u}));

    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify that reset discards buffered stream data and restores the empty state.
 *
 * \details
 * Writes a payload into a live stream buffer, resets the object and immediately
 * attempts a non-blocking read.
 *
 * Expected result:
 * - reset succeeds while the buffer is idle;
 * - pre-reset bytes are no longer observable;
 * - the following read reports the empty state.
 */
TEST_F(OsalPosixItTest, StreamBuffer_Reset_DiscardsBufferedData)
{
    /* Arrange */
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 1u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);
    const std::array<uint8_t, 3u> payload{1u, 2u, 3u};
    size_t bytesPut = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, payload.data(), payload.size(), &bytesPut),
              KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    ASSERT_EQ(kiwiTest_osalStreamBufferReset(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);

    /* Assert */
    std::array<uint8_t, 3u> received{};
    size_t bytesGet = 1u;
    EXPECT_EQ(kiwiTest_osalStreamBufferGet(osal(), streamBuffer, received.data(), received.size(), &bytesGet),
              KIWI_TEST_OSAL_STREAM_BUFFER_IS_EMPTY_ERR);
    EXPECT_EQ(bytesGet, 0u);

    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

/**
 * \brief Verify a stream-buffer handle is private to its creating OSAL instance.
 *
 * \details Creates a real stream buffer through the fixture instance and attempts
 *          to reset it through a second initialized OSAL instance before transferring
 *          data through the original owner.
 *
 * Expected result:
 * - the foreign reset is rejected with KIWI_TEST_OSAL_INVALID_ARGS_ERR;
 * - the owning instance can still transfer the original byte sequence and delete the buffer.
 */
TEST_F(OsalPosixItTest, StreamBuffer_ForeignInstance_CannotUseOwnedHandle)
{
    /* Arrange */
    ASSERT_TRUE(initializeSecondaryOsal());
    KiwiTest_osalStreamBufferHandle_t streamBuffer = nullptr;
    ASSERT_EQ(kiwiTest_osalStreamBufferCreate(osal(), 8u, 1u, &streamBuffer), KIWI_TEST_OSAL_NO_ERR);

    /* Act */
    EXPECT_EQ(kiwiTest_osalStreamBufferReset(secondaryOsal(), streamBuffer),
              KIWI_TEST_OSAL_INVALID_ARGS_ERR);

    /* Assert that the owner still has a functioning stream */
    const std::array<uint8_t, 3u> payload{0x11u, 0x22u, 0x33u};
    std::array<uint8_t, 3u> received{};
    size_t bytesPut = 0u;
    size_t bytesGet = 0u;
    ASSERT_EQ(kiwiTest_osalStreamBufferPut(osal(), streamBuffer, payload.data(), payload.size(), &bytesPut),
              KIWI_TEST_OSAL_NO_ERR);
    ASSERT_EQ(kiwiTest_osalStreamBufferGet(osal(), streamBuffer, received.data(), received.size(), &bytesGet),
              KIWI_TEST_OSAL_NO_ERR);
    EXPECT_EQ(bytesPut, payload.size());
    EXPECT_EQ(bytesGet, payload.size());
    EXPECT_EQ(received, payload);
    ASSERT_EQ(kiwiTest_osalStreamBufferDelete(osal(), streamBuffer), KIWI_TEST_OSAL_NO_ERR);
}

