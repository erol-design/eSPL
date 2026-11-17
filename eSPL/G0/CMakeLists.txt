# SPDX-License-Identifier: MIT
# eSPL STM32G0 - CMake integration (Arm GCC; any C99 cross compiler for the library itself).
#
# Use from a project:
#     set(ESPL_G0_DEVICE STM32G0B1xx)        # one CMSIS device macro, required
#     set(ESPL_G0_CHECK_LEVEL 0)             # 0 = SPL_CHECK_FULL (default), 2 = SPL_CHECK_FAST
#     add_subdirectory(<path>/eSPL/G0 espl_g0)
#     target_link_libraries(app PRIVATE espl::g0 espl::g0_startup)
#     #include "stm32g0_spl.h"
#
# Targets:
#   espl::g0          static library, every file in Src/. Device macro, SPL_CHECK_LEVEL and the
#                     include paths (Inc/, CMSIS) are PUBLIC: the application is compiled with the
#                     same check level as the library (a mixed build does not link,
#                     Docs/CHECK_LEVEL.md 3.2).
#   espl::g0_startup  object library, CMSIS system file + GCC startup file of the device (GNU
#                     assembler syntax; only created for a GNU-compatible compiler). The linker
#                     script is the application's (example: Examples/G0_Template/stm32g0b1re.ld).
# The library builds no vector table, no linker script and no compiler flags beyond C99: the
# CPU flags (-mcpu=cortex-m0plus -mthumb) come from the toolchain file
# (example: cmake/arm-none-eabi-gcc.cmake). Verified by Tests/run_cmake.sh.
cmake_minimum_required(VERSION 3.15)

# Version: single source is Inc/stm32g0_status.h (SPL_VERSION_*).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/Inc/stm32g0_status.h" _espl_ver REGEX "#define SPL_VERSION_")
foreach(_part MAJOR MINOR PATCH)
    string(REGEX REPLACE ".*SPL_VERSION_${_part} ([0-9]+)U.*" "\\1" _espl_${_part} "${_espl_ver}")
endforeach()
project(espl_g0 VERSION ${_espl_MAJOR}.${_espl_MINOR}.${_espl_PATCH} LANGUAGES C ASM)

set(_espl_devices STM32G030xx STM32G031xx STM32G041xx STM32G050xx STM32G051xx STM32G061xx
    STM32G070xx STM32G071xx STM32G081xx STM32G0B0xx STM32G0B1xx STM32G0C1xx)
set(ESPL_G0_DEVICE "" CACHE STRING "CMSIS device macro, one of: ${_espl_devices}")
set(ESPL_G0_CHECK_LEVEL 0 CACHE STRING "SPL_CHECK_LEVEL: 0 (FULL) or 2 (FAST)")
if(NOT ESPL_G0_DEVICE IN_LIST _espl_devices)
    message(FATAL_ERROR "eSPL G0: ESPL_G0_DEVICE='${ESPL_G0_DEVICE}' is not one of: ${_espl_devices}")
endif()
if(NOT ESPL_G0_CHECK_LEVEL STREQUAL "0" AND NOT ESPL_G0_CHECK_LEVEL STREQUAL "2")
    message(FATAL_ERROR "eSPL G0: ESPL_G0_CHECK_LEVEL must be 0 (FULL) or 2 (FAST), got '${ESPL_G0_CHECK_LEVEL}'")
endif()

file(GLOB _espl_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Src/*.c")
add_library(espl_g0 STATIC ${_espl_sources})
add_library(espl::g0 ALIAS espl_g0)
target_include_directories(espl_g0
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/Inc"
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Core/Include"
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Device/Include"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/Private")
target_compile_definitions(espl_g0 PUBLIC ${ESPL_G0_DEVICE} SPL_CHECK_LEVEL=${ESPL_G0_CHECK_LEVEL})
target_compile_features(espl_g0 PUBLIC c_std_99)
set_target_properties(espl_g0 PROPERTIES C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    string(TOLOWER "${ESPL_G0_DEVICE}" _espl_dev_lower)
    add_library(espl_g0_startup OBJECT
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Device/Source/system_stm32g0xx.c"
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Device/Source/startup/gcc/startup_${_espl_dev_lower}.s")
    add_library(espl::g0_startup ALIAS espl_g0_startup)
    target_link_libraries(espl_g0_startup PUBLIC espl_g0)
endif()
