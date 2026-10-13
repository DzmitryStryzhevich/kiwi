/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kiwi contributors
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>

extern "C"
{
#include "FreeRTOS.h"
#include "task.h"
}

namespace
{
int testResult = EXIT_FAILURE;

void gtestTask(void *)
{
    testResult = RUN_ALL_TESTS();
    std::fflush(stdout);
    std::fflush(stderr);
    std::exit(testResult);
}
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    constexpr configSTACK_DEPTH_TYPE stackDepth = 32u * 1024u;
    TaskHandle_t task = nullptr;

    const BaseType_t createStatus = xTaskCreate(gtestTask,
                                                "gtest",
                                                stackDepth,
                                                nullptr,
                                                tskIDLE_PRIORITY + 2u,
                                                &task);
    if ((createStatus != pdPASS) || (task == nullptr))
    {
        return EXIT_FAILURE;
    }

    vTaskStartScheduler();

    return EXIT_FAILURE;
}
