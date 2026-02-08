#include <stdio.h>
#include "pico/stdlib.h"
#include <pico2-swd-riscv/swd.h>
#include <pico2-swd-riscv/rp2350.h>

int main() {
    stdio_init_all();
    sleep_ms(2000);

    swd_config_t config = swd_config_default();
    config.pin_swclk = 2;
    config.pin_swdio = 3;
    config.freq_khz = 1000;

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

    err = rp2350_halt(target, 0);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("halt failed: %s\n", swd_error_string(err));
        swd_target_destroy(target);
        return 1;
    }

    swd_result_t pc = rp2350_read_pc(target, 0);
    if (pc.error == SWD_OK)
        printf("PC = 0x%08x\n", pc.value);

    uint32_t regs[32];
    if (rp2350_read_all_regs(target, 0, regs) == SWD_OK) {
        for (int i = 0; i < 32; i += 4)
            printf("x%2d=%08x x%2d=%08x x%2d=%08x x%2d=%08x\n",
                   i, regs[i], i+1, regs[i+1], i+2, regs[i+2], i+3, regs[i+3]);
    }

    rp2350_resume(target, 0);
    swd_target_destroy(target);
    return 0;
}
