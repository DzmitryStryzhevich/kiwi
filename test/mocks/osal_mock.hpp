/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef KIWI_TEST_OSAL_MOCK_HPP_
#define KIWI_TEST_OSAL_MOCK_HPP_

#include <gmock/gmock.h>

extern "C"
{
#include "kiwi_test_osal.h"
}

extern const KiwiTest_osalVtable_s kOsalMockVtable;

class OsalMock
{
public:
    MOCK_METHOD(KiwiTest_osalErr_e, queueCreate,
                (void *const osal,
                 const size_t queueItemSize,
                 const size_t queueDepth,
                 KiwiTest_osalQueueHandle_t *const queueHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, queueDelete,
                (void *const osal,
                 const KiwiTest_osalQueueHandle_t queueHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, queueItemPut,
                (void *const osal,
                 const KiwiTest_osalQueueHandle_t queueHandle,
                 const void *const queueItemPtr));
    MOCK_METHOD(KiwiTest_osalErr_e, queueItemPost,
                (void *const osal,
                 const KiwiTest_osalQueueHandle_t queueHandle,
                 const void *const queueItemPtr,
                 const KiwiTest_osalTimeMs_t timeoutMs));
    MOCK_METHOD(KiwiTest_osalErr_e, queueItemGet,
                (void *const osal,
                 const KiwiTest_osalQueueHandle_t queueHandle,
                 void *const queueItemPtr));
    MOCK_METHOD(KiwiTest_osalErr_e, queueItemWait,
                (void *const osal,
                 const KiwiTest_osalQueueHandle_t queueHandle,
                 void *const queueItemPtr));
    MOCK_METHOD(KiwiTest_osalErr_e, queueItemPend,
                (void *const osal,
                 const KiwiTest_osalQueueHandle_t queueHandle,
                 void *const queueItemPtr,
                 const KiwiTest_osalTimeMs_t timeoutMs));
    MOCK_METHOD(KiwiTest_osalErr_e, queueReset,
                (void *const osal,
                 const KiwiTest_osalQueueHandle_t queueHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferCreate,
                (void *const osal,
                 const size_t bufferSizeBytes,
                 const size_t triggerLevelBytes,
                 KiwiTest_osalStreamBufferHandle_t *const streamBufferHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferDelete,
                (void *const osal,
                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferPut,
                (void *const osal,
                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                 const void *const data,
                 const size_t dataLengthBytes,
                 size_t *const bytesPut));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferPost,
                (void *const osal,
                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                 const void *const data,
                 const size_t dataLengthBytes,
                 const KiwiTest_osalTimeMs_t timeoutMs,
                 size_t *const bytesPut));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferGet,
                (void *const osal,
                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                 void *const data,
                 const size_t dataLengthBytes,
                 size_t *const bytesGet));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferWait,
                (void *const osal,
                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                 void *const data,
                 const size_t dataLengthBytes,
                 size_t *const bytesGet));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferPend,
                (void *const osal,
                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                 void *const data,
                 const size_t dataLengthBytes,
                 const KiwiTest_osalTimeMs_t timeoutMs,
                 size_t *const bytesGet));
    MOCK_METHOD(KiwiTest_osalErr_e, streamBufferReset,
                (void *const osal,
                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, mutexCreate,
                (void *const osal,
                 KiwiTest_osalMutexHandle_t *const mutexHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, mutexDelete,
                (void *const osal,
                 const KiwiTest_osalMutexHandle_t mutexHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, mutexLock,
                (void *const osal,
                 const KiwiTest_osalMutexHandle_t mutexHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, mutexTryLock,
                (void *const osal,
                 const KiwiTest_osalMutexHandle_t mutexHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, mutexPendLock,
                (void *const osal,
                 const KiwiTest_osalMutexHandle_t mutexHandle,
                 const KiwiTest_osalTimeMs_t timeoutMs));
    MOCK_METHOD(KiwiTest_osalErr_e, mutexUnlock,
                (void *const osal,
                 const KiwiTest_osalMutexHandle_t mutexHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, semaphoreCreate,
                (void *const osal,
                 const KiwiTest_osalSemaphoreCount_t maxCount,
                 const KiwiTest_osalSemaphoreCount_t initialCount,
                 KiwiTest_osalSemaphoreHandle_t *const semaphoreHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, semaphoreDelete,
                (void *const osal,
                 const KiwiTest_osalSemaphoreHandle_t semaphoreHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, semaphoreWait,
                (void *const osal,
                 const KiwiTest_osalSemaphoreHandle_t semaphoreHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, semaphorePend,
                (void *const osal,
                 const KiwiTest_osalSemaphoreHandle_t semaphoreHandle,
                 const KiwiTest_osalTimeMs_t timeoutMs));
    MOCK_METHOD(KiwiTest_osalErr_e, semaphorePost,
                (void *const osal,
                 const KiwiTest_osalSemaphoreHandle_t semaphoreHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, semaphoreCountGet,
                (void *const osal,
                 const KiwiTest_osalSemaphoreHandle_t semaphoreHandle,
                 KiwiTest_osalSemaphoreCount_t *const semaphoreCount));
    MOCK_METHOD(KiwiTest_osalErr_e, eventFlagsCreate,
                (void *const osal,
                 KiwiTest_osalEventFlagsHandle_t *const eventFlagsHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, eventFlagsDelete,
                (void *const osal,
                 const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, eventFlagsSet,
                (void *const osal,
                 const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                 const uint32_t flags));
    MOCK_METHOD(KiwiTest_osalErr_e, eventFlagsClear,
                (void *const osal,
                 const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                 const uint32_t flags));
    MOCK_METHOD(KiwiTest_osalErr_e, eventFlagsGet,
                (void *const osal,
                 const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                 uint32_t *const flags));
    MOCK_METHOD(KiwiTest_osalErr_e, eventFlagsWait,
                (void *const osal,
                 const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                 const uint32_t flags,
                 const KiwiTest_osalEventFlagsOptions_e options,
                 const KiwiTest_osalTimeMs_t timeoutMs,
                 uint32_t *const actualFlags));
    MOCK_METHOD(KiwiTest_osalErr_e, threadCreate,
                (void *const osal,
                 KiwiTest_osalThreadHandle_t *const threadHandle,
                 KiwiTest_osalThreadAttr_s threadAttr));
    MOCK_METHOD(KiwiTest_osalErr_e, threadDelete,
                (void *const osal,
                 const KiwiTest_osalThreadHandle_t threadHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, threadSuspend,
                (void *const osal,
                 const KiwiTest_osalThreadHandle_t threadHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, threadResume,
                (void *const osal,
                 const KiwiTest_osalThreadHandle_t threadHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, threadYield, (void *const osal));
    MOCK_METHOD(KiwiTest_osalErr_e, threadDelay,
                (void *const osal,
                 const KiwiTest_osalTimeMs_t delayMs));
    MOCK_METHOD(KiwiTest_osalErr_e, threadDelayUntil,
                (void *const osal,
                 KiwiTest_osalTimeMs_t *const previousWakeTimeMs,
                 const KiwiTest_osalTimeMs_t periodMs));
    MOCK_METHOD(void, threadExit, (void *const osal));
    MOCK_METHOD(KiwiTest_osalErr_e, criticalSectionEnter, (void *const osal));
    MOCK_METHOD(KiwiTest_osalErr_e, criticalSectionExit, (void *const osal));
    MOCK_METHOD(KiwiTest_osalErr_e, softwareTimerCreate,
                (void *const osal,
                 KiwiTest_osalSoftwareTimerHandle_t *const timerHandle,
                 KiwiTest_osalSoftwareTimerAttr_s timerAttr));
    MOCK_METHOD(KiwiTest_osalErr_e, softwareTimerDelete,
                (void *const osal,
                 const KiwiTest_osalSoftwareTimerHandle_t timerHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, softwareTimerStart,
                (void *const osal,
                 const KiwiTest_osalSoftwareTimerHandle_t timerHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, softwareTimerStop,
                (void *const osal,
                 const KiwiTest_osalSoftwareTimerHandle_t timerHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, softwareTimerReset,
                (void *const osal,
                 const KiwiTest_osalSoftwareTimerHandle_t timerHandle));
    MOCK_METHOD(KiwiTest_osalErr_e, timeMsGet,
                (void *const osal,
                 KiwiTest_osalTimeMs_t *const osTimeMs));
    MOCK_METHOD(KiwiTest_osalErr_e, memAlloc,
                (void *const osal,
                 const size_t size,
                 void **const memPtr));
    MOCK_METHOD(KiwiTest_osalErr_e, memFree,
                (void *const osal,
                 void *const memPtr));
    MOCK_METHOD(bool, isValid, (const void *const osal));
};

namespace kiwi_test_osal_mock_registry
{

void bind(KiwiTest_osal_s *osal, OsalMock *mock);
void unbind(KiwiTest_osal_s *osal);
OsalMock *get(const KiwiTest_osal_s *osal);

} // namespace kiwi_test_osal_mock_registry


class OsalMockBinder
{
public:
    OsalMockBinder(KiwiTest_osal_s *osal, OsalMock *mock)
        : osal_(osal)
    {
        if (osal_ != nullptr)
        {
            osal_->vtable    = &kOsalMockVtable;
            osal_->validFlag = true;
            kiwi_test_osal_mock_registry::bind(osal_, mock);
        }
    }

    ~OsalMockBinder()
    {
        if (osal_ != nullptr)
        {
            kiwi_test_osal_mock_registry::unbind(osal_);
        }
    }

    OsalMockBinder(const OsalMockBinder &) = delete;
    OsalMockBinder &operator=(const OsalMockBinder &) = delete;

private:
    KiwiTest_osal_s *osal_;
};

#endif /* KIWI_TEST_OSAL_MOCK_HPP_ */
