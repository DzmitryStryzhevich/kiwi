# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

# FreeRTOS OSAL port template build script.
# FreeRTOS include paths and libraries are expected to be provided by the parent project.

add_library(template_osal_freertos STATIC
    template_osal_freertos.c
)

target_include_directories(template_osal_freertos
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../..
)

target_link_libraries(template_osal_freertos
    PUBLIC
        template_osal
)
