add_executable(debugger
    main.c
    commands.c
    rv32i_disasm.c
)

target_link_libraries(debugger
    pico_stdlib
    pico2_swd_riscv
)

pico_enable_stdio_usb(debugger 1)
pico_enable_stdio_uart(debugger 0)

pico_add_extra_outputs(debugger)
