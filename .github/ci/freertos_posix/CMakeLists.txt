# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

cmake_minimum_required(VERSION 3.20)
project(kiwi_generated_freertos_posix_ci C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED FREERTOS_KERNEL_DIR)
    message(FATAL_ERROR "FREERTOS_KERNEL_DIR must point to FreeRTOS-Kernel")
endif()

if(NOT DEFINED GENERATED_OSAL_DIR)
    message(FATAL_ERROR "GENERATED_OSAL_DIR must point to the generated ci_osal directory")
endif()

get_filename_component(FREERTOS_KERNEL_DIR "${FREERTOS_KERNEL_DIR}" ABSOLUTE)
get_filename_component(GENERATED_OSAL_DIR "${GENERATED_OSAL_DIR}" ABSOLUTE)

if(NOT EXISTS "${FREERTOS_KERNEL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "FreeRTOS-Kernel was not found at ${FREERTOS_KERNEL_DIR}")
endif()

if(NOT EXISTS "${GENERATED_OSAL_DIR}/ci_osal_osal.c")
    message(FATAL_ERROR "Generated OSAL was not found at ${GENERATED_OSAL_DIR}")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# FreeRTOS-Kernel expects the consuming project to provide this interface target.
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Use the official host simulator port and a real FreeRTOS heap implementation.
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)
set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)
add_subdirectory("${FREERTOS_KERNEL_DIR}" "${CMAKE_BINARY_DIR}/freertos-kernel")

# Build the exact full-set output produced by the previous CI stage.
add_subdirectory("${GENERATED_OSAL_DIR}" "${CMAKE_BINARY_DIR}/generated-osal")
add_subdirectory("${GENERATED_OSAL_DIR}/portable/freertos"
                 "${CMAKE_BINARY_DIR}/generated-osal-freertos")

target_link_libraries(ci_osal_osal_freertos
    PUBLIC
        freertos_kernel
        Threads::Threads
)

target_compile_options(ci_osal_osal
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

target_compile_options(ci_osal_osal_freertos
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

add_executable(ci_osal_freertos_link_check
    ${CMAKE_CURRENT_LIST_DIR}/smoke_main.c
)

target_include_directories(ci_osal_freertos_link_check
    PRIVATE
        ${GENERATED_OSAL_DIR}
        ${GENERATED_OSAL_DIR}/portable/freertos
)

target_link_libraries(ci_osal_freertos_link_check
    PRIVATE
        ci_osal_osal_freertos
        freertos_kernel
        Threads::Threads
)

target_compile_options(ci_osal_freertos_link_check
    PRIVATE
        -Wall
        -Wextra
        -Werror
)
