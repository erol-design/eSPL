# SPDX-License-Identifier: MIT
# eSPL STM32G4 - CMake integration (Arm GCC; any C99 cross compiler for the library itself).
#
# Use from a project:
#     set(ESPL_G4_DEVICE STM32G431xx)        # one CMSIS device macro, required
#     set(ESPL_G4_CHECK_LEVEL 0)             # 0 = SPL_CHECK_FULL (default), 2 = SPL_CHECK_FAST
#     add_subdirectory(<path>/eSPL/G4 espl_g4)
#     target_link_libraries(app PRIVATE espl::g4 espl::g4_startup)
#     #include "stm32g4_spl.h"
#
# Targets:
#   espl::g4          static library, every file in Src/. Device macro, SPL_CHECK_LEVEL and the
#                     include paths (Inc/, CMSIS) are PUBLIC: the application is compiled with the
#                     same check level as the library (a mixed build does not link,
#                     ../Common/Inc/spl_check.h).
#   espl::g4_startup  object library, CMSIS system file + GCC startup file of the device (GNU
#                     assembler syntax; only created for a GNU-compatible compiler). The linker
#                     script is the application's (example: Examples/G431_Template).
# The library builds no vector table, no linker script and no compiler flags beyond C99: the
# CPU flags (-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard) come from the toolchain
# file (example: cmake/arm-none-eabi-gcc.cmake). Verified by Tests/run_cmake.sh.
cmake_minimum_required(VERSION 3.15)

# Version: single source is Inc/stm32g4_status.h (SPL_VERSION_*).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/Inc/stm32g4_status.h" _espl_ver REGEX "#define SPL_VERSION_")
foreach(_part MAJOR MINOR PATCH)
    string(REGEX REPLACE ".*SPL_VERSION_${_part} ([0-9]+)U.*" "\\1" _espl_${_part} "${_espl_ver}")
endforeach()
project(espl_g4 VERSION ${_espl_MAJOR}.${_espl_MINOR}.${_espl_PATCH} LANGUAGES C ASM)

set(_espl_devices STM32G411xB STM32G411xC STM32G414xx STM32G431xx STM32G441xx STM32G471xx
    STM32G473xx STM32G474xx STM32G483xx STM32G484xx STM32G491xx STM32G4A1xx)
set(ESPL_G4_DEVICE "" CACHE STRING "CMSIS device macro, one of: ${_espl_devices}")
set(ESPL_G4_CHECK_LEVEL 0 CACHE STRING "SPL_CHECK_LEVEL: 0 (FULL) or 2 (FAST)")
if(NOT ESPL_G4_DEVICE IN_LIST _espl_devices)
    message(FATAL_ERROR "eSPL G4: ESPL_G4_DEVICE='${ESPL_G4_DEVICE}' is not one of: ${_espl_devices}")
endif()
if(NOT ESPL_G4_CHECK_LEVEL STREQUAL "0" AND NOT ESPL_G4_CHECK_LEVEL STREQUAL "2")
    message(FATAL_ERROR "eSPL G4: ESPL_G4_CHECK_LEVEL must be 0 (FULL) or 2 (FAST), got '${ESPL_G4_CHECK_LEVEL}'")
endif()

file(GLOB _espl_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Src/*.c")
add_library(espl_g4 STATIC ${_espl_sources})
add_library(espl::g4 ALIAS espl_g4)
target_include_directories(espl_g4
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/Inc"
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Core/Include"
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Device/Include"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/Private")
target_compile_definitions(espl_g4 PUBLIC ${ESPL_G4_DEVICE} SPL_CHECK_LEVEL=${ESPL_G4_CHECK_LEVEL})
target_compile_features(espl_g4 PUBLIC c_std_99)
set_target_properties(espl_g4 PROPERTIES C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    string(TOLOWER "${ESPL_G4_DEVICE}" _espl_dev_lower)
    add_library(espl_g4_startup OBJECT
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Device/Source/system_stm32g4xx.c"
        "${CMAKE_CURRENT_LIST_DIR}/CMSIS/Device/Source/startup/gcc/startup_${_espl_dev_lower}.s")
    add_library(espl::g4_startup ALIAS espl_g4_startup)
    target_link_libraries(espl_g4_startup PUBLIC espl_g4)
endif()
