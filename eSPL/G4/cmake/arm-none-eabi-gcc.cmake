# SPDX-License-Identifier: MIT
# Example CMake toolchain file: Arm GCC (arm-none-eabi) for STM32G4 (Cortex-M4F, hard float).
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=<path>/eSPL/G4/cmake/arm-none-eabi-gcc.cmake ...
# The CPU flags are the *_INIT defaults: -DCMAKE_C_FLAGS=... on the command line REPLACES them
# (the build then targets ARM mode and fails); add extra flags with the CFLAGS environment
# variable or target_compile_options.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(_espl_cpu "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${_espl_cpu} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${_espl_cpu}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_espl_cpu} -Wl,--gc-sections --specs=nano.specs")
