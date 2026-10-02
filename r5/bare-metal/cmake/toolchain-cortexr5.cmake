set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER    arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER  arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER  arm-none-eabi-gcc)
set(CMAKE_AR            arm-none-eabi-ar)
set(CMAKE_OBJCOPY       arm-none-eabi-objcopy)

set(CPU_FLAGS "-mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16")

set(CMAKE_C_FLAGS   "${CPU_FLAGS} -Os -ffunction-sections -fdata-sections -Wall" CACHE STRING "")
set(CMAKE_ASM_FLAGS "${CPU_FLAGS}" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS "-T ${CMAKE_SOURCE_DIR}/linker/lscript.ld -nostartfiles -Wl,--gc-sections" CACHE STRING "")

# Sin OS en el target
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
