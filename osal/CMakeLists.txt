# Base OSAL template build script.
# The generator replaces the "template" prefix in target and file names.

add_library(template_osal STATIC
    template_osal.c
)

target_include_directories(template_osal
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)
