#ifndef DEBUGGER_COMMANDS_H
#define DEBUGGER_COMMANDS_H

#include <pico2-swd-riscv/types.h>

typedef struct {
    const char *name;
    const char *usage;
    void (*handler)(swd_target_t *target, uint8_t *hart, int argc, char **argv);
} command_t;

extern const command_t *debugger_commands;
extern const int command_count;

#endif
