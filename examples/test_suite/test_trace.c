#include "test_framework.h"
#include "pico2-swd-riscv/rp2350.h"
#include <stdio.h>
#include "pico/stdlib.h"

typedef struct {
    uint32_t instruction_count;
    uint32_t expected_start_pc;
    bool pc_sequence_valid;
    uint32_t last_pc;
} trace_context_t;

static bool trace_callback_basic(const trace_record_t *record, void *user_data) {
    trace_context_t *ctx = (trace_context_t *)user_data;

    printf("# [%lu] PC=0x%08lx INST=0x%08lx\n",
           (unsigned long)ctx->instruction_count,
           (unsigned long)record->pc,
           (unsigned long)record->instruction);

    ctx->instruction_count++;
    return true;
}

static bool test_trace_basic(swd_target_t *target) {
    printf("# Testing basic instruction trace (no register capture)...\n");

    rp2350_halt(target, 0);

    uint32_t program[] = {
        0x00128293,
        0x00230313,
        0x00338393,
        0x0000006f,
    };

    printf("# Program to upload:\n");
    printf("#   0x00128293 = addi x5, x5, 1   (imm=1, rs1=x5, rd=x5, opcode=0x13)\n");
    printf("#   0x00230313 = addi x6, x6, 2   (imm=2, rs1=x6, rd=x6, opcode=0x13)\n");
    printf("#   0x00338393 = addi x7, x7, 3   (imm=3, rs1=x7, rd=x7, opcode=0x13)\n");
    printf("#   0x0000006f = jal x0, 0        (offset=0, rd=x0, opcode=0x6f)\n");

    uint32_t program_addr = 0x20010000;

    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
        if (err != SWD_OK) {
            printf("# Failed to upload program\n");
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    printf("# Verifying uploaded program...\n");
    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_result_t readback = rp2350_read_mem32(target, program_addr + (i * 4));
        printf("#   [%u] 0x%08lx: 0x%08lx (expected 0x%08lx) %s\n",
               i,
               (unsigned long)(program_addr + (i * 4)),
               (unsigned long)readback.value,
               (unsigned long)program[i],
               (readback.value == program[i]) ? "OK" : "MISMATCH!");
    }

    swd_result_t mstatus_read = rp2350_read_csr(target, 0, 0x300);
    if (mstatus_read.error == SWD_OK) {
        printf("# mstatus before: 0x%08lx (MIE=%d)\n",
               (unsigned long)mstatus_read.value,
               (int)((mstatus_read.value >> 3) & 1));
        rp2350_write_csr(target, 0, 0x300, mstatus_read.value & ~(1 << 3));
    }

    rp2350_write_pc(target, 0, program_addr);

    swd_result_t pc_check = rp2350_read_pc(target, 0);
    printf("# After write_pc: PC=0x%08lx (expected 0x%08lx)\n",
           (unsigned long)pc_check.value, (unsigned long)program_addr);

    if (pc_check.value != program_addr) {
        printf("# WARNING: PC write didn't stick!\n");
    }

    rp2350_write_reg(target, 0, 5, 0);
    rp2350_write_reg(target, 0, 6, 0);
    rp2350_write_reg(target, 0, 7, 0);

    trace_context_t ctx = {
        .instruction_count = 0,
        .expected_start_pc = program_addr,
        .pc_sequence_valid = true,
        .last_pc = program_addr
    };

    printf("# Starting trace from PC=0x%08lx...\n", (unsigned long)program_addr);
    int result = rp2350_trace(target, 0, 10, trace_callback_basic, &ctx, false);

    if (result < 0) {
        printf("# Trace failed with error code %d\n", result);
        test_send_response(RESP_FAIL, "Trace failed");
        return false;
    }

    printf("# Traced %d instructions\n", result);

    if (result != 10) {
        printf("# Expected 10 instructions, got %d\n", result);
        test_send_response(RESP_FAIL, "Instruction count mismatch");
        return false;
    }

    printf("# Basic trace test passed\n");
    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool trace_callback_with_regs(const trace_record_t *record, void *user_data) {
    trace_context_t *ctx = (trace_context_t *)user_data;

    printf("# [%lu] PC=0x%08lx INST=0x%08lx\n",
           (unsigned long)ctx->instruction_count,
           (unsigned long)record->pc,
           (unsigned long)record->instruction);

    printf("#      x5=0x%08lx x6=0x%08lx x7=0x%08lx\n",
           (unsigned long)record->regs[5],
           (unsigned long)record->regs[6],
           (unsigned long)record->regs[7]);

    ctx->instruction_count++;
    return true;
}

static bool test_trace_with_registers(swd_target_t *target) {
    printf("# Testing instruction trace with register capture...\n");

    rp2350_halt(target, 0);

    uint32_t program[] = {
        0x00100293,
        0x00200313,
        0x00300393,
        0x006282B3,
        0x007303B3,
        0x0000006f,
    };

    uint32_t program_addr = 0x20010100;

    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
        if (err != SWD_OK) {
            printf("# Failed to upload program\n");
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    swd_result_t mstatus_read = rp2350_read_csr(target, 0, 0x300);
    if (mstatus_read.error == SWD_OK) {
        rp2350_write_csr(target, 0, 0x300, mstatus_read.value & ~(1 << 3));
    }

    rp2350_write_pc(target, 0, program_addr);
    rp2350_write_reg(target, 0, 5, 0);
    rp2350_write_reg(target, 0, 6, 0);
    rp2350_write_reg(target, 0, 7, 0);

    trace_context_t ctx = {
        .instruction_count = 0,
        .expected_start_pc = program_addr,
        .pc_sequence_valid = true,
        .last_pc = program_addr
    };

    printf("# Starting trace with register capture from PC=0x%08lx...\n",
           (unsigned long)program_addr);
    int result = rp2350_trace(target, 0, 5, trace_callback_with_regs, &ctx, true);

    if (result < 0) {
        printf("# Trace failed with error code %d\n", result);
        test_send_response(RESP_FAIL, "Trace failed");
        return false;
    }

    printf("# Traced %d instructions with register capture\n", result);

    swd_result_t x5 = rp2350_read_reg(target, 0, 5);
    swd_result_t x6 = rp2350_read_reg(target, 0, 6);
    swd_result_t x7 = rp2350_read_reg(target, 0, 7);

    printf("# Final register values: x5=0x%08lx x6=0x%08lx x7=0x%08lx\n",
           (unsigned long)x5.value, (unsigned long)x6.value, (unsigned long)x7.value);

    if (x5.value != 3 || x6.value != 2 || x7.value != 5) {
        printf("# Register values don't match (expected x5=3, x6=2, x7=5)\n");
        test_send_response(RESP_FAIL, "Register values incorrect after trace");
        return false;
    }

    test_send_response(RESP_PASS, NULL);
    return true;
}

static bool trace_callback_early_stop(const trace_record_t *record, void *user_data) {
    trace_context_t *ctx = (trace_context_t *)user_data;

    printf("# [%lu] PC=0x%08lx INST=0x%08lx\n",
           (unsigned long)ctx->instruction_count,
           (unsigned long)record->pc,
           (unsigned long)record->instruction);

    ctx->instruction_count++;

    if (ctx->instruction_count >= 7) {
        printf("# Callback requesting early stop after %lu instructions\n",
               (unsigned long)ctx->instruction_count);
        return false;
    }

    return true;
}

static bool test_trace_early_stop(swd_target_t *target) {
    printf("# Testing trace early termination via callback...\n");

    rp2350_halt(target, 0);

    uint32_t program[] = {
        0x00000293,
        0x00128293,
        0x00c0006f,
        0x00000013,
        0x00000013,
        0x00228293,
        0x00c0006f,
        0x00000013,
        0x00000013,
        0x00328293,
        0x0040006f,
        0x0000006f,
    };

    uint32_t program_addr = 0x20010200;

    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
    }

    swd_result_t mstatus_read = rp2350_read_csr(target, 0, 0x300);
    if (mstatus_read.error == SWD_OK) {
        rp2350_write_csr(target, 0, 0x300, mstatus_read.value & ~(1 << 3));
    }

    rp2350_write_pc(target, 0, program_addr);
    rp2350_write_reg(target, 0, 5, 0);

    trace_context_t ctx = {
        .instruction_count = 0,
        .expected_start_pc = program_addr,
        .pc_sequence_valid = true,
        .last_pc = program_addr
    };

    printf("# Requesting 100 instructions, callback will stop at 7...\n");
    printf("# Expected execution: li(0) -> addi(1) -> j -> addi(3) -> j -> addi(6) -> j\n");
    int result = rp2350_trace(target, 0, 100, trace_callback_early_stop, &ctx, false);

    if (result < 0) {
        printf("# Trace failed with error code %d\n", result);
        test_send_response(RESP_FAIL, "Trace failed");
        return false;
    }

    printf("# Traced %d instructions (stopped by callback)\n", result);

    swd_result_t x5 = rp2350_read_reg(target, 0, 5);
    if (x5.error != SWD_OK) {
        printf("# Failed to read x5 after trace\n");
        test_send_response(RESP_FAIL, "Failed to read x5");
        return false;
    }

    printf("# After %d instructions: x5 = 0x%08lx (expected 0x00000006)\n",
           result, (unsigned long)x5.value);

    if (result == 7 && x5.value == 6) {
        printf("# Callback early stop worked correctly, x5 has expected value\n");
        test_send_response(RESP_PASS, NULL);
        return true;
    } else if (result != 7) {
        printf("# Expected 7 instructions, got %d\n", result);
        test_send_response(RESP_FAIL, "Wrong instruction count");
        return false;
    } else {
        printf("# x5 has wrong value (expected 6, got %lu)\n", (unsigned long)x5.value);
        test_send_response(RESP_FAIL, "x5 verification failed");
        return false;
    }
}

typedef struct {
    uint32_t instruction_count;
    uint32_t loop_pc;
    uint32_t loop_count;
    bool loop_detected;
} loop_context_t;

static bool trace_callback_loop_detect(const trace_record_t *record, void *user_data) {
    loop_context_t *ctx = (loop_context_t *)user_data;

    if (ctx->instruction_count == 0) {
        ctx->loop_pc = record->pc;
        printf("# Loop entry point: PC=0x%08lx\n", (unsigned long)record->pc);
    } else if (record->pc == ctx->loop_pc) {
        ctx->loop_count++;
        printf("# Loop iteration %lu detected\n", (unsigned long)ctx->loop_count);

        if (ctx->loop_count >= 3) {
            printf("# Detected 3 loop iterations, stopping trace\n");
            ctx->loop_detected = true;
            return false;
        }
    }

    ctx->instruction_count++;
    return true;
}

static bool test_trace_loop_detection(swd_target_t *target) {
    printf("# Testing loop detection during trace...\n");

    rp2350_halt(target, 0);

    uint32_t program[] = {
        0x00128293,
        0x00230313,
        0xFF9FF06F,
    };

    uint32_t program_addr = 0x20010300;

    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
    }

    swd_result_t mstatus_read = rp2350_read_csr(target, 0, 0x300);
    if (mstatus_read.error == SWD_OK) {
        rp2350_write_csr(target, 0, 0x300, mstatus_read.value & ~(1 << 3));
    }

    rp2350_write_pc(target, 0, program_addr);
    rp2350_write_reg(target, 0, 5, 0);
    rp2350_write_reg(target, 0, 6, 0);

    loop_context_t ctx = {
        .instruction_count = 0,
        .loop_pc = 0,
        .loop_count = 0,
        .loop_detected = false
    };

    printf("# Starting trace to detect loop...\n");
    int result = rp2350_trace(target, 0, 50, trace_callback_loop_detect, &ctx, false);

    if (result < 0) {
        printf("# Trace failed with error code %d\n", result);
        test_send_response(RESP_FAIL, "Trace failed");
        return false;
    }

    printf("# Traced %d instructions\n", result);
    printf("# Loop detected: %s, Loop count: %lu\n",
           ctx.loop_detected ? "YES" : "NO",
           (unsigned long)ctx.loop_count);

    if (ctx.loop_detected && ctx.loop_count >= 3) {
        printf("# Loop detection test passed\n");
        test_send_response(RESP_PASS, NULL);
        return true;
    } else {
        printf("# Loop detection test failed\n");
        test_send_response(RESP_FAIL, "Loop not detected");
        return false;
    }
}

