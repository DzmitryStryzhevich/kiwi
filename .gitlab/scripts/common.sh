#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

set -euo pipefail

KIWI_GITLAB_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KIWI_REPO_ROOT="$(cd "${KIWI_GITLAB_SCRIPT_DIR}/../.." && pwd)"

export CI_PROJECT_DIR="${CI_PROJECT_DIR:-${KIWI_REPO_ROOT}}"
export PIP_DISABLE_PIP_VERSION_CHECK="${PIP_DISABLE_PIP_VERSION_CHECK:-1}"
export CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"
export FREERTOS_KERNEL_REF="${FREERTOS_KERNEL_REF:-V11.3.1}"
export GTEST_REF="${GTEST_REF:-v1.14.0}"

cd "${CI_PROJECT_DIR}"

ci_install_packages()
{
    apt-get update
    apt-get install -y --no-install-recommends "$@"
    rm -rf /var/lib/apt/lists/*
}


ci_install_build_tools()
{
    ci_install_packages cmake ninja-build g++ git ca-certificates
}


ci_install_static_analysis_tools()
{
    ci_install_packages cmake ninja-build gcc g++ git ca-certificates cppcheck
}


ci_install_generator_editable()
{
    python -m pip install -e generator
}


ci_clone_repo()
{
    local repository_url="$1"
    local repository_ref="$2"
    local destination="$3"

    rm -rf "${destination}"
    git clone --depth 1 --branch "${repository_ref}" "${repository_url}" "${destination}"
}


ci_clone_gtest()
{
    ci_clone_repo \
        "https://github.com/google/googletest.git" \
        "${GTEST_REF}" \
        "third_party/googletest"
}


ci_clone_freertos_kernel()
{
    ci_clone_repo \
        "https://github.com/FreeRTOS/FreeRTOS-Kernel.git" \
        "${FREERTOS_KERNEL_REF}" \
        "third_party/FreeRTOS-Kernel"
}


ci_run_ctest_suite()
{
    local build_dir="$1"
    local label="$2"
    local junit_file="$3"

    ctest --test-dir "${build_dir}" -N -L "${label}"
    ctest \
        --test-dir "${build_dir}" \
        -L "${label}" \
        --output-on-failure \
        --output-junit "${build_dir}/${junit_file}"
}
