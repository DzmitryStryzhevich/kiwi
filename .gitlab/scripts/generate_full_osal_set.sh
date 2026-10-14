#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

ci_install_build_tools
python -m pip install dist-generator/*.whl

kiwicgen \
    --module-prefix=ci_osal \
    --port=FreeRTOS \
    --port=POSIX \
    --language=C \
    --use-queue-api \
    --use-stream-buffer-api \
    --use-mutex-api \
    --use-semaphore-api \
    --use-event-flags-api \
    --use-thread-api \
    --use-critical-section-api \
    --use-software-timer-api \
    --use-time-api \
    --use-memory-api \
    --split-into-port-dir \
    --output=ci-generated

test -f ci-generated/ci_osal/ci_osal_osal.h
test -f ci-generated/ci_osal/ci_osal_osal.c
test -f ci-generated/ci_osal/portable/freertos/ci_osal_osal_freertos.c
test -f ci-generated/ci_osal/portable/posix/ci_osal_osal_posix.c

if grep -R -n -E '// BEGIN |// END ' \
    ci-generated/ci_osal \
    --include='*.c' \
    --include='*.h'
then
    echo "Generated sources still contain generator section markers." >&2
    exit 1
fi
