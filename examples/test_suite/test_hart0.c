#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include <stdio.h>
#include "pico/stdlib.h"

static bool test_halt_hart0(swd_target_t *target) {
    printf("# Halting hart 0...\n");

    swd_error_t err = rp2350_halt(target, 0);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, swd_error_string(err));
        return false;
    }

    printf("# Hart 0 halted\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_read_pc_hart0(swd_target_t *target) {
    printf("# Reading PC from hart 0...\n");

    rp2350_halt(target, 0);

    swd_result_t pc = rp2350_read_pc(target, 0);
    if (pc.error != SWD_OK) {
        printf("# Failed to read PC: %s\n", swd_error_string(pc.error));
        test_send_response(RESP_FAIL, swd_error_string(pc.error));
        return false;
    }

    printf("# PC = 0x%08lx\n", (unsigned long)pc.value);
    test_send_value(pc.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_read_all_gprs(swd_target_t *target) {
    printf("# Reading all 32 GPRs from hart 0...\n");

    rp2350_halt(target, 0);

    for (uint8_t i = 0; i < 32; i++) {
        swd_result_t reg = rp2350_read_reg(target, 0, i);
        if (reg.error != SWD_OK) {
            printf("# Failed to read x%u: %s\n", i, swd_error_string(reg.error));
            test_send_response(RESP_FAIL, "Failed to read register");
            return false;
        }
        printf("# x%u = 0x%08lx\n", i, (unsigned long)reg.value);
    }

    swd_result_t x0 = rp2350_read_reg(target, 0, 0);
    if (x0.error != SWD_OK || x0.value != 0) {
        printf("# x0 should be 0, got 0x%08lx\n", (unsigned long)x0.value);
        test_send_response(RESP_FAIL, "x0 not zero");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_write_verify_gprs(swd_target_t *target) {
    printf("# Writing and verifying x1-x31...\n");

    rp2350_halt(target, 0);

    for (uint8_t i = 1; i < 32; i++) {
        uint32_t test_value = 0xA5A50000 | i;

        swd_error_t err = rp2350_write_reg(target, 0, i, test_value);
        if (err != SWD_OK) {
            printf("# Failed to write x%u: %s\n", i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Failed to write register");
            return false;
        }

        swd_result_t readback = rp2350_read_reg(target, 0, i);
        if (readback.error != SWD_OK || readback.value != test_value) {
            printf("# Verification failed for x%u: wrote 0x%08lx, read 0x%08lx\n",
                   i, (unsigned long)test_value, (unsigned long)readback.value);
            test_send_response(RESP_FAIL, "Register verification failed");
            return false;
        }
    }

    printf("# All registers written and verified\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_write_verify_pc(swd_target_t *target) {
    printf("# Writing and verifying PC...\n");

    rp2350_halt(target, 0);

    uint32_t test_pc = 0x20000100;
    swd_error_t err = rp2350_write_pc(target, 0, test_pc);
    if (err != SWD_OK) {
        printf("# Failed to write PC: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Failed to write PC");
        return false;
    }

    swd_result_t readback = rp2350_read_pc(target, 0);
    if (readback.error != SWD_OK || readback.value != test_pc) {
        printf("# PC verification failed: wrote 0x%08lx, read 0x%08lx\n",
               (unsigned long)test_pc, (unsigned long)readback.value);
        test_send_response(RESP_FAIL, "PC verification failed");
        return false;
    }

    printf("# PC written and verified: 0x%08lx\n", (unsigned long)test_pc);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_read_rom(swd_target_t *target) {
    printf("# Reading ROM at 0x00000000...\n");

    swd_result_t result = rp2350_read_mem32(target, 0x00000000);
    if (result.error != SWD_OK) {
        printf("# Failed to read ROM: %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Failed to read ROM");
        return false;
    }

    printf("# ROM[0x00000000] = 0x%08lx\n", (unsigned long)result.value);

    if (result.value == 0x00000000 || result.value == 0xFFFFFFFF) {
        printf("# ROM contains blank value, expected valid bootstrap code\n");
        test_send_response(RESP_FAIL, "ROM appears blank");
        return false;
    }

    test_send_value(result.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_write_verify_sram(swd_target_t *target) {
    printf("# Writing and verifying SRAM...\n");

    uint32_t test_addr = 0x20000000;
    uint32_t test_data = 0xDEADBEEF;

    swd_error_t err = rp2350_write_mem32(target, test_addr, test_data);
    if (err != SWD_OK) {
        printf("# Failed to write SRAM: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Failed to write SRAM");
        return false;
    }

    swd_result_t readback = rp2350_read_mem32(target, test_addr);
    if (readback.error != SWD_OK || readback.value != test_data) {
        printf("# SRAM verification failed: wrote 0x%08lx, read 0x%08lx\n",
               (unsigned long)test_data, (unsigned long)readback.value);
        test_send_response(RESP_FAIL, "SRAM verification failed");
        return false;
    }

    printf("# SRAM written and verified\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_resume_hart0(swd_target_t *target) {
    printf("# Resuming hart 0...\n");

    rp2350_halt(target, 0);

    swd_error_t err = rp2350_resume(target, 0);
    if (err != SWD_OK) {
        printf("# Failed to resume: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, swd_error_string(err));
        return false;
    }

    printf("# Hart 0 resumed\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_halt_resume_stress(swd_target_t *target) {
    printf("# Running halt/resume stress test (100 cycles)...\n");

    for (uint i = 0; i < 100; i++) {
        if (i % 10 == 0) printf("# Cycle %u/100\n", i);

        swd_error_t err = rp2350_halt(target, 0);
        if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
            printf("# Halt failed at cycle %u: %s\n", i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Halt failed during stress test");
            return false;
        }

        err = rp2350_resume(target, 0);
        if (err != SWD_OK) {
            printf("# Resume failed at cycle %u: %s\n", i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Resume failed during stress test");
            return false;
        }
    }

    printf("# Halt/resume stress test completed\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_register_stress(swd_target_t *target) {
    printf("# Running register stress test (1000 operations)...\n");

    rp2350_halt(target, 0);

    const uint32_t patterns[] = {
        0x00000000, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555,
        0x12345678, 0x87654321, 0xDEADBEEF, 0xCAFEBABE
    };

    for (uint iter = 0; iter < 125; iter++) {
        if (iter % 25 == 0) printf("# Iteration %u/125\n", iter);

        for (uint pat = 0; pat < 8; pat++) {
            for (uint8_t reg = 5; reg <= 12; reg++) {
                uint32_t value = patterns[pat] ^ (reg << 24);

                swd_error_t err = rp2350_write_reg(target, 0, reg, value);
                if (err != SWD_OK) {
                    printf("# Write failed at iter %u, reg x%u\n", iter, reg);
                    test_send_response(RESP_FAIL, "Register write failed");
                    return false;
                }

                swd_result_t readback = rp2350_read_reg(target, 0, reg);
                if (readback.error != SWD_OK || readback.value != value) {
                    printf("# Readback failed at iter %u, reg x%u\n", iter, reg);
                    test_send_response(RESP_FAIL, "Register readback failed");
                    return false;
                }
            }
        }
    }

    printf("# Register stress test completed (1000 operations)\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_stress(swd_target_t *target) {
    printf("# Running memory stress test...\n");

    uint32_t base_addr = 0x20001000;

    printf("# Testing walking 1s pattern...\n");
    for (uint i = 0; i < 32; i++) {
        uint32_t pattern = 1u << i;
        uint32_t addr = base_addr + (i * 4);

        swd_error_t err = rp2350_write_mem32(target, addr, pattern);
        if (err != SWD_OK) {
            printf("# Write failed at walking 1s test, bit %u\n", i);
            test_send_response(RESP_FAIL, "Memory write failed");
            return false;
        }
    }

    for (uint i = 0; i < 32; i++) {
        uint32_t expected = 1u << i;
        uint32_t addr = base_addr + (i * 4);

        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != expected) {
            printf("# Verify failed at walking 1s test, bit %u\n", i);
            test_send_response(RESP_FAIL, "Memory verify failed");
            return false;
        }
    }

    printf("# Testing block operations (256 words)...\n");
    for (uint i = 0; i < 256; i++) {
        uint32_t value = 0xA5000000 | i;
        uint32_t addr = base_addr + (i * 4);

        swd_error_t err = rp2350_write_mem32(target, addr, value);
        if (err != SWD_OK) {
            printf("# Block write failed at word %u\n", i);
            test_send_response(RESP_FAIL, "Block write failed");
            return false;
        }
    }

    for (uint i = 0; i < 256; i++) {
        uint32_t expected = 0xA5000000 | i;
        uint32_t addr = base_addr + (i * 4);

        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != expected) {
            printf("# Block verify failed at word %u\n", i);
            test_send_response(RESP_FAIL, "Block verify failed");
            return false;
        }
    }

    printf("# Memory stress test completed\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_execute_small_program(swd_target_t *target) {
    printf("# Testing program upload and execution...\n");

    rp2350_halt(target, 0);

    uint32_t program[] = {
        0x04200293,
        0x0000006f,
    };

    uint32_t program_addr = 0x20002000;

    printf("# Uploading program to 0x%08lx...\n", (unsigned long)program_addr);
    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
        if (err != SWD_OK) {
            printf("# Failed to upload instruction %u\n", i);
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    rp2350_write_reg(target, 0, 5, 0x00000000);

    swd_error_t err = rp2350_write_pc(target, 0, program_addr);
    if (err != SWD_OK) {
        printf("# Failed to set PC\n");
        test_send_response(RESP_FAIL, "Failed to set PC");
        return false;
    }

    err = rp2350_resume(target, 0);
    if (err != SWD_OK) {
        printf("# Failed to resume\n");
        test_send_response(RESP_FAIL, "Failed to resume");
        return false;
    }

    sleep_ms(10);

    err = rp2350_halt(target, 0);
    if (err != SWD_OK) {
        printf("# Failed to halt\n");
        test_send_response(RESP_FAIL, "Failed to halt");
        return false;
    }

    swd_result_t x5 = rp2350_read_reg(target, 0, 5);
    if (x5.error != SWD_OK || x5.value != 0x00000042) {
        printf("# Program verification failed: x5 = 0x%08lx (expected 0x00000042)\n",
               (unsigned long)x5.value);
        test_send_response(RESP_FAIL, "Program execution failed");
        return false;
    }

    printf("# Program executed successfully\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_instruction_tracing(swd_target_t *target) {
    printf("# Testing instruction tracing (3 steps through linear code)...\n");

    rp2350_halt(target, 0);

    uint32_t program[] = {
        0x00128293,
        0x00230313,
        0x00338393,
        0x0000006f,
    };

    uint32_t program_addr = 0x20002000;
    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
        if (err != SWD_OK) {
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    rp2350_write_pc(target, 0, program_addr);

    swd_result_t initial_pc = rp2350_read_pc(target, 0);
    if (initial_pc.error != SWD_OK || initial_pc.value != program_addr) {
        test_send_response(RESP_FAIL, "Failed to set initial PC");
        return false;
    }

    printf("# Starting PC: 0x%08lx\n", (unsigned long)initial_pc.value);

    uint32_t last_pc = initial_pc.value;
    for (uint i = 0; i < 3; i++) {
        swd_error_t err = rp2350_step(target, 0);
        if (err != SWD_OK) {
            printf("# Step %u failed: %s\n", i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Single-step failed");
            return false;
        }

        swd_result_t pc = rp2350_read_pc(target, 0);
        if (pc.error != SWD_OK) {
            test_send_response(RESP_FAIL, "Failed to read PC after step");
            return false;
        }

        printf("# Step %u: PC = 0x%08lx\n", i + 1, (unsigned long)pc.value);

        if (pc.value == last_pc) {
            printf("# PC did not advance after step\n");
            test_send_response(RESP_FAIL, "PC stuck");
            return false;
        }
        last_pc = pc.value;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_hart_reset(swd_target_t *target) {
    printf("# Testing hart reset with halt...\n");

    swd_error_t err = rp2350_reset(target, 0, true);
    if (err != SWD_OK) {
        printf("# Reset failed: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Reset failed");
        return false;
    }

    swd_result_t pc = rp2350_read_pc(target, 0);
    if (pc.error != SWD_OK) {
        printf("# Failed to read PC after reset\n");
        test_send_response(RESP_FAIL, "Failed to read PC after reset");
        return false;
    }

    bool in_rom = pc.value < 0x00010000;
    bool in_xip = pc.value >= 0x10000000 && pc.value < 0x20000000;
    if (!in_rom && !in_xip) {
        printf("# PC after reset (0x%08lx) not in ROM or XIP range\n", (unsigned long)pc.value);
        test_send_response(RESP_FAIL, "PC not at valid reset location");
        return false;
    }

    printf("# Hart reset successful, PC = 0x%08lx\n", (unsigned long)pc.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

test_case_t hart0_tests[] = {
    { "TEST 3: Halt Hart 0", test_halt_hart0, false, false },
    { "TEST 4: Read PC", test_read_pc_hart0, false, false },
    { "TEST 5: Read All GPRs", test_read_all_gprs, false, false },
    { "TEST 6: Write/Verify GPRs", test_write_verify_gprs, false, false },
    { "TEST 7: Write/Verify PC", test_write_verify_pc, false, false },
    { "TEST 8: Read ROM", test_read_rom, false, false },
    { "TEST 9: Write/Verify SRAM", test_write_verify_sram, false, false },
    { "TEST 10: Resume Hart 0", test_resume_hart0, false, false },
    { "TEST 11: Halt/Resume Stress Test", test_halt_resume_stress, false, false },
    { "TEST 12: Register Stress Test", test_register_stress, false, false },
    { "TEST 13: Memory Stress Test", test_memory_stress, false, false },
    { "TEST 14: Execute Small Program", test_execute_small_program, false, false },
    { "TEST 15: Instruction Tracing", test_instruction_tracing, false, false },
    { "TEST 16: Hart Reset", test_hart_reset, false, false },
};

const uint32_t hart0_test_count = sizeof(hart0_tests) / sizeof(hart0_tests[0]);
