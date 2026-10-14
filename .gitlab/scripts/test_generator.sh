#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

ci_install_build_tools
ci_install_generator_editable
cmake \
    -S test \
    -B build/test-generator \
    -DCMAKE_BUILD_TYPE=Debug \
    -DKIWI_TEST_BUILD_GENERATOR=ON \
    -DKIWI_TEST_BUILD_UNIT=OFF \
    -DKIWI_TEST_BUILD_POSIX=OFF \
    -DKIWI_TEST_BUILD_FREERTOS_POSIX=OFF

ci_run_ctest_suite build/test-generator generator junit.xml
