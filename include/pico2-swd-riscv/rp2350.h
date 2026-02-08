#ifndef PICO2_SWD_RISCV_RP2350_H
#define PICO2_SWD_RISCV_RP2350_H

#include "pico2-swd-riscv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

swd_error_t rp2350_init(swd_target_t *target);
bool rp2350_is_initialized(const swd_target_t *target);

swd_error_t rp2350_halt(swd_target_t *target, uint8_t hart_id);
swd_error_t rp2350_resume(swd_target_t *target, uint8_t hart_id);
swd_error_t rp2350_step(swd_target_t *target, uint8_t hart_id);
swd_error_t rp2350_reset(swd_target_t *target, uint8_t hart_id, bool halt_on_reset);
bool rp2350_is_halted(swd_target_t *target, uint8_t hart_id);

swd_result_t rp2350_read_pc(swd_target_t *target, uint8_t hart_id);
swd_error_t rp2350_write_pc(swd_target_t *target, uint8_t hart_id, uint32_t pc);

swd_result_t rp2350_read_reg(swd_target_t *target, uint8_t hart_id, uint8_t reg_num);
swd_error_t rp2350_write_reg(swd_target_t *target, uint8_t hart_id, uint8_t reg_num, uint32_t value);
swd_error_t rp2350_read_all_regs(swd_target_t *target, uint8_t hart_id, uint32_t regs[32]);

swd_result_t rp2350_read_csr(swd_target_t *target, uint8_t hart_id, uint16_t csr_addr);
swd_error_t rp2350_write_csr(swd_target_t *target, uint8_t hart_id, uint16_t csr_addr, uint32_t value);

void rp2350_invalidate_cache(swd_target_t *target, uint8_t hart_id);
void rp2350_enable_cache(swd_target_t *target, bool enable);

swd_result_t rp2350_read_mem32(swd_target_t *target, uint32_t addr);
swd_error_t rp2350_write_mem32(swd_target_t *target, uint32_t addr, uint32_t value);
swd_result_t rp2350_read_mem16(swd_target_t *target, uint32_t addr);
swd_error_t rp2350_write_mem16(swd_target_t *target, uint32_t addr, uint16_t value);
swd_result_t rp2350_read_mem8(swd_target_t *target, uint32_t addr);
swd_error_t rp2350_write_mem8(swd_target_t *target, uint32_t addr, uint8_t value);

swd_error_t rp2350_read_mem_block(swd_target_t *target, uint32_t addr,
                                   uint32_t *buffer, uint32_t count);
swd_error_t rp2350_write_mem_block(swd_target_t *target, uint32_t addr,
                                    const uint32_t *buffer, uint32_t count);

swd_error_t rp2350_execute_progbuf(swd_target_t *target, uint8_t hart_id,
                                    const uint32_t *instructions, uint8_t count);

swd_error_t rp2350_upload_code(swd_target_t *target, uint32_t addr,
                                const uint32_t *code, uint32_t word_count);
swd_error_t rp2350_execute_code(swd_target_t *target, uint8_t hart_id, uint32_t entry_point,
                                 const uint32_t *code, uint32_t word_count);

typedef struct {
    uint32_t pc;
    uint32_t instruction;
    uint32_t regs[32];
} trace_record_t;

typedef bool (*trace_callback_t)(const trace_record_t *record, void *user_data);

int rp2350_trace(swd_target_t *target, uint8_t hart_id, uint32_t max_instructions,
                 trace_callback_t callback, void *user_data,
                 bool capture_regs);

#ifdef __cplusplus
}
#endif

#endif
