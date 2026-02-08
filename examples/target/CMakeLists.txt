cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350-riscv)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "Set PICO_SDK_PATH environment variable")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(debug_target C CXX ASM)

pico_sdk_init()

add_executable(debug_target main.c)

target_link_libraries(debug_target pico_stdlib hardware_gpio)

pico_enable_stdio_usb(debug_target 0)
pico_enable_stdio_uart(debug_target 1)

target_compile_options(debug_target PRIVATE -march=rv32ima_zicsr_zifencei -mabi=ilp32)

pico_add_extra_outputs(debug_target)
