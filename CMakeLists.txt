cmake_minimum_required(VERSION 3.13)
if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

if(NOT TARGET pico_stdlib)
    if(NOT DEFINED ENV{PICO_SDK_PATH})
        message(FATAL_ERROR "PICO_SDK_PATH environment variable not set.\n"
                "Please set it to your Pico SDK installation directory.\n"
                "Example: export PICO_SDK_PATH=/path/to/pico-sdk")
    endif()

    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

    project(pico2_swd_riscv C CXX ASM)

    pico_sdk_init()
else()
    message(STATUS "pico2-swd-riscv: Building as subproject")
endif()

set(PICO2_SWD_RISCV_VERSION_MAJOR 1)
set(PICO2_SWD_RISCV_VERSION_MINOR 0)
set(PICO2_SWD_RISCV_VERSION_PATCH 0)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/include/pico2-swd-riscv/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/pico2-swd-riscv/version.h
)

add_library(pico2_swd_riscv STATIC
    src/swd.c
    src/swd_protocol.c
    src/dap.c
    src/rp2350.c
)

pico_generate_pio_header(pico2_swd_riscv ${CMAKE_CURRENT_LIST_DIR}/src/swd.pio)

target_include_directories(pico2_swd_riscv PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pico2_swd_riscv PUBLIC
    pico_stdlib
    hardware_pio
    hardware_gpio
    hardware_clocks
)

if(NOT DEFINED PICO2_SWD_DEBUG_LEVEL)
    set(PICO2_SWD_DEBUG_LEVEL 1 CACHE STRING "Debug level: 0=none, 1=warn, 2=info, 3=debug")
endif()

target_compile_definitions(pico2_swd_riscv PUBLIC
    PICO2_SWD_DEBUG_LEVEL=${PICO2_SWD_DEBUG_LEVEL}
)

target_compile_options(pico2_swd_riscv PRIVATE
    -Wall
    -Wextra
    -Wunused-function
    -Wunused-variable
    -Wunused-but-set-variable
    -Wunreachable-code
    -fdata-sections
    -ffunction-sections
)

target_link_options(pico2_swd_riscv PRIVATE
    -Wl,--gc-sections
)

option(PICO2_SWD_BUILD_EXAMPLES "Build example programs" OFF)
if(PICO2_SWD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
