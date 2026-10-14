# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

cmake_minimum_required(VERSION 3.20)

project(kiwi_osal_test LANGUAGES C CXX)

include(CTest)
include(FetchContent)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KIWI_TEST_BUILD_GENERATOR "Build/register generator integration tests" ON)
option(KIWI_TEST_BUILD_UNIT "Build generic OSAL unit tests" ON)
option(KIWI_TEST_BUILD_POSIX "Build native POSIX OSAL integration tests" ON)
option(KIWI_TEST_BUILD_FREERTOS_POSIX "Build FreeRTOS backend integration tests using GCC_POSIX" ON)
option(KIWI_TEST_FETCH_GTEST "Fetch GoogleTest when KIWI_TEST_GTEST_SOURCE_DIR is not supplied" ON)
option(KIWI_TEST_FETCH_FREERTOS "Fetch FreeRTOS-Kernel when KIWI_TEST_FREERTOS_KERNEL_DIR is not supplied" ON)
option(KIWI_TEST_STRICT "Treat test-harness warnings as errors" ON)

set(KIWI_TEST_GTEST_TAG "v1.14.0" CACHE STRING "GoogleTest tag used by the test harness")
set(KIWI_TEST_FREERTOS_TAG "V11.3.1" CACHE STRING "FreeRTOS-Kernel tag used by the host integration harness")
set(KIWI_TEST_GTEST_SOURCE_DIR "" CACHE PATH "Optional local GoogleTest source directory")
set(KIWI_TEST_FREERTOS_KERNEL_DIR "" CACHE PATH "Optional local FreeRTOS-Kernel source directory")

