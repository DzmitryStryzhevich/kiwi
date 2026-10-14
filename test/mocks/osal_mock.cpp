/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include "mocks/osal_mock.hpp"

#include <cassert>
#include <mutex>
#include <unordered_map>

namespace kiwi_test_osal_mock_registry
{
static std::unordered_map<const KiwiTest_osal_s *, OsalMock *> mockMap;
static std::mutex mockMutex;


void bind(KiwiTest_osal_s *osal, OsalMock *mock)
{
    std::lock_guard<std::mutex> lock(mockMutex);

    mockMap[osal] = mock;
}


void unbind(KiwiTest_osal_s *osal)
{
    std::lock_guard<std::mutex> lock(mockMutex);

    mockMap.erase(osal);
}


OsalMock *get(const KiwiTest_osal_s *osal)
{
    std::lock_guard<std::mutex> lock(mockMutex);
    const auto it = mockMap.find(osal);

    return (it == mockMap.end()) ? nullptr : it->second;
}
} // namespace kiwi_test_osal_mock_registry


static OsalMock *mustGetMock(const void *osal)
{
    OsalMock *const mock =
        kiwi_test_osal_mock_registry::get(static_cast<const KiwiTest_osal_s *>(osal));

    assert(mock != nullptr);

    return mock;
}

static KiwiTest_osalErr_e bridge_queueCreate(void *const osal,
                                             const size_t queueItemSize,
                                             const size_t queueDepth,
                                             KiwiTest_osalQueueHandle_t *const queueHandle)
{
    return mustGetMock(osal)->queueCreate(osal, queueItemSize, queueDepth, queueHandle);
}


static KiwiTest_osalErr_e bridge_queueDelete(void *const osal,
                                             const KiwiTest_osalQueueHandle_t queueHandle)
{
    return mustGetMock(osal)->queueDelete(osal, queueHandle);
}


static KiwiTest_osalErr_e bridge_queueItemPut(void *const osal,
                                              const KiwiTest_osalQueueHandle_t queueHandle,
                                              const void *const queueItemPtr)
{
    return mustGetMock(osal)->queueItemPut(osal, queueHandle, queueItemPtr);
}


static KiwiTest_osalErr_e bridge_queueItemPost(void *const osal,
                                               const KiwiTest_osalQueueHandle_t queueHandle,
                                               const void *const queueItemPtr,
                                               const KiwiTest_osalTimeMs_t timeoutMs)
{
    return mustGetMock(osal)->queueItemPost(osal, queueHandle, queueItemPtr, timeoutMs);
}


static KiwiTest_osalErr_e bridge_queueItemGet(void *const osal,
                                              const KiwiTest_osalQueueHandle_t queueHandle,
                                              void *const queueItemPtr)
{
    return mustGetMock(osal)->queueItemGet(osal, queueHandle, queueItemPtr);
}


static KiwiTest_osalErr_e bridge_queueItemWait(void *const osal,
                                               const KiwiTest_osalQueueHandle_t queueHandle,
                                               void *const queueItemPtr)
{
    return mustGetMock(osal)->queueItemWait(osal, queueHandle, queueItemPtr);
}


static KiwiTest_osalErr_e bridge_queueItemPend(void *const osal,
                                               const KiwiTest_osalQueueHandle_t queueHandle,
                                               void *const queueItemPtr,
                                               const KiwiTest_osalTimeMs_t timeoutMs)
{
    return mustGetMock(osal)->queueItemPend(osal, queueHandle, queueItemPtr, timeoutMs);
}


static KiwiTest_osalErr_e bridge_queueReset(void *const osal,
                                            const KiwiTest_osalQueueHandle_t queueHandle)
{
    return mustGetMock(osal)->queueReset(osal, queueHandle);
}


static KiwiTest_osalErr_e bridge_streamBufferCreate(void *const osal,
                                                    const size_t bufferSizeBytes,
                                                    const size_t triggerLevelBytes,
                                                    KiwiTest_osalStreamBufferHandle_t *const streamBufferHandle)
{
    return mustGetMock(osal)->streamBufferCreate(osal, bufferSizeBytes, triggerLevelBytes, streamBufferHandle);
}


