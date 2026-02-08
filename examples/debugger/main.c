#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include <pico2-swd-riscv/swd.h>
#include <pico2-swd-riscv/rp2350.h>
#include "commands.h"

#define MAX_LINE 256
#define MAX_ARGS 32

static int tokenize(char *line, char **argv, int max) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

static void repl(swd_target_t *target) {
    char line[MAX_LINE];
    char *argv[MAX_ARGS];
    uint8_t hart = 0;

    printf("\ntype 'help' for commands\n\n");

    for (;;) {
        printf("hart%u> ", hart);
        fflush(stdout);

        int pos = 0;
        for (;;) {
            int c = getchar();
            if (c == EOF) {
                tight_loop_contents();
                continue;
            }
            if (c == '\n' || c == '\r') break;
            if (pos < MAX_LINE - 1)
                line[pos++] = (char)c;
        }
        line[pos] = '\0';

        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        bool found = false;
        for (int i = 0; i < command_count; i++) {
            if (strcmp(argv[0], debugger_commands[i].name) == 0) {
                debugger_commands[i].handler(target, &hart, argc, argv);
                found = true;
                break;
            }
        }
        if (!found)
            printf("unknown command: %s (try 'help')\n", argv[0]);
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("\npico2-swd-riscv debugger\n");
    printf("========================\n");

    swd_config_t config = swd_config_default();
    config.pin_swclk = 2;
    config.pin_swdio = 3;
    config.freq_khz = 1000;
    config.enable_caching = true;

    swd_target_t *target = swd_target_create(&config);
    if (!target) {
        printf("failed to create target\n");
        return 1;
    }

    swd_error_t err = swd_connect(target);
    if (err != SWD_OK) {
        printf("connect failed: %s\n", swd_error_string(err));
        swd_target_destroy(target);
        return 1;
    }

    printf("%s\n", swd_get_target_info(target));

    err = rp2350_init(target);
    if (err != SWD_OK) {
        printf("DM init failed: %s\n", swd_error_string(err));
        swd_target_destroy(target);
        return 1;
    }

    printf("debug module ready\n");

    repl(target);

    swd_target_destroy(target);
    return 0;
}
