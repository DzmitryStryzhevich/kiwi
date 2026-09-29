# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

# POSIX OSAL port generated build script.

find_package(Threads REQUIRED)

add_library(template_osal_posix STATIC
    template_osal_posix.c
)

target_include_directories(template_osal_posix
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../..
)

target_compile_features(template_osal_posix PUBLIC c_std_99)

target_compile_definitions(template_osal_posix
    PUBLIC
        _POSIX_C_SOURCE=200809L
)

target_link_libraries(template_osal_posix
    PUBLIC
        template_osal
        Threads::Threads
)