static KiwiTest_osalErr_e bridge_streamBufferDelete(void *const osal,
                                                    const KiwiTest_osalStreamBufferHandle_t streamBufferHandle)
{
    return mustGetMock(osal)->streamBufferDelete(osal, streamBufferHandle);
}


static KiwiTest_osalErr_e bridge_streamBufferPut(void *const osal,
                                                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                                                 const void *const data,
                                                 const size_t dataLengthBytes,
                                                 size_t *const bytesPut)
{
    return mustGetMock(osal)->streamBufferPut(osal, streamBufferHandle, data, dataLengthBytes, bytesPut);
}


static KiwiTest_osalErr_e bridge_streamBufferPost(void *const osal,
                                                  const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                                                  const void *const data,
                                                  const size_t dataLengthBytes,
                                                  const KiwiTest_osalTimeMs_t timeoutMs,
                                                  size_t *const bytesPut)
{
    return mustGetMock(osal)->streamBufferPost(osal, streamBufferHandle, data, dataLengthBytes, timeoutMs, bytesPut);
}


static KiwiTest_osalErr_e bridge_streamBufferGet(void *const osal,
                                                 const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                                                 void *const data,
                                                 const size_t dataLengthBytes,
                                                 size_t *const bytesGet)
{
    return mustGetMock(osal)->streamBufferGet(osal, streamBufferHandle, data, dataLengthBytes, bytesGet);
}


static KiwiTest_osalErr_e bridge_streamBufferWait(void *const osal,
                                                  const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                                                  void *const data,
                                                  const size_t dataLengthBytes,
                                                  size_t *const bytesGet)
{
    return mustGetMock(osal)->streamBufferWait(osal, streamBufferHandle, data, dataLengthBytes, bytesGet);
}


static KiwiTest_osalErr_e bridge_streamBufferPend(void *const osal,
                                                  const KiwiTest_osalStreamBufferHandle_t streamBufferHandle,
                                                  void *const data,
                                                  const size_t dataLengthBytes,
                                                  const KiwiTest_osalTimeMs_t timeoutMs,
                                                  size_t *const bytesGet)
{
    return mustGetMock(osal)->streamBufferPend(osal, streamBufferHandle, data, dataLengthBytes, timeoutMs, bytesGet);
}


static KiwiTest_osalErr_e bridge_streamBufferReset(void *const osal,
                                                   const KiwiTest_osalStreamBufferHandle_t streamBufferHandle)
{
    return mustGetMock(osal)->streamBufferReset(osal, streamBufferHandle);
}


static KiwiTest_osalErr_e bridge_mutexCreate(void *const osal,
                                             KiwiTest_osalMutexHandle_t *const mutexHandle)
{
    return mustGetMock(osal)->mutexCreate(osal, mutexHandle);
}


static KiwiTest_osalErr_e bridge_mutexDelete(void *const osal,
                                             const KiwiTest_osalMutexHandle_t mutexHandle)
{
    return mustGetMock(osal)->mutexDelete(osal, mutexHandle);
}


static KiwiTest_osalErr_e bridge_mutexLock(void *const osal,
                                           const KiwiTest_osalMutexHandle_t mutexHandle)
{
    return mustGetMock(osal)->mutexLock(osal, mutexHandle);
}


static KiwiTest_osalErr_e bridge_mutexTryLock(void *const osal,
                                              const KiwiTest_osalMutexHandle_t mutexHandle)
{
    return mustGetMock(osal)->mutexTryLock(osal, mutexHandle);
}


static KiwiTest_osalErr_e bridge_mutexPendLock(void *const osal,
                                               const KiwiTest_osalMutexHandle_t mutexHandle,
                                               const KiwiTest_osalTimeMs_t timeoutMs)
{
    return mustGetMock(osal)->mutexPendLock(osal, mutexHandle, timeoutMs);
}


static KiwiTest_osalErr_e bridge_mutexUnlock(void *const osal,
                                             const KiwiTest_osalMutexHandle_t mutexHandle)
{
    return mustGetMock(osal)->mutexUnlock(osal, mutexHandle);
}


