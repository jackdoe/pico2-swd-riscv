#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

static bool test_memory_basic_halted(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t test_addr = 0x20000000;
    uint32_t patterns[] = {
        0x00000000, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555,
        0x12345678, 0x87654321, 0xDEADBEEF, 0xCAFEBABE
    };

    for (uint i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        uint32_t addr = test_addr + (i * 4);
        uint32_t pattern = patterns[i];

        swd_error_t err = rp2350_write_mem32(target, addr, pattern);
        if (err != SWD_OK) {
            printf("# Write failed at 0x%08lx\n", (unsigned long)addr);
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }

        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != pattern) {
            printf("# Verify failed at 0x%08lx: wrote 0x%08lx, read 0x%08lx\n",
                   (unsigned long)addr, (unsigned long)pattern, (unsigned long)result.value);
            test_send_response(RESP_FAIL, "Verify failed");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_walking_ones(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t base_addr = 0x20001000;

    for (uint i = 0; i < 32; i++) {
        uint32_t pattern = 1u << i;
        uint32_t addr = base_addr + (i * 4);

        swd_error_t err = rp2350_write_mem32(target, addr, pattern);
        if (err != SWD_OK) {
            printf("# Write failed at bit %u\n", i);
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }
    }

    for (uint i = 0; i < 32; i++) {
        uint32_t expected = 1u << i;
        uint32_t addr = base_addr + (i * 4);

        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != expected) {
            printf("# Verify failed at bit %u: expected 0x%08lx, got 0x%08lx\n",
                   i, (unsigned long)expected, (unsigned long)result.value);
            test_send_response(RESP_FAIL, "Verify failed");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_walking_zeros(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t base_addr = 0x20001100;

    for (uint i = 0; i < 32; i++) {
        uint32_t pattern = ~(1u << i);
        uint32_t addr = base_addr + (i * 4);

        swd_error_t err = rp2350_write_mem32(target, addr, pattern);
        if (err != SWD_OK) {
            printf("# Write failed at bit %u\n", i);
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }

        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != pattern) {
            printf("# Verify failed at bit %u\n", i);
            test_send_response(RESP_FAIL, "Verify failed");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_checkerboard(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t base_addr = 0x20001200;
    uint32_t word_count = 256;

    for (uint i = 0; i < word_count; i++) {
        uint32_t addr = base_addr + (i * 4);
        swd_error_t err = rp2350_write_mem32(target, addr, 0xAAAAAAAA);
        if (err != SWD_OK) {
            printf("# Write failed at word %u\n", i);
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }
    }

    for (uint i = 0; i < word_count; i++) {
        uint32_t addr = base_addr + (i * 4);
        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != 0xAAAAAAAA) {
            printf("# Verify failed at word %u\n", i);
            test_send_response(RESP_FAIL, "Verify failed");
            return false;
        }
    }

    for (uint i = 0; i < word_count; i++) {
        uint32_t addr = base_addr + (i * 4);
        swd_error_t err = rp2350_write_mem32(target, addr, 0x55555555);
        if (err != SWD_OK) {
            printf("# Write failed at word %u\n", i);
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }
    }

    for (uint i = 0; i < word_count; i++) {
        uint32_t addr = base_addr + (i * 4);
        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != 0x55555555) {
            printf("# Verify failed at word %u\n", i);
            test_send_response(RESP_FAIL, "Verify failed");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_address_pattern(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t base_addr = 0x20002000;
    uint32_t word_count = 512;

    for (uint i = 0; i < word_count; i++) {
        uint32_t addr = base_addr + (i * 4);
        swd_error_t err = rp2350_write_mem32(target, addr, addr);
        if (err != SWD_OK) {
            printf("# Write failed at word %u\n", i);
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }
    }

    for (uint i = 0; i < word_count; i++) {
        uint32_t addr = base_addr + (i * 4);
        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != addr) {
            printf("# Verify failed at word %u: expected 0x%08lx, got 0x%08lx\n",
                   i, (unsigned long)addr, (unsigned long)result.value);
            test_send_response(RESP_FAIL, "Verify failed");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static uint32_t large_block_write_buf[1024];
static uint32_t large_block_read_buf[1024];

static bool test_memory_large_block(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t base_addr = 0x20003000;

    for (uint32_t i = 0; i < 1024; i++)
        large_block_write_buf[i] = 0xA5000000 | i;

    swd_error_t err = rp2350_write_mem_block(target, base_addr, large_block_write_buf, 1024);
    if (err != SWD_OK) {
        printf("# Block write failed: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Block write failed");
        return false;
    }

    err = rp2350_read_mem_block(target, base_addr, large_block_read_buf, 1024);
    if (err != SWD_OK) {
        printf("# Block read failed: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Block read failed");
        return false;
    }

    if (memcmp(large_block_write_buf, large_block_read_buf, 1024 * sizeof(uint32_t)) != 0) {
        for (uint32_t i = 0; i < 1024; i++) {
            if (large_block_read_buf[i] != large_block_write_buf[i]) {
                printf("# Mismatch at word %lu: expected 0x%08lx, got 0x%08lx\n",
                       (unsigned long)i,
                       (unsigned long)large_block_write_buf[i],
                       (unsigned long)large_block_read_buf[i]);
                test_send_response(RESP_FAIL, "Block verify failed");
                return false;
            }
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_while_running(swd_target_t *target) {
    uint32_t program_addr = 0x20004000;
    uint32_t test_addr = 0x20004100;

    rp2350_halt(target, 0);

    uint32_t program[] = {
        0x0000006f,
    };

    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
        if (err != SWD_OK) {
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    rp2350_write_pc(target, 0, program_addr);
    rp2350_resume(target, 0);

    uint32_t patterns[] = {0xDEADBEEF, 0xCAFEBABE, 0xFEEDFACE, 0xBAADF00D};

    for (uint i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        uint32_t addr = test_addr + (i * 4);
        uint32_t pattern = patterns[i];

        swd_error_t err = rp2350_write_mem32(target, addr, pattern);
        if (err != SWD_OK) {
            printf("# Write failed while running at 0x%08lx\n", (unsigned long)addr);
            test_send_response(RESP_FAIL, "Write failed while running");
            rp2350_halt(target, 0);
            return false;
        }

        swd_result_t result = rp2350_read_mem32(target, addr);
        if (result.error != SWD_OK || result.value != pattern) {
            printf("# Verify failed while running at 0x%08lx\n", (unsigned long)addr);
            test_send_response(RESP_FAIL, "Verify failed while running");
            rp2350_halt(target, 0);
            return false;
        }
    }

    rp2350_halt(target, 0);

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_ram_fill_cpu(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t program_base = 0x20078000;
    uint32_t fill_start = 0x20000000;
    uint32_t fill_end = 0x20040000;
    uint32_t fill_pattern = 0xA5A5A5A5;

    uint32_t fill_program[] = {
        0x200002B7,
        0x20040337,
        0xA5A5A3B7,
        0x5A538393,
        0x0072A023,
        0x00428293,
        0xFE629CE3,
        0x0000006F,
    };

    for (uint i = 0; i < sizeof(fill_program)/sizeof(fill_program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_base + (i * 4), fill_program[i]);
        if (err != SWD_OK) {
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    rp2350_write_pc(target, 0, program_base);
    rp2350_resume(target, 0);
    sleep_ms(100);
    rp2350_halt(target, 0);

    uint32_t sample_addrs[] = {
        fill_start,
        fill_start + 0x10000,
        fill_start + 0x20000,
        fill_start + 0x30000,
        fill_end - 4
    };

    for (uint i = 0; i < sizeof(sample_addrs)/sizeof(sample_addrs[0]); i++) {
        swd_result_t result = rp2350_read_mem32(target, sample_addrs[i]);
        if (result.error != SWD_OK || result.value != fill_pattern) {
            printf("# Verify failed at 0x%08lx: expected 0x%08lx, got 0x%08lx\n",
                   (unsigned long)sample_addrs[i],
                   (unsigned long)fill_pattern,
                   (unsigned long)result.value);
            test_send_response(RESP_FAIL, "Verify failed");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_memory_checksum(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t program_base = 0x20078000;
    uint32_t checksum_base = program_base + 0x100;
    uint32_t fill_start = 0x20000000;
    uint32_t fill_end = 0x20040000;
    uint32_t fill_pattern = 0xA5A5A5A5;

    uint32_t fill_program[] = {
        0x200002B7,
        0x20040337,
        0xA5A5A3B7,
        0x5A538393,
        0x0072A023,
        0x00428293,
        0xFE629CE3,
        0x0000006F,
    };

    for (uint i = 0; i < sizeof(fill_program)/sizeof(fill_program[0]); i++)
        rp2350_write_mem32(target, program_base + (i * 4), fill_program[i]);

    rp2350_write_pc(target, 0, program_base);
    rp2350_resume(target, 0);
    sleep_ms(100);
    rp2350_halt(target, 0);

    uint32_t checksum_program[] = {
        0x200002B7,
        0x20040337,
        0x00000513,
        0x0002A383,
        0x00754533,
        0x00428293,
        0xFE629AE3,
        0x0000006F,
    };

    for (uint i = 0; i < sizeof(checksum_program)/sizeof(checksum_program[0]); i++)
        rp2350_write_mem32(target, checksum_base + (i * 4), checksum_program[i]);

    rp2350_write_reg(target, 0, 10, 0);

    rp2350_write_pc(target, 0, checksum_base);
    rp2350_resume(target, 0);
    sleep_ms(100);
    rp2350_halt(target, 0);

    swd_result_t checksum_result = rp2350_read_reg(target, 0, 10);
    if (checksum_result.error != SWD_OK) {
        test_send_response(RESP_FAIL, "Failed to read checksum");
        return false;
    }

    uint32_t word_count = (fill_end - fill_start) / 4;
    uint32_t expected_checksum = (word_count & 1) ? fill_pattern : 0;

    if (checksum_result.value != expected_checksum) {
        printf("# Checksum mismatch: got 0x%08lx, expected 0x%08lx\n",
               (unsigned long)checksum_result.value,
               (unsigned long)expected_checksum);
        test_send_response(RESP_FAIL, "Checksum mismatch");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

test_case_t memory_tests[] = {
    { "MEM 1: Basic Memory R/W (Halted)", test_memory_basic_halted, false, false },
    { "MEM 2: Walking 1s Pattern", test_memory_walking_ones, false, false },
    { "MEM 3: Walking 0s Pattern", test_memory_walking_zeros, false, false },
    { "MEM 4: Checkerboard Pattern", test_memory_checkerboard, false, false },
    { "MEM 5: Address-Based Pattern", test_memory_address_pattern, false, false },
    { "MEM 6: Large Block (4KB)", test_memory_large_block, false, false },
    { "MEM 7: Memory Access While Running", test_memory_while_running, false, false },
    { "MEM 8: RAM Fill with CPU (256KB)", test_memory_ram_fill_cpu, false, false },
    { "MEM 9: Checksum Verification (256KB)", test_memory_checksum, false, false },
};

const uint32_t memory_test_count = sizeof(memory_tests) / sizeof(memory_tests[0]);
