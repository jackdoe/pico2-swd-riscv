#include "pico2-swd-riscv/swd.h"
#include "pico2-swd-riscv/dap.h"
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

resource_tracker_t g_resources = {
    .pio0_sm_owners = {NULL, NULL, NULL, NULL},
    .pio1_sm_owners = {NULL, NULL, NULL, NULL},
    .active_count = 0
};

static const char* error_strings[] = {
    [SWD_OK] = "Success",
    [SWD_ERROR_TIMEOUT] = "Timeout",
    [SWD_ERROR_FAULT] = "Target fault",
    [SWD_ERROR_PROTOCOL] = "Protocol error",
    [SWD_ERROR_PARITY] = "Parity error",
    [SWD_ERROR_WAIT] = "Wait timeout",
    [SWD_ERROR_NOT_CONNECTED] = "Not connected",
    [SWD_ERROR_NOT_HALTED] = "Hart not halted",
    [SWD_ERROR_ALREADY_HALTED] = "Hart already halted",
    [SWD_ERROR_INVALID_STATE] = "Invalid state",
    [SWD_ERROR_NO_MEMORY] = "Out of memory",
    [SWD_ERROR_INVALID_CONFIG] = "Invalid configuration",
    [SWD_ERROR_RESOURCE_BUSY] = "Resource busy",
    [SWD_ERROR_INVALID_PARAM] = "Invalid parameter",
    [SWD_ERROR_NOT_INITIALIZED] = "Not initialized",
    [SWD_ERROR_ABSTRACT_CMD] = "Abstract command failed",
    [SWD_ERROR_BUS] = "Bus error",
    [SWD_ERROR_ALIGNMENT] = "Alignment error",
    [SWD_ERROR_VERIFY] = "Verification failed",
};

const char* swd_error_string(swd_error_t error) {
    if (error >= SWD_ERROR_COUNT) {
        return "Unknown error";
    }
    return error_strings[error];
}

void swd_set_error(swd_target_t *target, swd_error_t error, const char *detail, ...) {
    if (!target) return;

    target->last_error = error;

    if (detail) {
        va_list args;
        va_start(args, detail);
        vsnprintf(target->error_detail, sizeof(target->error_detail), detail, args);
        va_end(args);
    } else {
        target->error_detail[0] = '\0';
    }

    if (error != SWD_OK) {
        SWD_WARN("%s: %s\n", swd_error_string(error), target->error_detail);
    }
}

const char* swd_get_last_error_detail(const swd_target_t *target) {
    if (!target) return "";
    return target->error_detail;
}

swd_error_t swd_ack_to_error(uint8_t ack) {
    switch (ack) {
        case SWD_ACK_OK:    return SWD_OK;
        case SWD_ACK_WAIT:  return SWD_ERROR_WAIT;
        case SWD_ACK_FAULT: return SWD_ERROR_FAULT;
        case SWD_ACK_ERROR: return SWD_ERROR_PROTOCOL;
        default:            return SWD_ERROR_PROTOCOL;
    }
}

swd_error_t allocate_pio_sm(PIO *pio, uint *sm) {
    for (uint i = 0; i < 4; i++) {
        if (g_resources.pio0_sm_owners[i] == NULL) {
            *pio = pio0;
            *sm = i;
            return SWD_OK;
        }
    }

    for (uint i = 0; i < 4; i++) {
        if (g_resources.pio1_sm_owners[i] == NULL) {
            *pio = pio1;
            *sm = i;
            return SWD_OK;
        }
    }

    return SWD_ERROR_RESOURCE_BUSY;
}

void release_pio_sm(PIO pio, uint sm) {
    if (sm >= 4) return;

    if (pio == pio0) {
        g_resources.pio0_sm_owners[sm] = NULL;
    } else if (pio == pio1) {
        g_resources.pio1_sm_owners[sm] = NULL;
    }

    if (g_resources.active_count > 0) {
        g_resources.active_count--;
    }
}

bool register_target(swd_target_t *target, PIO pio, uint sm) {
    if (!target || sm >= 4) return false;

    swd_target_t **owner_ptr = NULL;

    if (pio == pio0) {
        owner_ptr = &g_resources.pio0_sm_owners[sm];
    } else if (pio == pio1) {
        owner_ptr = &g_resources.pio1_sm_owners[sm];
    } else {
        return false;
    }

    if (*owner_ptr != NULL) {
        return false;
    }

    *owner_ptr = target;
    g_resources.active_count++;
    return true;
}

void unregister_target(swd_target_t *target) {
    if (!target || !target->resource_registered) return;

    release_pio_sm(target->pio.pio, target->pio.sm);
    target->resource_registered = false;
}

