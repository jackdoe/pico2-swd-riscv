#ifndef PICO2_SWD_RISCV_SWD_H
#define PICO2_SWD_RISCV_SWD_H

#include "pico2-swd-riscv/types.h"
#include "pico2-swd-riscv/version.h"
#include "hardware/pio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PIO pio;
    uint sm;
    uint pin_swclk;
    uint pin_swdio;
    uint freq_khz;
    bool enable_caching;
    uint retry_count;
} swd_config_t;

typedef struct {
    bool pio0_sm_used[4];
    bool pio1_sm_used[4];
    uint active_targets;
} swd_resource_info_t;

swd_config_t swd_config_default(void);
swd_resource_info_t swd_get_resource_usage(void);

swd_target_t* swd_target_create(const swd_config_t *config);
void swd_target_destroy(swd_target_t *target);

swd_error_t swd_connect(swd_target_t *target);
swd_error_t swd_disconnect(swd_target_t *target);
bool swd_is_connected(const swd_target_t *target);

swd_result_t swd_read_idcode(swd_target_t *target);
const char* swd_get_target_info(const swd_target_t *target);

const char* swd_error_string(swd_error_t error);
const char* swd_get_last_error_detail(const swd_target_t *target);

swd_error_t swd_set_frequency(swd_target_t *target, uint32_t freq_khz);
uint32_t swd_get_frequency(const swd_target_t *target);

#ifdef __cplusplus
}
#endif

#endif
