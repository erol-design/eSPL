# SPDX-License-Identifier: MIT
# Basic examples, one image per driver, CMake + Arm GCC:
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi-gcc.cmake
#   cmake --build build
# Output: build/ex_<name>.elf, .hex and .bin. Default device and linker script: STM32G0B1RETx
# (NUCLEO-G0B1RE); another SKU: -DESPL_G0_DEVICE=... and -DEX_LINKER_SCRIPT=....
cmake_minimum_required(VERSION 3.15)
project(G0_Basic_Examples LANGUAGES C ASM)

set(ESPL_G0_DEVICE STM32G0B1xx CACHE STRING "CMSIS device macro")
set(EX_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../G0_Template/stm32g0b1re.ld" CACHE FILEPATH
    "GNU ld script")

add_subdirectory(../.. espl_g0)

set(EX_NAMES gpio exti usart rcc tim_pwm tim_irq adc dac dma crc spi i2c iwdg rtc flash)

add_library(ex_board OBJECT ex_board.c)
target_link_libraries(ex_board PUBLIC espl::g0)
target_compile_options(ex_board PRIVATE -Wall -Wextra -Wconversion -Werror)
set_target_properties(ex_board PROPERTIES C_EXTENSIONS OFF)

find_program(ESPL_OBJCOPY NAMES arm-none-eabi-objcopy)
foreach(name IN LISTS EX_NAMES)
    set(t ex_${name})
    add_executable(${t} ${t}.c $<TARGET_OBJECTS:ex_board>)
    set_target_properties(${t} PROPERTIES SUFFIX ".elf" C_EXTENSIONS OFF)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wconversion -Werror)
    target_link_libraries(${t} PRIVATE espl::g0 espl::g0_startup)
    target_link_options(${t} PRIVATE "-T${EX_LINKER_SCRIPT}"
        "-Wl,-Map=$<TARGET_FILE_DIR:${t}>/${t}.map")
    set_property(TARGET ${t} APPEND PROPERTY LINK_DEPENDS "${EX_LINKER_SCRIPT}")
    if(ESPL_OBJCOPY)
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${ESPL_OBJCOPY} -O ihex $<TARGET_FILE:${t}> $<TARGET_FILE_DIR:${t}>/${t}.hex
            COMMAND ${ESPL_OBJCOPY} -O binary $<TARGET_FILE:${t}> $<TARGET_FILE_DIR:${t}>/${t}.bin
            VERBATIM)
    endif()
endforeach()
