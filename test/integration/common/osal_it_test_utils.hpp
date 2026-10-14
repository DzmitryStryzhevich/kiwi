/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#ifndef KIWI_OSAL_IT_TEST_UTILS_HPP_
#define KIWI_OSAL_IT_TEST_UTILS_HPP_

#include <atomic>
#include <cstdint>
#include <functional>

extern "C"
{
#include "kiwi_test_osal.h"
}

namespace kiwi::test
{
constexpr KiwiTest_osalTimeMs_t kPollDelayMs       = 1u;
constexpr KiwiTest_osalTimeMs_t kShortTimeoutMs    = 40u;
constexpr KiwiTest_osalTimeMs_t kScenarioTimeoutMs = 500u;

inline bool waitUntil(KiwiTest_osal_s *osal,
                      const std::function<bool()> &predicate,
                      const KiwiTest_osalTimeMs_t timeoutMs = kScenarioTimeoutMs)
{
    KiwiTest_osalTimeMs_t startMs = 0u;

    if (kiwiTest_osalTimeMsGet(osal, &startMs) != KIWI_TEST_OSAL_NO_ERR)
    {
        return false;
    }

    while (!predicate())
    {
        KiwiTest_osalTimeMs_t nowMs = 0u;
        if (kiwiTest_osalTimeMsGet(osal, &nowMs) != KIWI_TEST_OSAL_NO_ERR)
        {
            return false;
        }

        if (static_cast<KiwiTest_osalTimeMs_t>(nowMs - startMs) >= timeoutMs)
        {
            return false;
        }

        if (kiwiTest_osalThreadDelay(osal, kPollDelayMs) != KIWI_TEST_OSAL_NO_ERR)
        {
            return false;
        }
    }

    return true;
}

inline bool staysFalseFor(KiwiTest_osal_s *osal,
                          const std::atomic<bool> &value,
                          const KiwiTest_osalTimeMs_t durationMs)
{
    KiwiTest_osalTimeMs_t startMs = 0u;

    if (kiwiTest_osalTimeMsGet(osal, &startMs) != KIWI_TEST_OSAL_NO_ERR)
    {
        return false;
    }

    while (true)
    {
        if (value.load())
        {
            return false;
        }

        KiwiTest_osalTimeMs_t nowMs = 0u;
        if (kiwiTest_osalTimeMsGet(osal, &nowMs) != KIWI_TEST_OSAL_NO_ERR)
        {
            return false;
        }

        if (static_cast<KiwiTest_osalTimeMs_t>(nowMs - startMs) >= durationMs)
        {
            return true;
        }

        if (kiwiTest_osalThreadDelay(osal, kPollDelayMs) != KIWI_TEST_OSAL_NO_ERR)
        {
            return false;
        }
    }
}

[[noreturn]] inline void parkWorker(KiwiTest_osal_s *const osal)
{
    while (true)
    {
        (void)kiwiTest_osalThreadDelay(osal, kShortTimeoutMs);
    }
}


class RegisteredThreadGuard
{
public:
    explicit RegisteredThreadGuard(KiwiTest_osal_s *const osal) : osal_(osal)
    {
    }

    RegisteredThreadGuard(const RegisteredThreadGuard &)            = delete;
    RegisteredThreadGuard &operator=(const RegisteredThreadGuard &) = delete;

    ~RegisteredThreadGuard()
    {
        cleanup();
    }

    void set(const KiwiTest_osalThreadHandle_t handle)
    {
        handle_ = handle;
    }

    void release()
    {
        handle_ = nullptr;
    }

    void cleanup()
    {
        if ((osal_ != nullptr) &&
            (handle_ != nullptr))
        {
            (void)kiwiTest_osalThreadDelete(osal_, handle_);
        }

        handle_ = nullptr;
    }

private:
    KiwiTest_osal_s *osal_              = nullptr;
    KiwiTest_osalThreadHandle_t handle_ = nullptr;
};

inline KiwiTest_osalThreadAttr_s workerAttr(KiwiTest_osalThreadWorker_f worker,
                                            void *args,
                                            const char *name = "it-worker")
{
    KiwiTest_osalThreadAttr_s attr = {};

    attr.worker    = worker;
    attr.name      = name;
    attr.stackSize = 64u * 1024u;
    attr.args      = args;
    attr.prio      = KIWI_TEST_OSAL_THREAD_PRIO_NORMAL;

    return attr;
}
} // namespace kiwi::test

#endif /* KIWI_OSAL_IT_TEST_UTILS_HPP_ */
