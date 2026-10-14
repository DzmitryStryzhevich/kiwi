#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

ci_install_static_analysis_tools
ci_clone_freertos_kernel

gcc -std=c11 -Wall -Wextra -Werror -fanalyzer \
    -Ici-generated/ci_osal \
    -Ici-generated/ci_osal/portable/freertos \
    -I.github/ci/freertos_posix \
    -Ithird_party/FreeRTOS-Kernel/include \
    -Ithird_party/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix \
    -c ci-generated/ci_osal/ci_osal_osal.c \
    -o /tmp/ci_osal_osal_analyze.o

gcc -std=c11 -Wall -Wextra -Werror -fanalyzer \
    -Ici-generated/ci_osal \
    -Ici-generated/ci_osal/portable/freertos \
    -I.github/ci/freertos_posix \
    -Ithird_party/FreeRTOS-Kernel/include \
    -Ithird_party/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix \
    -c ci-generated/ci_osal/portable/freertos/ci_osal_osal_freertos.c \
    -o /tmp/ci_osal_osal_freertos_analyze.o

gcc -std=c11 -Wall -Wextra -Werror -fanalyzer -pthread \
    -D_POSIX_C_SOURCE=200809L \
    -Ici-generated/ci_osal \
    -Ici-generated/ci_osal/portable/posix \
    -c ci-generated/ci_osal/portable/posix/ci_osal_osal_posix.c \
    -o /tmp/ci_osal_osal_posix_analyze.o

cppcheck \
    --std=c11 \
    --enable=warning,performance,portability \
    --error-exitcode=1 \
    --suppress=missingIncludeSystem \
    -Ici-generated/ci_osal \
    -Ici-generated/ci_osal/portable/freertos \
    -Ici-generated/ci_osal/portable/posix \
    -I.github/ci/freertos_posix \
    -Ithird_party/FreeRTOS-Kernel/include \
    -Ithird_party/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix \
    ci-generated/ci_osal/ci_osal_osal.c \
    ci-generated/ci_osal/portable/freertos/ci_osal_osal_freertos.c \
    ci-generated/ci_osal/portable/posix/ci_osal_osal_posix.c
