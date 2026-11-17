# SPDX-License-Identifier: MIT
# G0_Template with CMake + Arm GCC (the Keil project G0_Template.uvprojx builds the same main.c).
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi-gcc.cmake
#   cmake --build build
# Output: build/G0_Template.elf, .hex and .bin. Default device and linker script: STM32G0B1RETx
# (NUCLEO-G0B1RE); another SKU: -DESPL_G0_DEVICE=... and -DG0_TEMPLATE_LINKER_SCRIPT=....
cmake_minimum_required(VERSION 3.15)
project(G0_Template LANGUAGES C ASM)

set(ESPL_G0_DEVICE STM32G0B1xx CACHE STRING "CMSIS device macro")
set(G0_TEMPLATE_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/stm32g0b1re.ld" CACHE FILEPATH "GNU ld script")

add_subdirectory(../.. espl_g0)

add_executable(G0_Template main.c)
set_target_properties(G0_Template PROPERTIES SUFFIX ".elf" C_EXTENSIONS OFF)
target_compile_options(G0_Template PRIVATE -Wall -Wextra -Wconversion -Werror)
target_link_libraries(G0_Template PRIVATE espl::g0 espl::g0_startup)
target_link_options(G0_Template PRIVATE "-T${G0_TEMPLATE_LINKER_SCRIPT}"
    "-Wl,-Map=$<TARGET_FILE_DIR:G0_Template>/G0_Template.map")
set_property(TARGET G0_Template APPEND PROPERTY LINK_DEPENDS "${G0_TEMPLATE_LINKER_SCRIPT}")

find_program(ESPL_OBJCOPY NAMES arm-none-eabi-objcopy)
if(ESPL_OBJCOPY)
    add_custom_command(TARGET G0_Template POST_BUILD
        COMMAND ${ESPL_OBJCOPY} -O ihex $<TARGET_FILE:G0_Template> $<TARGET_FILE_DIR:G0_Template>/G0_Template.hex
        COMMAND ${ESPL_OBJCOPY} -O binary $<TARGET_FILE:G0_Template> $<TARGET_FILE_DIR:G0_Template>/G0_Template.bin
        VERBATIM)
endif()
