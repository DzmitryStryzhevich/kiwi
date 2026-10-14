#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

ci_install_build_tools
ci_clone_freertos_kernel

cmake \
    -S .github/ci/freertos_posix \
    -B build-generated \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFREERTOS_KERNEL_DIR="${CI_PROJECT_DIR}/third_party/FreeRTOS-Kernel" \
    -DGENERATED_OSAL_DIR="${CI_PROJECT_DIR}/ci-generated/ci_osal"

cmake --build build-generated --parallel
./build-generated/ci_osal_posix_link_check
