cmake_minimum_required(VERSION 3.13)

# Create the test_suite executable
add_executable(test_suite
    main.c
    test_basic.c
    test_dual_hart.c
    test_framework.c
    test_hart0.c
    test_hart1.c
    test_mem.c
    test_trace.c
    test_api_coverage.c
    test_memory_ops.c
    test_cache.c
    test_code_exec.c
    test_error_paths.c
)

# Link against the library
target_link_libraries(test_suite
    pico2_swd_riscv
    pico_stdlib
)

# Enable USB output, disable UART
pico_enable_stdio_usb(test_suite 1)
pico_enable_stdio_uart(test_suite 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(test_suite)

# Print memory usage
target_link_options(test_suite PRIVATE -Wl,--print-memory-usage)
