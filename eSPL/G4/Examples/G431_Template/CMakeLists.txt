# eSPL G431_Template, Arm GCC derlemesi.
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake
#   cmake --build build
# Cikti: build/G431_Template.elf, .hex, .bin
cmake_minimum_required(VERSION 3.20)
project(G431_Template C ASM)

set(ESPL_G4 ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(ESPL_COMMON ${ESPL_G4}/../Common)

# Cihaz ve denetim seviyesi: SPL_CHECK_LEVEL 0 = FULL, 1 = MIX, 2 = FAST (butun derlemede ayni).
set(DEVICE STM32G431xx)
set(SPL_CHECK_LEVEL 0 CACHE STRING "eSPL denetim seviyesi (0 FULL, 1 MIX, 2 FAST)")

add_executable(${PROJECT_NAME}
    main.c
    ${ESPL_G4}/Src/stm32g4_check.c
    ${ESPL_G4}/Src/stm32g4_flash.c
    ${ESPL_G4}/Src/stm32g4_gpio.c
    ${ESPL_G4}/Src/stm32g4_pwr.c
    ${ESPL_G4}/Src/stm32g4_rcc.c
    ${ESPL_G4}/Src/stm32g4_usart.c
    ${ESPL_G4}/CMSIS/Device/Source/system_stm32g4xx.c
    ${ESPL_G4}/CMSIS/Device/Source/startup/gcc/startup_stm32g431xx.s
)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX .elf)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ESPL_G4}/Inc
    ${ESPL_G4}/Private
    ${ESPL_COMMON}/Inc
    ${ESPL_G4}/CMSIS/Device/Include
    ${ESPL_G4}/CMSIS/Core/Include
)
target_compile_definitions(${PROJECT_NAME} PRIVATE ${DEVICE} SPL_CHECK_LEVEL=${SPL_CHECK_LEVEL})

set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
target_compile_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    $<$<COMPILE_LANGUAGE:C>:-std=c99 -Wall -Wextra -O2 -g -ffunction-sections -fdata-sections>
    $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>
)
target_link_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32G431xB_FLASH.ld
    --specs=nano.specs
    -Wl,--gc-sections -Wl,-Map=${PROJECT_NAME}.map -Wl,--print-memory-usage
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
)
