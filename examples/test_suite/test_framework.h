#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>
#include "pico2-swd-riscv/swd.h"

#define CMD_READY "READY"
#define CMD_CONNECT "CONNECT"
#define CMD_INIT "INIT"
#define CMD_HALT "HALT"
#define CMD_RESUME "RESUME"
#define CMD_READ_PC "READ_PC"
#define CMD_WRITE_PC "WRITE_PC"
#define CMD_READ_REG "READ_REG"
#define CMD_WRITE_REG "WRITE_REG"
#define CMD_READ_MEM "READ_MEM"
#define CMD_WRITE_MEM "WRITE_MEM"
#define CMD_TRACE "TRACE"
#define CMD_RESET "RESET"
#define CMD_SET_BP "SET_BP"
#define CMD_CLEAR_BP "CLEAR_BP"
#define CMD_CLEAR_ALL_BP "CLEAR_ALL_BP"
#define CMD_TEST_ALL "TEST_ALL"
#define CMD_DISCONNECT "DISCONNECT"

#define RESP_PASS "PASS"
#define RESP_FAIL "FAIL"
#define RESP_VALUE "VALUE"

typedef struct {
    const char *name;
    bool (*test_func)(swd_target_t *target);
    bool passed;
    bool ran;
} test_case_t;

typedef struct {
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
} test_stats_t;

void test_framework_init(swd_target_t *target);
swd_target_t* test_get_target(void);
void test_send_response(const char *status, const char *message);
void test_send_value(uint32_t value);
swd_error_t test_setup(void);
void test_cleanup(void);
void test_final_cleanup(void);
bool test_run_single(test_case_t *test_case);
test_stats_t test_run_suite(test_case_t *tests, uint32_t count);
void test_print_stats(const test_stats_t *stats);

extern test_case_t basic_tests[];
extern const uint32_t basic_test_count;

extern test_case_t hart0_tests[];
extern const uint32_t hart0_test_count;

extern test_case_t hart1_tests[];
extern const uint32_t hart1_test_count;

extern test_case_t dual_hart_tests[];
extern const uint32_t dual_hart_test_count;

extern test_case_t memory_tests[];
extern const uint32_t memory_test_count;

extern test_case_t trace_tests[];
extern const uint32_t trace_test_count;

extern test_case_t api_coverage_tests[];
extern const uint32_t api_coverage_test_count;

extern test_case_t memory_ops_tests[];
extern const uint32_t memory_ops_test_count;

extern test_case_t cache_tests[];
extern const uint32_t cache_test_count;

extern test_case_t code_exec_tests[];
extern const uint32_t code_exec_test_count;

extern test_case_t error_path_tests[];
extern const uint32_t error_path_test_count;

#endif
