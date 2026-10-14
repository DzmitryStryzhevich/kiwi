#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

ci_install_build_tools
ci_install_generator_editable
ci_clone_gtest

cmake \
    -S test \
    -B build/test-posix \
    -DCMAKE_BUILD_TYPE=Debug \
    -DKIWI_TEST_BUILD_GENERATOR=OFF \
    -DKIWI_TEST_BUILD_UNIT=OFF \
    -DKIWI_TEST_BUILD_POSIX=ON \
    -DKIWI_TEST_BUILD_FREERTOS_POSIX=OFF \
    -DKIWI_TEST_FETCH_GTEST=OFF \
    -DKIWI_TEST_GTEST_SOURCE_DIR="${CI_PROJECT_DIR}/third_party/googletest"

cmake --build build/test-posix --parallel
ci_run_ctest_suite build/test-posix '^posix$' junit.xml
