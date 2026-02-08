#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include "pico/stdlib.h"

#include <stdio.h>

#define CODE_BASE 0x20077000
#define DATA_BASE 0x20078000

static bool test_execute_addition_code(swd_target_t *target) {
    printf("# Testing code execution (addition)...\n");

    const uint32_t program[] = {
        0x007302B3,
        0x0000006F,
    };

    uint32_t a = 42;
    uint32_t b = 58;

    swd_error_t err = rp2350_write_reg(target, 0, 6, a);
    if (err != SWD_OK) {
        printf("# Failed to write x6: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Setup failed");
        return false;
    }

    err = rp2350_write_reg(target, 0, 7, b);
    if (err != SWD_OK) {
        printf("# Failed to write x7: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Setup failed");
        return false;
    }

    err = rp2350_execute_code(target, 0, CODE_BASE, program, 2);
    if (err != SWD_OK) {
        printf("# Failed to execute code: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Execution failed");
        return false;
    }

    printf("# Code started, waiting for execution...\n");
    sleep_ms(10);

    err = rp2350_halt(target, 0);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt hart: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Halt failed");
        return false;
    }

    swd_result_t result = rp2350_read_reg(target, 0, 5);
    if (result.error != SWD_OK) {
        printf("# Failed to read result: %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    uint32_t expected = a + b;
    if (result.value != expected) {
        printf("# Incorrect result: got %lu, expected %lu\n",
               (unsigned long)result.value, (unsigned long)expected);
        test_send_response(RESP_FAIL, "Incorrect result");
        return false;
    }

    printf("# Code executed successfully: %lu + %lu = %lu\n",
           (unsigned long)a, (unsigned long)b, (unsigned long)result.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_execute_memory_store_code(swd_target_t *target) {
    printf("# Testing code execution (memory store)...\n");

    uint32_t store_addr = DATA_BASE;
    uint32_t store_value = 0xCAFEBABE;

    const uint32_t program[] = {
        0x200785B7,
        0x00058593,
        0x00A5A023,
        0x0000006F,
    };

    swd_error_t err = rp2350_write_reg(target, 0, 10, store_value);
    if (err != SWD_OK) {
        printf("# Failed to write x10: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Setup failed");
        return false;
    }

    err = rp2350_execute_code(target, 0, CODE_BASE, program, 4);
    if (err != SWD_OK) {
        printf("# Failed to execute code: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Execution failed");
        return false;
    }

    sleep_ms(10);

    err = rp2350_halt(target, 0);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt hart: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Halt failed");
        return false;
    }

    swd_result_t mem_result = rp2350_read_mem32(target, store_addr);
    if (mem_result.error != SWD_OK) {
        printf("# Failed to read memory: %s\n", swd_error_string(mem_result.error));
        test_send_response(RESP_FAIL, "Memory read failed");
        return false;
    }

    if (mem_result.value != store_value) {
        printf("# Memory mismatch: got 0x%08lx, expected 0x%08lx\n",
               (unsigned long)mem_result.value, (unsigned long)store_value);
        test_send_response(RESP_FAIL, "Memory mismatch");
        return false;
    }

    printf("# Memory store successful: stored 0x%08lx at 0x%08lx\n",
           (unsigned long)store_value, (unsigned long)store_addr);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_execute_code_on_hart1(swd_target_t *target) {
    printf("# Testing code execution on hart 1...\n");

    const uint32_t program[] = {
        0x00169693,
        0x00068633,
        0x0000006F,
    };

    uint32_t input = 25;
    uint32_t expected = input * 2;

    swd_error_t err = rp2350_write_reg(target, 1, 13, input);
    if (err != SWD_OK) {
        printf("# Failed to write x13 on hart 1: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Setup failed");
        return false;
    }

    err = rp2350_execute_code(target, 1, CODE_BASE, program, 3);
    if (err != SWD_OK) {
        printf("# Failed to execute code on hart 1: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Execution failed");
        return false;
    }

    sleep_ms(10);

    err = rp2350_halt(target, 1);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt hart 1: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Halt failed");
        return false;
    }

    swd_result_t result = rp2350_read_reg(target, 1, 12);
    if (result.error != SWD_OK) {
        printf("# Failed to read result from hart 1: %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    if (result.value != expected) {
        printf("# Incorrect result: got %lu, expected %lu\n",
               (unsigned long)result.value, (unsigned long)expected);
        test_send_response(RESP_FAIL, "Incorrect result");
        return false;
    }

    printf("# Hart 1 code executed successfully: %lu * 2 = %lu\n",
           (unsigned long)input, (unsigned long)result.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_execute_progbuf(swd_target_t *target) {
    printf("# Testing program buffer execution...\n");

    rp2350_halt(target, 0);

    uint32_t input = 0x12345678;
    swd_error_t err = rp2350_write_reg(target, 0, 14, input);
    if (err != SWD_OK) {
        printf("# Failed to write x14: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Setup failed");
        return false;
    }

    const uint32_t progbuf[] = {
        0xFFF74793,
        0x00100073,
    };

    err = rp2350_execute_progbuf(target, 0, progbuf, 2);
    if (err != SWD_OK) {
        printf("# Failed to execute progbuf: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Progbuf execution failed");
        return false;
    }

    swd_result_t result = rp2350_read_reg(target, 0, 15);
    if (result.error != SWD_OK) {
        printf("# Failed to read result: %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    uint32_t expected = ~input;
    if (result.value != expected) {
        printf("# Incorrect result: got 0x%08lx, expected 0x%08lx\n",
               (unsigned long)result.value, (unsigned long)expected);
        test_send_response(RESP_FAIL, "Incorrect result");
        return false;
    }

    printf("# Progbuf executed successfully: NOT(0x%08lx) = 0x%08lx\n",
           (unsigned long)input, (unsigned long)result.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_execute_code_with_loop(swd_target_t *target) {
    printf("# Testing code execution (loop)...\n");

    const uint32_t program[] = {
        0x00000813,
        0x00A00893,
        0x00180813,
        0xFF181EE3,
        0x0000006F,
    };

    swd_error_t err = rp2350_execute_code(target, 0, CODE_BASE, program, 5);
    if (err != SWD_OK) {
        printf("# Failed to execute code: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Execution failed");
        return false;
    }

    sleep_ms(10);

    err = rp2350_halt(target, 0);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt hart: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Halt failed");
        return false;
    }

    swd_result_t result = rp2350_read_reg(target, 0, 16);
    if (result.error != SWD_OK) {
        printf("# Failed to read result: %s\n", swd_error_string(result.error));
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    uint32_t expected = 10;
    if (result.value != expected) {
        printf("# Incorrect result: got %lu, expected %lu\n",
               (unsigned long)result.value, (unsigned long)expected);
        test_send_response(RESP_FAIL, "Incorrect result");
        return false;
    }

    printf("# Loop executed successfully: counted to %lu\n", (unsigned long)result.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

test_case_t code_exec_tests[] = {
    {"Execute Addition Code", test_execute_addition_code, false, false},
    {"Execute Memory Store Code", test_execute_memory_store_code, false, false},
    {"Execute Code on Hart 1", test_execute_code_on_hart1, false, false},
    {"Execute Program Buffer", test_execute_progbuf, false, false},
    {"Execute Code with Loop", test_execute_code_with_loop, false, false},
};

const uint32_t code_exec_test_count = sizeof(code_exec_tests) / sizeof(code_exec_tests[0]);
