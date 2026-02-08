#ifndef PICO2_SWD_RISCV_TYPES_H
#define PICO2_SWD_RISCV_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct swd_target swd_target_t;

typedef enum {
    SWD_OK = 0,
    SWD_ERROR_TIMEOUT,
    SWD_ERROR_FAULT,
    SWD_ERROR_PROTOCOL,
    SWD_ERROR_PARITY,
    SWD_ERROR_WAIT,
    SWD_ERROR_NOT_CONNECTED,
    SWD_ERROR_NOT_HALTED,
    SWD_ERROR_ALREADY_HALTED,
    SWD_ERROR_INVALID_STATE,
    SWD_ERROR_NO_MEMORY,
    SWD_ERROR_INVALID_CONFIG,
    SWD_ERROR_RESOURCE_BUSY,
    SWD_ERROR_INVALID_PARAM,
    SWD_ERROR_NOT_INITIALIZED,
    SWD_ERROR_ABSTRACT_CMD,
    SWD_ERROR_BUS,
    SWD_ERROR_ALIGNMENT,
    SWD_ERROR_VERIFY,
    SWD_ERROR_COUNT
} swd_error_t;

typedef struct {
    swd_error_t error;
    uint32_t value;
} swd_result_t;

#define SWD_AUTO 0xFF

#define SWD_ACK_OK     0x1
#define SWD_ACK_WAIT   0x2
#define SWD_ACK_FAULT  0x4
#define SWD_ACK_ERROR  0x7

#ifdef __cplusplus
}
#endif

#endif
