#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico2-swd-riscv/swd.h"
#include "pico2-swd-riscv/rp2350.h"
#include "pico2-swd-riscv/version.h"
#include "test_framework.h"

#define SWCLK_PIN 2
#define SWDIO_PIN 3

static swd_target_t *g_target = NULL;

static void run_all_tests(void) {
    printf("\n");
    printf("====================================\n");
    printf("  Running Full Test Suite\n");
    printf("====================================\n");

    test_stats_t total_stats = {0};

    printf("\n=== BASIC CONNECTION TESTS ===\n");
    test_stats_t basic_stats = test_run_suite(basic_tests, basic_test_count);
    total_stats.total += basic_stats.total;
    total_stats.passed += basic_stats.passed;
    total_stats.failed += basic_stats.failed;

    printf("\n=== HART 0 TESTS ===\n");
    test_stats_t hart0_stats = test_run_suite(hart0_tests, hart0_test_count);
    total_stats.total += hart0_stats.total;
    total_stats.passed += hart0_stats.passed;
    total_stats.failed += hart0_stats.failed;

    printf("\n=== HART 1 TESTS ===\n");
    test_stats_t hart1_stats = test_run_suite(hart1_tests, hart1_test_count);
    total_stats.total += hart1_stats.total;
    total_stats.passed += hart1_stats.passed;
    total_stats.failed += hart1_stats.failed;

    printf("\n=== DUAL-HART TESTS ===\n");
    test_stats_t dual_stats = test_run_suite(dual_hart_tests, dual_hart_test_count);
    total_stats.total += dual_stats.total;
    total_stats.passed += dual_stats.passed;
    total_stats.failed += dual_stats.failed;

    printf("\n=== MEMORY TESTS ===\n");
    test_stats_t mem_stats = test_run_suite(memory_tests, memory_test_count);
    total_stats.total += mem_stats.total;
    total_stats.passed += mem_stats.passed;
    total_stats.failed += mem_stats.failed;

    printf("\n=== TRACE TESTS ===\n");
    test_stats_t trace_stats = test_run_suite(trace_tests, trace_test_count);
    total_stats.total += trace_stats.total;
    total_stats.passed += trace_stats.passed;
    total_stats.failed += trace_stats.failed;

    printf("\n=== API COVERAGE TESTS ===\n");
    test_stats_t api_stats = test_run_suite(api_coverage_tests, api_coverage_test_count);
    total_stats.total += api_stats.total;
    total_stats.passed += api_stats.passed;
    total_stats.failed += api_stats.failed;

    printf("\n=== MEMORY OPERATIONS TESTS ===\n");
    test_stats_t mem_ops_stats = test_run_suite(memory_ops_tests, memory_ops_test_count);
    total_stats.total += mem_ops_stats.total;
    total_stats.passed += mem_ops_stats.passed;
    total_stats.failed += mem_ops_stats.failed;

    printf("\n=== CACHE TESTS ===\n");
    test_stats_t cache_stats = test_run_suite(cache_tests, cache_test_count);
    total_stats.total += cache_stats.total;
    total_stats.passed += cache_stats.passed;
    total_stats.failed += cache_stats.failed;

    printf("\n=== CODE EXECUTION TESTS ===\n");
    test_stats_t code_exec_stats = test_run_suite(code_exec_tests, code_exec_test_count);
    total_stats.total += code_exec_stats.total;
    total_stats.passed += code_exec_stats.passed;
    total_stats.failed += code_exec_stats.failed;

    printf("\n=== ERROR PATH TESTS ===\n");
    test_stats_t error_path_stats = test_run_suite(error_path_tests, error_path_test_count);
    total_stats.total += error_path_stats.total;
    total_stats.passed += error_path_stats.passed;
    total_stats.failed += error_path_stats.failed;

    printf("\n");
    printf("====================================\n");
    printf("  Overall Test Results\n");
    printf("====================================\n");
    test_print_stats(&total_stats);

    test_final_cleanup();
}

static void handle_command(char *cmd) {
    char *newline = strchr(cmd, '\n');
    if (newline) *newline = '\0';
    char *cr = strchr(cmd, '\r');
    if (cr) *cr = '\0';

    if (strlen(cmd) == 0) return;

    printf("# Command: %s\n", cmd);

    if (strcmp(cmd, CMD_READY) == 0) {
        test_send_response(RESP_PASS, "Test suite ready");
    }
    else if (strcmp(cmd, CMD_TEST_ALL) == 0) {
        run_all_tests();
    }
    else if (strcmp(cmd, CMD_DISCONNECT) == 0) {
        printf("# Disconnecting...\n");
        test_final_cleanup();
        test_send_response(RESP_PASS, NULL);
    }
    else if (strcmp(cmd, "HELP") == 0) {
        printf("# Available commands:\n");
        printf("#   READY       - Check if test suite is ready\n");
        printf("#   TEST_ALL    - Run all tests\n");
        printf("#   DISCONNECT  - Disconnect from target\n");
        printf("#   HELP        - Show this help message\n");
    }
    else {
        test_send_response(RESP_FAIL, "Unknown command (try HELP)");
    }
}

int main() {
    stdio_init_all();

    sleep_ms(2000);

    printf("\n\n");
    printf("====================================\n");
    printf("  pico2-swd-riscv Test Suite\n");
    printf("====================================\n");
    printf("Version: %s\n", PICO2_SWD_RISCV_VERSION_STRING);
    printf("Pins: SWCLK=%d, SWDIO=%d\n", SWCLK_PIN, SWDIO_PIN);
    printf("\n");

    swd_config_t config = swd_config_default();
    config.pin_swclk = SWCLK_PIN;
    config.pin_swdio = SWDIO_PIN;
    config.freq_khz = 1000;
    config.enable_caching = true;

    g_target = swd_target_create(&config);
    if (!g_target) {
        printf("FATAL: Failed to create SWD target\n");
        return 1;
    }

    test_framework_init(g_target);

    printf("Test suite ready!\n");
    printf("Send 'TEST_ALL' to run full test suite, or 'HELP' for commands.\n");
    printf("\n");

    char cmd_buf[128];
    int cmd_pos = 0;

    while (true) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (cmd_pos > 0) {
                cmd_buf[cmd_pos] = '\0';
                handle_command(cmd_buf);
                cmd_pos = 0;
            }
        } else if (cmd_pos < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_pos++] = (char)c;
        }
    }

    swd_target_destroy(g_target);
    return 0;
}
