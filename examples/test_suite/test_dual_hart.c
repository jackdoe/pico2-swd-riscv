#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include <stdio.h>
#include "pico/stdlib.h"

static bool test_independent_hart_control(swd_target_t *target) {
    printf("# Testing independent hart control...\n");

    printf("# Halting hart 0...\n");
    swd_error_t err = rp2350_halt(target, 0);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt hart 0\n");
        test_send_response(RESP_FAIL, "Failed to halt hart 0");
        return false;
    }

    printf("# Resuming hart 1...\n");
    err = rp2350_resume(target, 1);
    if (err != SWD_OK) {
        printf("# Failed to resume hart 1\n");
        test_send_response(RESP_FAIL, "Failed to resume hart 1");
        return false;
    }

    sleep_ms(10);

    printf("# Halting hart 1...\n");
    err = rp2350_halt(target, 1);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt hart 1\n");
        test_send_response(RESP_FAIL, "Failed to halt hart 1");
        return false;
    }

    printf("# Resuming hart 0...\n");
    err = rp2350_resume(target, 0);
    if (err != SWD_OK) {
        printf("# Failed to resume hart 0\n");
        test_send_response(RESP_FAIL, "Failed to resume hart 0");
        return false;
    }

    sleep_ms(10);

    printf("# Independent hart control test passed\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_register_isolation(swd_target_t *target) {
    printf("# Testing per-hart register isolation...\n");

    rp2350_halt(target, 0);
    rp2350_halt(target, 1);

    uint32_t h0_value = 0xAAAAAAAA;
    uint32_t h1_value = 0x55555555;

    printf("# Writing x5 on hart 0: 0x%08lx\n", (unsigned long)h0_value);
    swd_error_t err = rp2350_write_reg(target, 0, 5, h0_value);
    if (err != SWD_OK) {
        printf("# Failed to write hart 0 x5\n");
        test_send_response(RESP_FAIL, "Failed to write hart 0 x5");
        return false;
    }

    printf("# Writing x5 on hart 1: 0x%08lx\n", (unsigned long)h1_value);
    err = rp2350_write_reg(target, 1, 5, h1_value);
    if (err != SWD_OK) {
        printf("# Failed to write hart 1 x5\n");
        test_send_response(RESP_FAIL, "Failed to write hart 1 x5");
        return false;
    }

    swd_result_t h0_readback = rp2350_read_reg(target, 0, 5);
    if (h0_readback.error != SWD_OK || h0_readback.value != h0_value) {
        printf("# Hart 0 x5 corrupted: expected 0x%08lx, got 0x%08lx\n",
               (unsigned long)h0_value, (unsigned long)h0_readback.value);
        test_send_response(RESP_FAIL, "Hart 0 register corrupted");
        return false;
    }

    swd_result_t h1_readback = rp2350_read_reg(target, 1, 5);
    if (h1_readback.error != SWD_OK || h1_readback.value != h1_value) {
        printf("# Hart 1 x5 corrupted: expected 0x%08lx, got 0x%08lx\n",
               (unsigned long)h1_value, (unsigned long)h1_readback.value);
        test_send_response(RESP_FAIL, "Hart 1 register corrupted");
        return false;
    }

    printf("# Register isolation verified\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_execute_on_hart1(swd_target_t *target) {
    printf("# Testing code execution on hart 1...\n");

    rp2350_halt(target, 1);

    uint32_t program[] = {
        0x09900313,
        0x0000006f,
    };

    uint32_t program_addr = 0x20003000;

    printf("# Uploading program to 0x%08lx...\n", (unsigned long)program_addr);
    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
        if (err != SWD_OK) {
            printf("# Failed to upload program\n");
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    rp2350_write_reg(target, 1, 6, 0x00000000);

    swd_error_t err = rp2350_write_pc(target, 1, program_addr);
    if (err != SWD_OK) {
        printf("# Failed to set PC\n");
        test_send_response(RESP_FAIL, "Failed to set PC");
        return false;
    }

    err = rp2350_resume(target, 1);
    if (err != SWD_OK) {
        printf("# Failed to resume hart 1\n");
        test_send_response(RESP_FAIL, "Failed to resume");
        return false;
    }

    sleep_ms(10);

    err = rp2350_halt(target, 1);
    if (err != SWD_OK) {
        printf("# Failed to halt hart 1\n");
        test_send_response(RESP_FAIL, "Failed to halt");
        return false;
    }

    swd_result_t x6 = rp2350_read_reg(target, 1, 6);
    if (x6.error != SWD_OK) {
        printf("# Failed to read x6\n");
        test_send_response(RESP_FAIL, "Failed to read x6");
        return false;
    }

    printf("# Hart 1 x6 after execution: 0x%08lx (expected 0x00000099)\n",
           (unsigned long)x6.value);

    if (x6.value == 0x00000099) {
        printf("# Program executed successfully on hart 1\n");
        test_send_response(RESP_PASS, NULL);
        return true;
    } else {
        printf("# Program execution failed on hart 1\n");
        test_send_response(RESP_FAIL, "Hart 1 execution failed");
        return false;
    }
}

static bool test_hart1_reset(swd_target_t *target) {
    printf("# Testing hart 1 reset...\n");

    swd_error_t err = rp2350_reset(target, 1, true);
    if (err != SWD_OK) {
        printf("# Reset failed: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Reset failed");
        return false;
    }

    swd_result_t pc = rp2350_read_pc(target, 1);
    if (pc.error != SWD_OK) {
        printf("# Failed to read PC after reset\n");
        test_send_response(RESP_FAIL, "Failed to read PC after reset");
        return false;
    }

    printf("# Hart 1 reset successful, PC = 0x%08lx\n", (unsigned long)pc.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_single_step_both_harts(swd_target_t *target) {
    printf("# Testing single-step on both harts independently...\n");

    rp2350_halt(target, 0);
    rp2350_halt(target, 1);

    swd_result_t h0_pc_before = rp2350_read_pc(target, 0);
    swd_result_t h1_pc_before = rp2350_read_pc(target, 1);

    if (h0_pc_before.error != SWD_OK || h1_pc_before.error != SWD_OK) {
        printf("# Failed to read initial PCs\n");
        test_send_response(RESP_FAIL, "Failed to read PCs");
        return false;
    }

    printf("# Hart 0 initial PC: 0x%08lx\n", (unsigned long)h0_pc_before.value);
    printf("# Hart 1 initial PC: 0x%08lx\n", (unsigned long)h1_pc_before.value);

    printf("# Stepping hart 0...\n");
    swd_error_t err = rp2350_step(target, 0);
    if (err != SWD_OK) {
        printf("# Failed to step hart 0: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Hart 0 step failed");
        return false;
    }

    printf("# Stepping hart 1...\n");
    err = rp2350_step(target, 1);
    if (err != SWD_OK) {
        printf("# Failed to step hart 1: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Hart 1 step failed");
        return false;
    }

    swd_result_t h0_pc_after = rp2350_read_pc(target, 0);
    swd_result_t h1_pc_after = rp2350_read_pc(target, 1);

    if (h0_pc_after.error != SWD_OK || h1_pc_after.error != SWD_OK) {
        test_send_response(RESP_FAIL, "Failed to read PCs after step");
        return false;
    }

    printf("# Hart 0 after step: PC = 0x%08lx\n", (unsigned long)h0_pc_after.value);
    printf("# Hart 1 after step: PC = 0x%08lx\n", (unsigned long)h1_pc_after.value);

    if (h0_pc_after.value == h0_pc_before.value) {
        printf("# Hart 0 PC did not advance\n");
        test_send_response(RESP_FAIL, "Hart 0 PC stuck");
        return false;
    }

    if (h1_pc_after.value == h1_pc_before.value) {
        printf("# Hart 1 PC did not advance\n");
        test_send_response(RESP_FAIL, "Hart 1 PC stuck");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_rapid_hart_switching(swd_target_t *target) {
    printf("# Testing rapid hart switching (100 switches)...\n");

    rp2350_halt(target, 0);
    rp2350_halt(target, 1);

    for (uint i = 0; i < 100; i++) {
        if (i % 10 == 0) printf("# Switch %u/100\n", i);

        uint32_t h0_val = 0xA0000000 | i;
        swd_error_t err = rp2350_write_reg(target, 0, 5, h0_val);
        if (err != SWD_OK) {
            printf("# Failed to write hart 0 at iteration %u\n", i);
            test_send_response(RESP_FAIL, "Hart 0 write failed");
            return false;
        }

        uint32_t h1_val = 0xB0000000 | i;
        err = rp2350_write_reg(target, 1, 5, h1_val);
        if (err != SWD_OK) {
            printf("# Failed to write hart 1 at iteration %u\n", i);
            test_send_response(RESP_FAIL, "Hart 1 write failed");
            return false;
        }

        swd_result_t h0_readback = rp2350_read_reg(target, 0, 5);
        if (h0_readback.error != SWD_OK || h0_readback.value != h0_val) {
            printf("# Hart 0 verify failed at iteration %u\n", i);
            test_send_response(RESP_FAIL, "Hart 0 verify failed");
            return false;
        }

        swd_result_t h1_readback = rp2350_read_reg(target, 1, 5);
        if (h1_readback.error != SWD_OK || h1_readback.value != h1_val) {
            printf("# Hart 1 verify failed at iteration %u\n", i);
            test_send_response(RESP_FAIL, "Hart 1 verify failed");
            return false;
        }
    }

    printf("# Rapid hart switching test completed\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

test_case_t dual_hart_tests[] = {
    { "TEST 22: Independent Hart Control", test_independent_hart_control, false, false },
    { "TEST 23: Per-Hart Register Isolation", test_register_isolation, false, false },
    { "TEST 24: Execute Code on Hart 1", test_execute_on_hart1, false, false },
    { "TEST 25: Hart 1 Reset", test_hart1_reset, false, false },
    { "TEST 26: Single-Step Both Harts", test_single_step_both_harts, false, false },
    { "TEST 27: Rapid Hart Switching", test_rapid_hart_switching, false, false },
};

const uint32_t dual_hart_test_count = sizeof(dual_hart_tests) / sizeof(dual_hart_tests[0]);