static bool trace_callback_hart1(const trace_record_t *record, void *user_data) {
    trace_context_t *ctx = (trace_context_t *)user_data;

    printf("# [Hart1-%lu] PC=0x%08lx INST=0x%08lx\n",
           (unsigned long)ctx->instruction_count,
           (unsigned long)record->pc,
           (unsigned long)record->instruction);

    ctx->instruction_count++;
    return true;
}

static bool test_trace_hart1(swd_target_t *target) {
    printf("# Testing instruction trace on hart 1...\n");

    rp2350_halt(target, 1);

    uint32_t program[] = {
        0x00100313,
        0x00200393,
        0x007303B3,
        0x0000006f,
    };

    uint32_t program_addr = 0x20011000;

    for (uint i = 0; i < sizeof(program)/sizeof(program[0]); i++) {
        swd_error_t err = rp2350_write_mem32(target, program_addr + (i * 4), program[i]);
        if (err != SWD_OK) {
            printf("# Failed to upload program\n");
            test_send_response(RESP_FAIL, "Program upload failed");
            return false;
        }
    }

    swd_result_t mstatus_read = rp2350_read_csr(target, 1, 0x300);
    if (mstatus_read.error == SWD_OK) {
        rp2350_write_csr(target, 1, 0x300, mstatus_read.value & ~(1 << 3));
    }

    rp2350_write_pc(target, 1, program_addr);
    rp2350_write_reg(target, 1, 6, 0);
    rp2350_write_reg(target, 1, 7, 0);

    trace_context_t ctx = {
        .instruction_count = 0,
        .expected_start_pc = program_addr,
        .pc_sequence_valid = true,
        .last_pc = program_addr
    };

    printf("# Starting trace on hart 1 from PC=0x%08lx...\n",
           (unsigned long)program_addr);
    int result = rp2350_trace(target, 1, 8, trace_callback_hart1, &ctx, false);

    if (result < 0) {
        printf("# Trace failed with error code %d\n", result);
        test_send_response(RESP_FAIL, "Hart 1 trace failed");
        return false;
    }

    printf("# Traced %d instructions on hart 1\n", result);

    if (result == 8) {
        printf("# Hart 1 trace test passed\n");
        test_send_response(RESP_PASS, NULL);
        return true;
    } else {
        printf("# Expected 8 instructions, got %d\n", result);
        test_send_response(RESP_FAIL, "Instruction count mismatch");
        return false;
    }
}

test_case_t trace_tests[] = {
    { "TRACE 1: Basic Instruction Trace", test_trace_basic, false, false },
    { "TRACE 2: Trace with Register Capture", test_trace_with_registers, false, false },
    { "TRACE 3: Early Termination via Callback", test_trace_early_stop, false, false },
    { "TRACE 4: Loop Detection", test_trace_loop_detection, false, false },
    { "TRACE 5: Trace Hart 1", test_trace_hart1, false, false },
};

const uint32_t trace_test_count = sizeof(trace_tests) / sizeof(trace_tests[0]);