static KiwiTest_osalErr_e bridge_semaphoreCreate(void *const osal,
                                                 const KiwiTest_osalSemaphoreCount_t maxCount,
                                                 const KiwiTest_osalSemaphoreCount_t initialCount,
                                                 KiwiTest_osalSemaphoreHandle_t *const semaphoreHandle)
{
    return mustGetMock(osal)->semaphoreCreate(osal, maxCount, initialCount, semaphoreHandle);
}


static KiwiTest_osalErr_e bridge_semaphoreDelete(void *const osal,
                                                 const KiwiTest_osalSemaphoreHandle_t semaphoreHandle)
{
    return mustGetMock(osal)->semaphoreDelete(osal, semaphoreHandle);
}


static KiwiTest_osalErr_e bridge_semaphoreWait(void *const osal,
                                               const KiwiTest_osalSemaphoreHandle_t semaphoreHandle)
{
    return mustGetMock(osal)->semaphoreWait(osal, semaphoreHandle);
}


static KiwiTest_osalErr_e bridge_semaphorePend(void *const osal,
                                               const KiwiTest_osalSemaphoreHandle_t semaphoreHandle,
                                               const KiwiTest_osalTimeMs_t timeoutMs)
{
    return mustGetMock(osal)->semaphorePend(osal, semaphoreHandle, timeoutMs);
}


static KiwiTest_osalErr_e bridge_semaphorePost(void *const osal,
                                               const KiwiTest_osalSemaphoreHandle_t semaphoreHandle)
{
    return mustGetMock(osal)->semaphorePost(osal, semaphoreHandle);
}


static KiwiTest_osalErr_e bridge_semaphoreCountGet(void *const osal,
                                                   const KiwiTest_osalSemaphoreHandle_t semaphoreHandle,
                                                   KiwiTest_osalSemaphoreCount_t *const semaphoreCount)
{
    return mustGetMock(osal)->semaphoreCountGet(osal, semaphoreHandle, semaphoreCount);
}


static KiwiTest_osalErr_e bridge_eventFlagsCreate(void *const osal,
                                                  KiwiTest_osalEventFlagsHandle_t *const eventFlagsHandle)
{
    return mustGetMock(osal)->eventFlagsCreate(osal, eventFlagsHandle);
}


static KiwiTest_osalErr_e bridge_eventFlagsDelete(void *const osal,
                                                  const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle)
{
    return mustGetMock(osal)->eventFlagsDelete(osal, eventFlagsHandle);
}


static KiwiTest_osalErr_e bridge_eventFlagsSet(void *const osal,
                                               const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                                               const uint32_t flags)
{
    return mustGetMock(osal)->eventFlagsSet(osal, eventFlagsHandle, flags);
}


static KiwiTest_osalErr_e bridge_eventFlagsClear(void *const osal,
                                                 const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                                                 const uint32_t flags)
{
    return mustGetMock(osal)->eventFlagsClear(osal, eventFlagsHandle, flags);
}


static KiwiTest_osalErr_e bridge_eventFlagsGet(void *const osal,
                                               const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                                               uint32_t *const flags)
{
    return mustGetMock(osal)->eventFlagsGet(osal, eventFlagsHandle, flags);
}


static KiwiTest_osalErr_e bridge_eventFlagsWait(void *const osal,
                                                const KiwiTest_osalEventFlagsHandle_t eventFlagsHandle,
                                                const uint32_t flags,
                                                const KiwiTest_osalEventFlagsOptions_e options,
                                                const KiwiTest_osalTimeMs_t timeoutMs,
                                                uint32_t *const actualFlags)
{
    return mustGetMock(osal)->eventFlagsWait(osal, eventFlagsHandle, flags, options, timeoutMs, actualFlags);
}


static KiwiTest_osalErr_e bridge_threadCreate(void *const osal,
                                              KiwiTest_osalThreadHandle_t *const threadHandle,
                                              KiwiTest_osalThreadAttr_s threadAttr)
{
    return mustGetMock(osal)->threadCreate(osal, threadHandle, threadAttr);
}


