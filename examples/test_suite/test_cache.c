#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include "pico/stdlib.h"
#include <stdio.h>

static bool test_cache_enable_disable(swd_target_t *target) {
    printf("# Testing cache enable/disable...\n");

    rp2350_halt(target, 0);

    uint32_t test_value = 0xCAFEBABE;
    swd_error_t err = rp2350_write_reg(target, 0, 5, test_value);
    if (err != SWD_OK) {
        printf("# Failed to write register: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Write failed");
        return false;
    }

    rp2350_enable_cache(target, true);
    printf("# Cache enabled\n");

    swd_result_t result1 = rp2350_read_reg(target, 0, 5);
    if (result1.error != SWD_OK) {
        printf("# First read failed: %s\n", swd_error_string(result1.error));
        test_send_response(RESP_FAIL, "Read 1 failed");
        return false;
    }

    swd_result_t result2 = rp2350_read_reg(target, 0, 5);
    if (result2.error != SWD_OK) {
        printf("# Second read failed: %s\n", swd_error_string(result2.error));
        test_send_response(RESP_FAIL, "Read 2 failed");
        return false;
    }

    if (result1.value != test_value || result2.value != test_value) {
        printf("# Value mismatch\n");
        test_send_response(RESP_FAIL, "Value mismatch");
        return false;
    }

    printf("# Both reads returned correct value: 0x%08lx\n", (unsigned long)test_value);

    rp2350_enable_cache(target, false);
    printf("# Cache disabled\n");

    swd_result_t result3 = rp2350_read_reg(target, 0, 5);
    if (result3.error != SWD_OK) {
        printf("# Third read failed: %s\n", swd_error_string(result3.error));
        test_send_response(RESP_FAIL, "Read 3 failed");
        return false;
    }

    if (result3.value != test_value) {
        printf("# Value mismatch after cache disable\n");
        test_send_response(RESP_FAIL, "Value mismatch");
        return false;
    }

    printf("# Read without cache successful: 0x%08lx\n", (unsigned long)result3.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_cache_invalidate_single_hart(swd_target_t *target) {
    printf("# Testing cache invalidation for single hart...\n");

    rp2350_halt(target, 0);
    rp2350_enable_cache(target, true);

    uint32_t value1 = 0x12345678;
    swd_error_t err = rp2350_write_reg(target, 0, 7, value1);
    if (err != SWD_OK) {
        printf("# Failed to write register: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Write failed");
        return false;
    }

    swd_result_t result1 = rp2350_read_reg(target, 0, 7);
    if (result1.error != SWD_OK || result1.value != value1) {
        printf("# Initial read failed\n");
        test_send_response(RESP_FAIL, "Initial read failed");
        return false;
    }
    printf("# Cache populated with value: 0x%08lx\n", (unsigned long)value1);

    rp2350_invalidate_cache(target, 0);
    printf("# Cache invalidated for hart 0\n");

    swd_result_t result2 = rp2350_read_reg(target, 0, 7);
    if (result2.error != SWD_OK) {
        printf("# Read after invalidation failed: %s\n", swd_error_string(result2.error));
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    if (result2.value != value1) {
        printf("# Value mismatch after invalidation\n");
        test_send_response(RESP_FAIL, "Value mismatch");
        return false;
    }

    printf("# Read after invalidation successful: 0x%08lx\n", (unsigned long)result2.value);
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_cache_isolation_between_harts(swd_target_t *target) {
    printf("# Testing cache isolation between harts...\n");

    rp2350_halt(target, 0);
    rp2350_halt(target, 1);
    rp2350_enable_cache(target, true);

    uint32_t value_h0 = 0xAAAAAAAA;
    uint32_t value_h1 = 0x55555555;

    swd_error_t err = rp2350_write_reg(target, 0, 10, value_h0);
    if (err != SWD_OK) {
        printf("# Failed to write hart 0 register: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Write hart 0 failed");
        return false;
    }

    err = rp2350_write_reg(target, 1, 10, value_h1);
    if (err != SWD_OK) {
        printf("# Failed to write hart 1 register: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Write hart 1 failed");
        return false;
    }

    swd_result_t result_h0 = rp2350_read_reg(target, 0, 10);
    swd_result_t result_h1 = rp2350_read_reg(target, 1, 10);

    if (result_h0.error != SWD_OK || result_h1.error != SWD_OK) {
        printf("# Failed to read registers\n");
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    if (result_h0.value != value_h0 || result_h1.value != value_h1) {
        printf("# Value mismatch: h0=0x%08lx (expected 0x%08lx), h1=0x%08lx (expected 0x%08lx)\n",
               (unsigned long)result_h0.value, (unsigned long)value_h0,
               (unsigned long)result_h1.value, (unsigned long)value_h1);
        test_send_response(RESP_FAIL, "Value mismatch");
        return false;
    }

    printf("# Caches populated: hart0=0x%08lx, hart1=0x%08lx\n",
           (unsigned long)value_h0, (unsigned long)value_h1);

    rp2350_invalidate_cache(target, 0);
    printf("# Invalidated cache for hart 0 only\n");

    result_h0 = rp2350_read_reg(target, 0, 10);
    result_h1 = rp2350_read_reg(target, 1, 10);

    if (result_h0.error != SWD_OK || result_h1.error != SWD_OK) {
        printf("# Failed to read registers after invalidation\n");
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    if (result_h0.value != value_h0 || result_h1.value != value_h1) {
        printf("# Value mismatch after partial invalidation\n");
        test_send_response(RESP_FAIL, "Value mismatch");
        return false;
    }

    printf("# Cache isolation verified: values remain correct\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool test_cache_behavior_on_resume(swd_target_t *target) {
    printf("# Testing cache invalidation on hart resume...\n");

    rp2350_halt(target, 0);
    rp2350_enable_cache(target, true);

    uint32_t value1 = 0xDEADBEEF;
    swd_error_t err = rp2350_write_reg(target, 0, 11, value1);
    if (err != SWD_OK) {
        printf("# Failed to write register: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Write failed");
        return false;
    }

    swd_result_t result1 = rp2350_read_reg(target, 0, 11);
    if (result1.error != SWD_OK || result1.value != value1) {
        printf("# Initial read failed\n");
        test_send_response(RESP_FAIL, "Initial read failed");
        return false;
    }
    printf("# Cache populated: 0x%08lx\n", (unsigned long)value1);

    err = rp2350_resume(target, 0);
    if (err != SWD_OK) {
        printf("# Failed to resume hart: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Resume failed");
        return false;
    }
    printf("# Hart resumed (cache should be invalidated)\n");

    sleep_ms(10);

    err = rp2350_halt(target, 0);
    if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED) {
        printf("# Failed to halt hart: %s\n", swd_error_string(err));
        test_send_response(RESP_FAIL, "Halt failed");
        return false;
    }

    swd_result_t result2 = rp2350_read_reg(target, 0, 11);
    if (result2.error != SWD_OK) {
        printf("# Read after resume failed: %s\n", swd_error_string(result2.error));
        test_send_response(RESP_FAIL, "Read failed");
        return false;
    }

    printf("# Read after resume/halt cycle: 0x%08lx\n", (unsigned long)result2.value);

    if (result2.value != value1) {
        printf("# Value changed: expected 0x%08lx, got 0x%08lx\n",
               (unsigned long)value1, (unsigned long)result2.value);
        test_send_response(RESP_FAIL, "Value corrupted after resume");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

test_case_t cache_tests[] = {
    {"Cache Enable/Disable", test_cache_enable_disable, false, false},
    {"Cache Invalidation (Single Hart)", test_cache_invalidate_single_hart, false, false},
    {"Cache Isolation Between Harts", test_cache_isolation_between_harts, false, false},
    {"Cache Behavior on Resume", test_cache_behavior_on_resume, false, false},
};

const uint32_t cache_test_count = sizeof(cache_tests) / sizeof(cache_tests[0]);