swd_resource_info_t swd_get_resource_usage(void) {
    swd_resource_info_t info = {0};
    info.active_targets = g_resources.active_count;

    for (uint i = 0; i < 4; i++) {
        info.pio0_sm_used[i] = (g_resources.pio0_sm_owners[i] != NULL);
        info.pio1_sm_used[i] = (g_resources.pio1_sm_owners[i] != NULL);
    }

    return info;
}

swd_config_t swd_config_default(void) {
    swd_config_t config = {
        .pio = (PIO)SWD_AUTO,
        .sm = SWD_AUTO,
        .pin_swclk = 0,
        .pin_swdio = 0,
        .freq_khz = 1000,
        .enable_caching = true,
        .retry_count = 5,
    };
    return config;
}

swd_target_t* swd_target_create(const swd_config_t *config) {
    if (!config) {
        SWD_WARN("swd_target_create: NULL config\n");
        return NULL;
    }

    swd_target_t *target = calloc(1, sizeof(swd_target_t));
    if (!target) {
        SWD_WARN("swd_target_create: out of memory\n");
        return NULL;
    }

    target->last_error = SWD_OK;
    target->connected = false;
    target->resource_registered = false;

    target->pio.pin_swclk = config->pin_swclk;
    target->pio.pin_swdio = config->pin_swdio;
    target->pio.freq_khz = config->freq_khz;
    target->pio.initialized = false;

    if ((uintptr_t)config->pio == SWD_AUTO || config->sm == SWD_AUTO) {
        PIO pio;
        uint sm;
        swd_error_t err = allocate_pio_sm(&pio, &sm);
        if (err != SWD_OK) {
            swd_set_error(target, err, "No free PIO/SM available");
            free(target);
            return NULL;
        }
        target->pio.pio = pio;
        target->pio.sm = sm;
    } else {
        target->pio.pio = config->pio;
        target->pio.sm = config->sm;
    }

    if (!register_target(target, target->pio.pio, target->pio.sm)) {
        swd_set_error(target, SWD_ERROR_RESOURCE_BUSY,
                     "PIO%d SM%d already in use",
                     target->pio.pio == pio0 ? 0 : 1, target->pio.sm);
        free(target);
        return NULL;
    }
    target->resource_registered = true;

    target->dap.powered = false;
    target->dap.retry_count = config->retry_count;
    target->dap.current_apsel = 0xFF;
    target->dap.current_bank = 0xFF;

    target->rp2350.cache_enabled = config->enable_caching;
    target->rp2350.initialized = false;
    target->rp2350.sba_initialized = false;

    for (uint8_t i = 0; i < RP2350_NUM_HARTS; i++) {
        target->rp2350.harts[i].halt_state_known = false;
        target->rp2350.harts[i].halted = false;
        target->rp2350.harts[i].cache_valid = false;
    }

    SWD_INFO("Created target: PIO%d SM%d, pins SWCLK=%d SWDIO=%d\n",
             target->pio.pio == pio0 ? 0 : 1, target->pio.sm,
             target->pio.pin_swclk, target->pio.pin_swdio);

    return target;
}

void swd_target_destroy(swd_target_t *target) {
    if (!target) return;

    if (target->connected) {
        swd_disconnect(target);
    }

    unregister_target(target);

    SWD_INFO("Destroyed target: PIO%d SM%d\n",
             target->pio.pio == pio0 ? 0 : 1, target->pio.sm);

    free(target);
}

bool swd_is_connected(const swd_target_t *target) {
    return target && target->connected;
}

swd_result_t swd_read_idcode(swd_target_t *target) {
    swd_result_t result = {.error = SWD_ERROR_NOT_CONNECTED, .value = 0};

    if (!target) {
        return result;
    }

    if (!target->connected) {
        swd_set_error(target, SWD_ERROR_NOT_CONNECTED, "Not connected");
        return result;
    }

    result.value = target->idcode;
    result.error = SWD_OK;
    return result;
}

const char* swd_get_target_info(const swd_target_t *target) {
    static char info_buf[128];

    if (!target || !target->connected) {
        return NULL;
    }

    snprintf(info_buf, sizeof(info_buf),
             "IDCODE: 0x%08lX, PIO%d SM%d, SWCLK=%d SWDIO=%d, %u kHz",
             (unsigned long)target->idcode,
             target->pio.pio == pio0 ? 0 : 1,
             target->pio.sm,
             target->pio.pin_swclk,
             target->pio.pin_swdio,
             target->pio.freq_khz);

    return info_buf;
}

uint32_t swd_get_frequency(const swd_target_t *target) {
    return target ? target->pio.freq_khz : 0;
}