static KiwiTest_osalErr_e bridge_threadDelete(void *const osal,
                                              const KiwiTest_osalThreadHandle_t threadHandle)
{
    return mustGetMock(osal)->threadDelete(osal, threadHandle);
}


static KiwiTest_osalErr_e bridge_threadSuspend(void *const osal,
                                               const KiwiTest_osalThreadHandle_t threadHandle)
{
    return mustGetMock(osal)->threadSuspend(osal, threadHandle);
}


static KiwiTest_osalErr_e bridge_threadResume(void *const osal,
                                              const KiwiTest_osalThreadHandle_t threadHandle)
{
    return mustGetMock(osal)->threadResume(osal, threadHandle);
}


static KiwiTest_osalErr_e bridge_threadYield(void *const osal)
{
    return mustGetMock(osal)->threadYield(osal);
}


static KiwiTest_osalErr_e bridge_threadDelay(void *const osal,
                                             const KiwiTest_osalTimeMs_t delayMs)
{
    return mustGetMock(osal)->threadDelay(osal, delayMs);
}


static KiwiTest_osalErr_e bridge_threadDelayUntil(void *const osal,
                                                  KiwiTest_osalTimeMs_t *const previousWakeTimeMs,
                                                  const KiwiTest_osalTimeMs_t periodMs)
{
    return mustGetMock(osal)->threadDelayUntil(osal, previousWakeTimeMs, periodMs);
}


static void bridge_threadExit(void *const osal)
{
    mustGetMock(osal)->threadExit(osal);
}


static KiwiTest_osalErr_e bridge_criticalSectionEnter(void *const osal)
{
    return mustGetMock(osal)->criticalSectionEnter(osal);
}


static KiwiTest_osalErr_e bridge_criticalSectionExit(void *const osal)
{
    return mustGetMock(osal)->criticalSectionExit(osal);
}


static KiwiTest_osalErr_e bridge_softwareTimerCreate(void *const osal,
                                                     KiwiTest_osalSoftwareTimerHandle_t *const timerHandle,
                                                     KiwiTest_osalSoftwareTimerAttr_s timerAttr)
{
    return mustGetMock(osal)->softwareTimerCreate(osal, timerHandle, timerAttr);
}


static KiwiTest_osalErr_e bridge_softwareTimerDelete(void *const osal,
                                                     const KiwiTest_osalSoftwareTimerHandle_t timerHandle)
{
    return mustGetMock(osal)->softwareTimerDelete(osal, timerHandle);
}


static KiwiTest_osalErr_e bridge_softwareTimerStart(void *const osal,
                                                    const KiwiTest_osalSoftwareTimerHandle_t timerHandle)
{
    return mustGetMock(osal)->softwareTimerStart(osal, timerHandle);
}


static KiwiTest_osalErr_e bridge_softwareTimerStop(void *const osal,
                                                   const KiwiTest_osalSoftwareTimerHandle_t timerHandle)
{
    return mustGetMock(osal)->softwareTimerStop(osal, timerHandle);
}


static KiwiTest_osalErr_e bridge_softwareTimerReset(void *const osal,
                                                    const KiwiTest_osalSoftwareTimerHandle_t timerHandle)
{
    return mustGetMock(osal)->softwareTimerReset(osal, timerHandle);
}


static KiwiTest_osalErr_e bridge_timeMsGet(void *const osal,
                                           KiwiTest_osalTimeMs_t *const osTimeMs)
{
    return mustGetMock(osal)->timeMsGet(osal, osTimeMs);
}


static KiwiTest_osalErr_e bridge_memAlloc(void *const osal,
                                          const size_t size,
                                          void **const memPtr)
{
    return mustGetMock(osal)->memAlloc(osal, size, memPtr);
}


static KiwiTest_osalErr_e bridge_memFree(void *const osal,
                                         void *const memPtr)
{
    return mustGetMock(osal)->memFree(osal, memPtr);
}


static bool bridge_isValid(const void *const osal)
{
    return mustGetMock(osal)->isValid(osal);
}


