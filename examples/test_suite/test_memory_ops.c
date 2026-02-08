#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include "pico2-swd-riscv/dap.h"
#include <stdio.h>

#define TEST_ADDR 0x20077000

static bool test_mem8_read_write(swd_target_t *target) {
    uint32_t base_addr = TEST_ADDR;

    uint8_t test_bytes[] = {0x12, 0x34, 0x56, 0x78};
    for (int i = 0; i < 4; i++) {
        swd_error_t err = rp2350_write_mem8(target, base_addr + i, test_bytes[i]);
        if (err != SWD_OK) {
            printf("# Failed to write byte %d: %s\n", i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }
    }

    swd_result_t word_result = rp2350_read_mem32(target, base_addr);
    if (word_result.error != SWD_OK) {
        test_send_response(RESP_FAIL, "Read word failed");
        return false;
    }

    if (word_result.value != 0x78563412) {
        printf("# Word mismatch: got 0x%08lx, expected 0x78563412\n",
               (unsigned long)word_result.value);
        test_send_response(RESP_FAIL, "Byte order incorrect");
        return false;
    }

    for (int i = 0; i < 4; i++) {
        swd_result_t byte_result = rp2350_read_mem8(target, base_addr + i);
        if (byte_result.error != SWD_OK) {
            printf("# Failed to read byte %d: %s\n", i, swd_error_string(byte_result.error));
            test_send_response(RESP_FAIL, "Read failed");
            return false;
        }

        if ((uint8_t)byte_result.value != test_bytes[i]) {
            printf("# Byte %d mismatch: got 0x%02x, expected 0x%02x\n",
                   i, (uint8_t)byte_result.value, test_bytes[i]);
            test_send_response(RESP_FAIL, "Byte mismatch");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_mem16_read_write(swd_target_t *target) {
    uint32_t base_addr = TEST_ADDR;

    uint16_t test_halfwords[] = {0xABCD, 0x1234};
    for (int i = 0; i < 2; i++) {
        swd_error_t err = rp2350_write_mem16(target, base_addr + (i * 2), test_halfwords[i]);
        if (err != SWD_OK) {
            printf("# Failed to write halfword %d: %s\n", i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }
    }

    swd_result_t word_result = rp2350_read_mem32(target, base_addr);
    if (word_result.error != SWD_OK) {
        test_send_response(RESP_FAIL, "Read word failed");
        return false;
    }

    if (word_result.value != 0x1234ABCD) {
        printf("# Word mismatch: got 0x%08lx, expected 0x1234ABCD\n",
               (unsigned long)word_result.value);
        test_send_response(RESP_FAIL, "Halfword order incorrect");
        return false;
    }

    for (int i = 0; i < 2; i++) {
        swd_result_t half_result = rp2350_read_mem16(target, base_addr + (i * 2));
        if (half_result.error != SWD_OK) {
            printf("# Failed to read halfword %d: %s\n", i, swd_error_string(half_result.error));
            test_send_response(RESP_FAIL, "Read failed");
            return false;
        }

        if ((uint16_t)half_result.value != test_halfwords[i]) {
            printf("# Halfword %d mismatch: got 0x%04x, expected 0x%04x\n",
                   i, (uint16_t)half_result.value, test_halfwords[i]);
            test_send_response(RESP_FAIL, "Halfword mismatch");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_rp2350_read_mem_block(swd_target_t *target) {
    uint32_t base_addr = TEST_ADDR;
    uint32_t block_size = 16;

    uint32_t write_buffer[16];
    for (uint32_t i = 0; i < block_size; i++) {
        write_buffer[i] = 0xAA550000 | i;
        swd_error_t err = rp2350_write_mem32(target, base_addr + (i * 4), write_buffer[i]);
        if (err != SWD_OK) {
            printf("# Failed to write word %lu: %s\n", (unsigned long)i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Write failed");
            return false;
        }
    }

    uint32_t read_buffer[16];
    swd_error_t err = rp2350_read_mem_block(target, base_addr, read_buffer, block_size);
    if (err != SWD_OK) {
        printf("# Block read failed: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Block read failed");
        return false;
    }

    for (uint32_t i = 0; i < block_size; i++) {
        if (read_buffer[i] != write_buffer[i]) {
            printf("# Word %lu mismatch: got 0x%08lx, expected 0x%08lx\n",
                   (unsigned long)i, (unsigned long)read_buffer[i], (unsigned long)write_buffer[i]);
            test_send_response(RESP_FAIL, "Data mismatch");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_rp2350_write_mem_block(swd_target_t *target) {
    uint32_t base_addr = TEST_ADDR;
    uint32_t block_size = 16;

    uint32_t write_buffer[16];
    for (uint32_t i = 0; i < block_size; i++)
        write_buffer[i] = 0x55AA0000 | (i << 8) | i;

    swd_error_t err = rp2350_write_mem_block(target, base_addr, write_buffer, block_size);
    if (err != SWD_OK) {
        printf("# Block write failed: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Block write failed");
        return false;
    }

    for (uint32_t i = 0; i < block_size; i++) {
        swd_result_t result = rp2350_read_mem32(target, base_addr + (i * 4));
        if (result.error != SWD_OK) {
            printf("# Read word %lu failed: %s\n", (unsigned long)i, swd_error_string(result.error));
            test_send_response(RESP_FAIL, "Read failed");
            return false;
        }

        if (result.value != write_buffer[i]) {
            printf("# Word %lu mismatch: got 0x%08lx, expected 0x%08lx\n",
                   (unsigned long)i, (unsigned long)result.value, (unsigned long)write_buffer[i]);
            test_send_response(RESP_FAIL, "Data mismatch");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

#define DM_DATA0       (0x04 * 4)
#define DM_DATA1       (0x05 * 4)
#define DM_PROGBUF0    (0x20 * 4)
#define DM_PROGBUF1    (0x21 * 4)

static bool test_dap_dm_register_access(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t test_value = 0xDEADBEEF;
    swd_error_t err = dap_write_mem32(target, DM_DATA0, test_value);
    if (err != SWD_OK) {
        test_send_response(RESP_FAIL, "Write DATA0 failed");
        return false;
    }

    swd_result_t result = dap_read_mem32(target, DM_DATA0);
    if (result.error != SWD_OK) {
        test_send_response(RESP_FAIL, "Read DATA0 failed");
        return false;
    }

    if (result.value != test_value) {
        printf("# DATA0 mismatch: got 0x%08lx, expected 0x%08lx\n",
               (unsigned long)result.value, (unsigned long)test_value);
        test_send_response(RESP_FAIL, "DATA0 mismatch");
        return false;
    }

    uint32_t progbuf_instr = 0x00100073;
    err = dap_write_mem32(target, DM_PROGBUF0, progbuf_instr);
    if (err != SWD_OK) {
        test_send_response(RESP_FAIL, "Write PROGBUF0 failed");
        return false;
    }

    result = dap_read_mem32(target, DM_PROGBUF0);
    if (result.error != SWD_OK) {
        test_send_response(RESP_FAIL, "Read PROGBUF0 failed");
        return false;
    }

    if (result.value != progbuf_instr) {
        printf("# PROGBUF0 mismatch: got 0x%08lx, expected 0x%08lx\n",
               (unsigned long)result.value, (unsigned long)progbuf_instr);
        test_send_response(RESP_FAIL, "PROGBUF0 mismatch");
        return false;
    }

    uint32_t values[2] = {0xAAAAAAAA, 0x55555555};

    for (int i = 0; i < 2; i++) {
        uint32_t addr = DM_PROGBUF0 + (i * 4);
        err = dap_write_mem32(target, addr, values[i]);
        if (err != SWD_OK) {
            printf("# Failed to write PROGBUF%d: %s\n", i, swd_error_string(err));
            test_send_response(RESP_FAIL, "Write PROGBUF failed");
            return false;
        }
    }

    for (int i = 0; i < 2; i++) {
        uint32_t addr = DM_PROGBUF0 + (i * 4);
        result = dap_read_mem32(target, addr);
        if (result.error != SWD_OK) {
            test_send_response(RESP_FAIL, "Read PROGBUF failed");
            return false;
        }
        if (result.value != values[i]) {
            printf("# PROGBUF%d mismatch: got 0x%08lx, expected 0x%08lx\n",
                   i, (unsigned long)result.value, (unsigned long)values[i]);
            test_send_response(RESP_FAIL, "PROGBUF value mismatch");
            return false;
        }
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

test_case_t memory_ops_tests[] = {
    {"8-bit Memory Read/Write", test_mem8_read_write, false, false},
    {"16-bit Memory Read/Write", test_mem16_read_write, false, false},
    {"RP2350 Block Memory Read", test_rp2350_read_mem_block, false, false},
    {"RP2350 Block Memory Write", test_rp2350_write_mem_block, false, false},
    {"DAP DM Register Access", test_dap_dm_register_access, false, false},
};

const uint32_t memory_ops_test_count = sizeof(memory_ops_tests) / sizeof(memory_ops_tests[0]);
