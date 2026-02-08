#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include <stdio.h>
#include "pico/stdlib.h"

static bool test_read_reg_while_running(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t loop_addr = 0x20004000;
    rp2350_write_mem32(target, loop_addr, 0x0000006f);
    rp2350_write_pc(target, 0, loop_addr);
    rp2350_resume(target, 0);

    swd_result_t result = rp2350_read_reg(target, 0, 1);
    rp2350_halt(target, 0);

    if (result.error != SWD_ERROR_NOT_HALTED) {
        printf("# Expected SWD_ERROR_NOT_HALTED, got %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Wrong error for read_reg while running");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_write_reg_while_running(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t loop_addr = 0x20004000;
    rp2350_write_mem32(target, loop_addr, 0x0000006f);
    rp2350_write_pc(target, 0, loop_addr);
    rp2350_resume(target, 0);

    swd_error_t err = rp2350_write_reg(target, 0, 1, 0x12345678);
    rp2350_halt(target, 0);

    if (err != SWD_ERROR_NOT_HALTED) {
        printf("# Expected SWD_ERROR_NOT_HALTED, got %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Wrong error for write_reg while running");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_read_pc_while_running(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t loop_addr = 0x20004000;
    rp2350_write_mem32(target, loop_addr, 0x0000006f);
    rp2350_write_pc(target, 0, loop_addr);
    rp2350_resume(target, 0);

    swd_result_t result = rp2350_read_pc(target, 0);
    rp2350_halt(target, 0);

    if (result.error != SWD_ERROR_NOT_HALTED) {
        printf("# Expected SWD_ERROR_NOT_HALTED, got %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Wrong error for read_pc while running");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_invalid_hart_id(swd_target_t *target) {
    swd_error_t err = rp2350_halt(target, 2);
    if (err == SWD_OK) {
        printf("# halt with hart_id=2 should have failed\n");
        test_send_response(RESP_FAIL, "Invalid hart accepted");
        return false;
    }

    swd_result_t result = rp2350_read_reg(target, 5, 0);
    if (result.error == SWD_OK) {
        printf("# read_reg with hart_id=5 should have failed\n");
        test_send_response(RESP_FAIL, "Invalid hart accepted");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_invalid_register_number(swd_target_t *target) {
    rp2350_halt(target, 0);

    swd_result_t result = rp2350_read_reg(target, 0, 32);
    if (result.error != SWD_ERROR_INVALID_PARAM) {
        printf("# Expected SWD_ERROR_INVALID_PARAM for reg 32, got %s\n",
               swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Wrong error for reg 32");
        return false;
    }

    result = rp2350_read_reg(target, 0, 255);
    if (result.error != SWD_ERROR_INVALID_PARAM) {
        printf("# Expected SWD_ERROR_INVALID_PARAM for reg 255, got %s\n",
               swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Wrong error for reg 255");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_unaligned_memory_access(swd_target_t *target) {
    swd_result_t result = rp2350_read_mem32(target, 0x20000001);
    if (result.error != SWD_ERROR_ALIGNMENT) {
        printf("# Expected SWD_ERROR_ALIGNMENT for 32-bit at 0x20000001, got %s\n",
               swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Wrong error for unaligned mem32");
        return false;
    }

    result = rp2350_read_mem16(target, 0x20000001);
    if (result.error != SWD_ERROR_ALIGNMENT) {
        printf("# Expected SWD_ERROR_ALIGNMENT for 16-bit at 0x20000001, got %s\n",
               swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Wrong error for unaligned mem16");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_step_while_running(swd_target_t *target) {
    rp2350_halt(target, 0);

    uint32_t loop_addr = 0x20004000;
    rp2350_write_mem32(target, loop_addr, 0x0000006f);
    rp2350_write_pc(target, 0, loop_addr);
    rp2350_resume(target, 0);

    swd_error_t err = rp2350_step(target, 0);
    rp2350_halt(target, 0);

    if (err != SWD_ERROR_NOT_HALTED) {
        printf("# Expected SWD_ERROR_NOT_HALTED, got %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Wrong error for step while running");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_double_halt(swd_target_t *target) {
    rp2350_halt(target, 0);

    swd_error_t err = rp2350_halt(target, 0);
    if (err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Expected SWD_ERROR_ALREADY_HALTED, got %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Wrong error for double halt");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_x0_always_zero(swd_target_t *target) {
    rp2350_halt(target, 0);

    rp2350_write_reg(target, 0, 0, 0xFFFFFFFF);

    swd_result_t result = rp2350_read_reg(target, 0, 0);
    if (result.error != SWD_OK) {
        printf("# Failed to read x0: %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Read x0 failed");
        return false;
    }

    if (result.value != 0) {
        printf("# x0 should be 0, got 0x%08lx\n", (unsigned long)result.value);
        test_send_response(RESP_FAIL, "x0 not zero");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

test_case_t error_path_tests[] = {
    {"ERR 1: Read Reg While Running", test_read_reg_while_running, false, false},
    {"ERR 2: Write Reg While Running", test_write_reg_while_running, false, false},
    {"ERR 3: Read PC While Running", test_read_pc_while_running, false, false},
    {"ERR 4: Invalid Hart ID", test_invalid_hart_id, false, false},
    {"ERR 5: Invalid Register Number", test_invalid_register_number, false, false},
    {"ERR 6: Unaligned Memory Access", test_unaligned_memory_access, false, false},
    {"ERR 7: Step While Running", test_step_while_running, false, false},
    {"ERR 8: Double Halt", test_double_halt, false, false},
    {"ERR 9: x0 Always Zero", test_x0_always_zero, false, false},
};

const uint32_t error_path_test_count = sizeof(error_path_tests) / sizeof(error_path_tests[0]);