const KiwiTest_osalVtable_s kOsalMockVtable = []()
                                              {
                                                  KiwiTest_osalVtable_s vtable = {};

                                                  vtable.queueCreate          = bridge_queueCreate;
                                                  vtable.queueDelete          = bridge_queueDelete;
                                                  vtable.queueItemPut         = bridge_queueItemPut;
                                                  vtable.queueItemPost        = bridge_queueItemPost;
                                                  vtable.queueItemGet         = bridge_queueItemGet;
                                                  vtable.queueItemWait        = bridge_queueItemWait;
                                                  vtable.queueItemPend        = bridge_queueItemPend;
                                                  vtable.queueReset           = bridge_queueReset;
                                                  vtable.streamBufferCreate   = bridge_streamBufferCreate;
                                                  vtable.streamBufferDelete   = bridge_streamBufferDelete;
                                                  vtable.streamBufferPut      = bridge_streamBufferPut;
                                                  vtable.streamBufferPost     = bridge_streamBufferPost;
                                                  vtable.streamBufferGet      = bridge_streamBufferGet;
                                                  vtable.streamBufferWait     = bridge_streamBufferWait;
                                                  vtable.streamBufferPend     = bridge_streamBufferPend;
                                                  vtable.streamBufferReset    = bridge_streamBufferReset;
                                                  vtable.mutexCreate          = bridge_mutexCreate;
                                                  vtable.mutexDelete          = bridge_mutexDelete;
                                                  vtable.mutexLock            = bridge_mutexLock;
                                                  vtable.mutexTryLock         = bridge_mutexTryLock;
                                                  vtable.mutexPendLock        = bridge_mutexPendLock;
                                                  vtable.mutexUnlock          = bridge_mutexUnlock;
                                                  vtable.semaphoreCreate      = bridge_semaphoreCreate;
                                                  vtable.semaphoreDelete      = bridge_semaphoreDelete;
                                                  vtable.semaphoreWait        = bridge_semaphoreWait;
                                                  vtable.semaphorePend        = bridge_semaphorePend;
                                                  vtable.semaphorePost        = bridge_semaphorePost;
                                                  vtable.semaphoreCountGet    = bridge_semaphoreCountGet;
                                                  vtable.eventFlagsCreate     = bridge_eventFlagsCreate;
                                                  vtable.eventFlagsDelete     = bridge_eventFlagsDelete;
                                                  vtable.eventFlagsSet        = bridge_eventFlagsSet;
                                                  vtable.eventFlagsClear      = bridge_eventFlagsClear;
                                                  vtable.eventFlagsGet        = bridge_eventFlagsGet;
                                                  vtable.eventFlagsWait       = bridge_eventFlagsWait;
                                                  vtable.threadCreate         = bridge_threadCreate;
                                                  vtable.threadDelete         = bridge_threadDelete;
                                                  vtable.threadSuspend        = bridge_threadSuspend;
                                                  vtable.threadResume         = bridge_threadResume;
                                                  vtable.threadYield          = bridge_threadYield;
                                                  vtable.threadDelay          = bridge_threadDelay;
                                                  vtable.threadDelayUntil     = bridge_threadDelayUntil;
                                                  vtable.threadExit           = bridge_threadExit;
                                                  vtable.criticalSectionEnter = bridge_criticalSectionEnter;
                                                  vtable.criticalSectionExit  = bridge_criticalSectionExit;
                                                  vtable.softwareTimerCreate  = bridge_softwareTimerCreate;
                                                  vtable.softwareTimerDelete  = bridge_softwareTimerDelete;
                                                  vtable.softwareTimerStart   = bridge_softwareTimerStart;
                                                  vtable.softwareTimerStop    = bridge_softwareTimerStop;
                                                  vtable.softwareTimerReset   = bridge_softwareTimerReset;
                                                  vtable.timeMsGet            = bridge_timeMsGet;
                                                  vtable.memAlloc             = bridge_memAlloc;
                                                  vtable.memFree              = bridge_memFree;
                                                  vtable.isValid              = bridge_isValid;

                                                  return vtable;
                                              }();
