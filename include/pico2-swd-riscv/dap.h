#ifndef PICO2_SWD_RISCV_DAP_H
#define PICO2_SWD_RISCV_DAP_H

#include "pico2-swd-riscv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_IDCODE     0x0
#define DP_CTRL_STAT  0x4
#define DP_SELECT     0x8
#define DP_RDBUFF     0xC

#define AP_CSW  0x00
#define AP_TAR  0x04
#define AP_DRW  0x0C
#define AP_IDR  0xFC

#define AP_ROM_TABLE    0x0
#define AP_ARM_CORE0    0x2
#define AP_ARM_CORE1    0x4
#define AP_RISCV        0xA
#define AP_RP_SPECIFIC  0x8

#define CTRL_STAT_CDBGPWRUPREQ  (1U << 28)
#define CTRL_STAT_CDBGPWRUPACK  (1U << 29)
#define CTRL_STAT_CSYSPWRUPREQ  (1U << 30)
#define CTRL_STAT_CSYSPWRUPACK  (1U << 31)

#define CTRL_STAT_STICKYORUN    (1U << 1)
#define CTRL_STAT_STICKYCMP     (1U << 4)
#define CTRL_STAT_STICKYERR     (1U << 5)
#define CTRL_STAT_WDATAERR      (1U << 7)

#define CTRL_STAT_ERROR_CLEAR   (CTRL_STAT_STICKYORUN | CTRL_STAT_STICKYCMP | \
                                 CTRL_STAT_STICKYERR | CTRL_STAT_WDATAERR)

swd_error_t dap_power_up(swd_target_t *target);
swd_error_t dap_power_down(swd_target_t *target);
bool dap_is_powered(const swd_target_t *target);

swd_result_t dap_read_dp(swd_target_t *target, uint8_t reg);
swd_error_t dap_write_dp(swd_target_t *target, uint8_t reg, uint32_t value);

swd_result_t dap_read_ap(swd_target_t *target, uint8_t apsel, uint8_t reg);
swd_error_t dap_write_ap(swd_target_t *target, uint8_t apsel, uint8_t reg, uint32_t value);

swd_result_t dap_read_mem32(swd_target_t *target, uint32_t addr);
swd_error_t dap_write_mem32(swd_target_t *target, uint32_t addr, uint32_t value);

swd_error_t dap_clear_errors(swd_target_t *target);

#ifdef __cplusplus
}
#endif

#endif
