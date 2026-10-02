cmake_minimum_required(VERSION 3.20)
project(firmware-r5 C ASM)

set(OUTPUT_BINARY_NAME firmware-r5)

# ---------------------------------------------------------------------------
# Firmware executable (bare metal, no BSP / external libraries)
# ---------------------------------------------------------------------------
add_executable(firmware
    src/start.S
    src/main.c
    inc/xparameters.h
    inc/main.h
)
set_target_properties(firmware PROPERTIES OUTPUT_NAME ${OUTPUT_BINARY_NAME} SUFFIX .elf)

target_include_directories(firmware PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/inc
)

target_link_options(firmware PRIVATE
    -Wl,-Map=${OUTPUT_BINARY_NAME}.map
)

# libgcc provides compiler helpers (e.g. __aeabi_uldivmod for 64-bit division)
target_link_libraries(firmware PRIVATE gcc)

set_property(TARGET firmware APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/linker/lscript.ld)

add_custom_command(TARGET firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${OUTPUT_BINARY_NAME}.elf ${OUTPUT_BINARY_NAME}.bin
)