get_filename_component(KIWI_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(KIWI_TEST_GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated")
set(KIWI_TEST_GENERATED_OSAL_DIR "${KIWI_TEST_GENERATED_ROOT}/kiwi_test")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

# Generate the exact full-set OSAL used by every test executable.  The test
# harness deliberately regenerates from the current templates instead of
# keeping checked-in generated sources that can become stale.
execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        "${KIWI_REPOSITORY_ROOT}/generator/cli/main.py"
        --module-prefix=kiwi_test
        --port=FreeRTOS
        --port=POSIX
        --use-queue-api
        --use-stream-buffer-api
        --use-mutex-api
        --use-semaphore-api
        --use-event-flags-api
        --use-thread-api
        --use-critical-section-api
        --use-software-timer-api
        --use-time-api
        --use-memory-api
        --split-into-port-dir
        --no-format-generated-code
        --output=${KIWI_TEST_GENERATED_ROOT}
    WORKING_DIRECTORY "${KIWI_REPOSITORY_ROOT}"
    RESULT_VARIABLE KIWI_TEST_GENERATE_RESULT
    OUTPUT_VARIABLE KIWI_TEST_GENERATE_STDOUT
    ERROR_VARIABLE KIWI_TEST_GENERATE_STDERR
)

if(NOT KIWI_TEST_GENERATE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Failed to generate the test OSAL (exit ${KIWI_TEST_GENERATE_RESULT}).\n"
        "stdout:\n${KIWI_TEST_GENERATE_STDOUT}\n"
        "stderr:\n${KIWI_TEST_GENERATE_STDERR}")
endif()

if(NOT EXISTS "${KIWI_TEST_GENERATED_OSAL_DIR}/kiwi_test_osal.c")
    message(FATAL_ERROR "Generated test OSAL was not found at ${KIWI_TEST_GENERATED_OSAL_DIR}")
endif()

if(KIWI_TEST_BUILD_UNIT OR KIWI_TEST_BUILD_POSIX OR KIWI_TEST_BUILD_FREERTOS_POSIX)
    # GoogleTest follows the same override/fetch pattern as the supplied project
    # reference: local dependency roots are supported, while CI can fetch a pinned
    # upstream version.
    if(KIWI_TEST_GTEST_SOURCE_DIR)
        get_filename_component(KIWI_TEST_GTEST_SOURCE_DIR "${KIWI_TEST_GTEST_SOURCE_DIR}" ABSOLUTE)
        if(NOT EXISTS "${KIWI_TEST_GTEST_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "GoogleTest source was not found at ${KIWI_TEST_GTEST_SOURCE_DIR}")
        endif()
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        add_subdirectory("${KIWI_TEST_GTEST_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
    elseif(KIWI_TEST_FETCH_GTEST)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG ${KIWI_TEST_GTEST_TAG}
            GIT_SHALLOW TRUE
            UPDATE_DISCONNECTED TRUE
        )
        FetchContent_MakeAvailable(googletest)
    else()
        find_package(GTest REQUIRED)
    endif()

    include(GoogleTest)

endif()

# Build the generated generic OSAL once and share it between all C/C++ suites.
if(KIWI_TEST_BUILD_UNIT OR KIWI_TEST_BUILD_POSIX OR KIWI_TEST_BUILD_FREERTOS_POSIX)
    add_subdirectory("${KIWI_TEST_GENERATED_OSAL_DIR}" "${CMAKE_BINARY_DIR}/generated-osal")

    target_compile_options(kiwi_test_osal PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wpedantic>
    )
endif()

set(KIWI_TEST_COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/mocks
    ${CMAKE_CURRENT_LIST_DIR}/integration/common
    ${KIWI_TEST_GENERATED_OSAL_DIR}
)

function(kiwi_test_apply_options target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE ${KIWI_TEST_COMMON_INCLUDE_DIRS})

    if(KIWI_TEST_STRICT)
        target_compile_options(${target} PRIVATE
            $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wpedantic;-Werror>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wpedantic>
        )
    endif()
endfunction()

if(KIWI_TEST_BUILD_GENERATOR)
    add_test(
        NAME generator::osal_profile_generation
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/generator/osal_generator_it_test.py"
            --repo-root "${KIWI_REPOSITORY_ROOT}"
    )
    set_tests_properties(generator::osal_profile_generation PROPERTIES
        LABELS "generator"
        TIMEOUT 20
    )
endif()

if(KIWI_TEST_BUILD_UNIT)
    add_executable(kiwi_osal_unit_test
        mocks/osal_mock.cpp
        unit/osal_unit_test_lifecycle_gtest.cpp
        unit/osal_unit_test_queue_gtest.cpp
        unit/osal_unit_test_mutex_gtest.cpp
        unit/osal_unit_test_semaphore_gtest.cpp
        unit/osal_unit_test_event_flags_gtest.cpp
        unit/osal_unit_test_stream_buffer_gtest.cpp
        unit/osal_unit_test_thread_gtest.cpp
        unit/osal_unit_test_software_timer_gtest.cpp
        unit/osal_unit_test_time_gtest.cpp
        unit/osal_unit_test_memory_gtest.cpp
        unit/osal_unit_test_critical_section_gtest.cpp
    )

    kiwi_test_apply_options(kiwi_osal_unit_test)
    target_link_libraries(kiwi_osal_unit_test PRIVATE
        kiwi_test_osal
        GTest::gmock_main
    )

    gtest_discover_tests(kiwi_osal_unit_test
        TEST_PREFIX "unit::"
        PROPERTIES
            LABELS "unit"
            TIMEOUT 5
        DISCOVERY_TIMEOUT 15
    )
endif()

if(KIWI_TEST_BUILD_POSIX)
    add_subdirectory("${KIWI_TEST_GENERATED_OSAL_DIR}/portable/posix"
                     "${CMAKE_BINARY_DIR}/generated-osal-posix")
    target_compile_options(kiwi_test_osal_posix PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wpedantic>
    )

    add_executable(kiwi_osal_posix_it_test
        integration/posix/osal_posix_it_test_lifecycle_gtest.cpp
        integration/posix/osal_posix_it_test_queue_gtest.cpp
        integration/posix/osal_posix_it_test_mutex_gtest.cpp
        integration/posix/osal_posix_it_test_semaphore_gtest.cpp
        integration/posix/osal_posix_it_test_event_flags_gtest.cpp
        integration/posix/osal_posix_it_test_stream_buffer_gtest.cpp
        integration/posix/osal_posix_it_test_thread_gtest.cpp
        integration/posix/osal_posix_it_test_software_timer_gtest.cpp
        integration/posix/osal_posix_it_test_time_gtest.cpp
        integration/posix/osal_posix_it_test_memory_gtest.cpp
        integration/posix/osal_posix_it_test_critical_section_gtest.cpp
    )

    kiwi_test_apply_options(kiwi_osal_posix_it_test)
    target_include_directories(kiwi_osal_posix_it_test PRIVATE
        ${KIWI_TEST_GENERATED_OSAL_DIR}/portable/posix
    )
    target_link_libraries(kiwi_osal_posix_it_test PRIVATE
        kiwi_test_osal_posix
        GTest::gtest_main
        Threads::Threads
    )

    gtest_discover_tests(kiwi_osal_posix_it_test
        TEST_PREFIX "posix::"
        PROPERTIES
            LABELS "integration\;posix"
            TIMEOUT 10
        DISCOVERY_TIMEOUT 15
    )
endif()

if(KIWI_TEST_BUILD_FREERTOS_POSIX)
    if(KIWI_TEST_FREERTOS_KERNEL_DIR)
        get_filename_component(KIWI_TEST_FREERTOS_KERNEL_DIR "${KIWI_TEST_FREERTOS_KERNEL_DIR}" ABSOLUTE)
        if(NOT EXISTS "${KIWI_TEST_FREERTOS_KERNEL_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "FreeRTOS-Kernel was not found at ${KIWI_TEST_FREERTOS_KERNEL_DIR}")
        endif()
        set(_kiwi_freertos_source_dir "${KIWI_TEST_FREERTOS_KERNEL_DIR}")
    elseif(KIWI_TEST_FETCH_FREERTOS)
        FetchContent_Declare(
            freertos_kernel_source
            GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
            GIT_TAG ${KIWI_TEST_FREERTOS_TAG}
            GIT_SHALLOW TRUE
            UPDATE_DISCONNECTED TRUE
        )
        FetchContent_GetProperties(freertos_kernel_source)
        if(NOT freertos_kernel_source_POPULATED)
            FetchContent_Populate(freertos_kernel_source)
        endif()
        set(_kiwi_freertos_source_dir "${freertos_kernel_source_SOURCE_DIR}")
    else()
        message(FATAL_ERROR
            "FreeRTOS integration tests require KIWI_TEST_FREERTOS_KERNEL_DIR or KIWI_TEST_FETCH_FREERTOS=ON")
    endif()

    add_library(freertos_config INTERFACE)
    target_include_directories(freertos_config SYSTEM INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/config
    )
    target_compile_definitions(freertos_config INTERFACE
        projCOVERAGE_TEST=0
    )

    set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)
    set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)
    add_subdirectory("${_kiwi_freertos_source_dir}" "${CMAKE_BINARY_DIR}/freertos-kernel")

    add_subdirectory("${KIWI_TEST_GENERATED_OSAL_DIR}/portable/freertos"
                     "${CMAKE_BINARY_DIR}/generated-osal-freertos")
    target_compile_options(kiwi_test_osal_freertos PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wpedantic>
    )
    target_link_libraries(kiwi_test_osal_freertos PUBLIC
        freertos_kernel
        Threads::Threads
    )

    add_executable(kiwi_osal_freertos_posix_it_test
        integration/freertos_posix/osal_freertos_posix_it_test_main.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_lifecycle_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_queue_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_mutex_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_semaphore_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_event_flags_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_stream_buffer_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_thread_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_software_timer_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_time_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_memory_gtest.cpp
        integration/freertos_posix/osal_freertos_posix_it_test_critical_section_gtest.cpp
    )

    kiwi_test_apply_options(kiwi_osal_freertos_posix_it_test)
    target_include_directories(kiwi_osal_freertos_posix_it_test PRIVATE
        ${KIWI_TEST_GENERATED_OSAL_DIR}/portable/freertos
        ${CMAKE_CURRENT_LIST_DIR}/config
    )
    target_link_libraries(kiwi_osal_freertos_posix_it_test PRIVATE
        kiwi_test_osal_freertos
        GTest::gtest
        freertos_kernel
        Threads::Threads
    )

    gtest_discover_tests(kiwi_osal_freertos_posix_it_test
        TEST_PREFIX "freertos_posix::"
        PROPERTIES
            LABELS "integration\;freertos_posix"
            TIMEOUT 10
        DISCOVERY_TIMEOUT 20
    )
endif()
