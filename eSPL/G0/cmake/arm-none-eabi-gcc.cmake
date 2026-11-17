# SPDX-License-Identifier: MIT
# Example CMake toolchain file: Arm GCC (arm-none-eabi) for STM32G0 (Cortex-M0+).
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=<path>/eSPL/G0/cmake/arm-none-eabi-gcc.cmake ...
# The CPU flags are the *_INIT defaults: -DCMAKE_C_FLAGS=... on the command line REPLACES them
# (the build then targets ARM mode and fails); add extra flags with the CFLAGS environment
# variable or target_compile_options.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m0plus -mthumb -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m0plus -mthumb")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mcpu=cortex-m0plus -mthumb -Wl,--gc-sections --specs=nano.specs")
