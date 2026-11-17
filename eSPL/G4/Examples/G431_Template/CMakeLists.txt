# SPDX-License-Identifier: MIT
# G431_Template with CMake + Arm GCC (the Keil project G431_Template.uvprojx builds the same main.c).
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi-gcc.cmake
#   cmake --build build
# Output: build/G431_Template.elf, .hex and .bin. Default device and linker script: STM32G431RBTx
# (NUCLEO-G431RB); another SKU: -DESPL_G4_DEVICE=... and -DG4_TEMPLATE_LINKER_SCRIPT=....
# Check level: -DESPL_G4_CHECK_LEVEL=0 (FULL, default) or 2 (FAST), the same for the whole build.
cmake_minimum_required(VERSION 3.15)
project(G431_Template LANGUAGES C ASM)

set(ESPL_G4_DEVICE STM32G431xx CACHE STRING "CMSIS device macro")
set(G4_TEMPLATE_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/STM32G431xB_FLASH.ld" CACHE FILEPATH "GNU ld script")

add_subdirectory(../.. espl_g4)

add_executable(G431_Template main.c)
set_target_properties(G431_Template PROPERTIES SUFFIX ".elf" C_EXTENSIONS OFF)
target_compile_options(G431_Template PRIVATE -Wall -Wextra -Wconversion -Werror)
target_link_libraries(G431_Template PRIVATE espl::g4 espl::g4_startup)
target_link_options(G431_Template PRIVATE "-T${G4_TEMPLATE_LINKER_SCRIPT}"
    "-Wl,-Map=$<TARGET_FILE_DIR:G431_Template>/G431_Template.map")
set_property(TARGET G431_Template APPEND PROPERTY LINK_DEPENDS "${G4_TEMPLATE_LINKER_SCRIPT}")

find_program(ESPL_OBJCOPY NAMES arm-none-eabi-objcopy)
if(ESPL_OBJCOPY)
    add_custom_command(TARGET G431_Template POST_BUILD
        COMMAND ${ESPL_OBJCOPY} -O ihex $<TARGET_FILE:G431_Template> $<TARGET_FILE_DIR:G431_Template>/G431_Template.hex
        COMMAND ${ESPL_OBJCOPY} -O binary $<TARGET_FILE:G431_Template> $<TARGET_FILE_DIR:G431_Template>/G431_Template.bin
        VERBATIM)
endif()
