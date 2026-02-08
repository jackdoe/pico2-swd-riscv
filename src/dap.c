#include "pico2-swd-riscv/dap.h"
#include "pico2-swd-riscv/swd.h"
#include "internal.h"
#include "pico/stdlib.h"
#include <stdio.h>

uint32_t encode_dp_select(uint8_t apsel, uint8_t bank, bool ctrlsel) {
    return ((apsel & 0xF) << 12) | (0xD << 8) | ((bank & 0xF) << 4) | (ctrlsel ? 1 : 0);
}

static swd_error_t select_ap_bank(swd_target_t *target, uint8_t apsel, uint8_t bank) {
    if (target->dap.current_apsel == apsel &&
        target->dap.current_bank == bank) {
        SWD_DEBUG("AP bank already selected (APSEL=%u, bank=%u)\n", apsel, bank);
        return SWD_OK;
    }

    uint32_t select_val = encode_dp_select(apsel, bank, true);

    swd_error_t err = swd_write_dp_raw(target, DP_SELECT, select_val);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to select AP bank (APSEL=%u, bank=%u)", apsel, bank);
        return err;
    }

    target->dap.current_apsel = apsel;
    target->dap.current_bank = bank;
    target->dap.ctrlsel = true;
    target->dap.select_cache = select_val;

    SWD_DEBUG("Selected AP bank: APSEL=%u, bank=%u\n", apsel, bank);
    return SWD_OK;
}

swd_error_t dap_power_up(swd_target_t *target) {
    if (!target) {
        return SWD_ERROR_INVALID_PARAM;
    }

    if (target->dap.powered) {
        return SWD_OK;
    }

    SWD_INFO("Powering up debug domains...\n");

    swd_error_t err = swd_write_dp_raw(target, DP_CTRL_STAT, 0);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to clear DP_CTRL_STAT");
        return err;
    }

    uint32_t ctrl_stat = CTRL_STAT_CDBGPWRUPREQ | CTRL_STAT_CSYSPWRUPREQ;
    err = swd_write_dp_raw(target, DP_CTRL_STAT, ctrl_stat);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to request power-up");
        return err;
    }

    for (int i = 0; i < 10; i++) {
        uint32_t status;
        err = swd_read_dp_raw(target, DP_CTRL_STAT, &status);
        if (err != SWD_OK) {
            swd_set_error(target, err, "Failed to read power status");
            return err;
        }

        bool cdbgpwrupack = status & CTRL_STAT_CDBGPWRUPACK;
        bool csyspwrupack = status & CTRL_STAT_CSYSPWRUPACK;

        if (cdbgpwrupack && csyspwrupack) {
            SWD_INFO("Debug domains powered up\n");
            target->dap.powered = true;
            return SWD_OK;
        }

        sleep_ms(20);
    }

    swd_set_error(target, SWD_ERROR_TIMEOUT, "Power-up timeout");
    return SWD_ERROR_TIMEOUT;
}

swd_error_t dap_power_down(swd_target_t *target) {
    if (!target) {
        return SWD_ERROR_INVALID_PARAM;
    }

    if (!target->dap.powered) {
        return SWD_OK;
    }

    SWD_INFO("Powering down debug domains...\n");

    swd_error_t err = swd_write_dp_raw(target, DP_CTRL_STAT, 0);
    if (err == SWD_OK) {
        target->dap.powered = false;
    }

    return err;
}

bool dap_is_powered(const swd_target_t *target) {
    return target && target->dap.powered;
}

swd_result_t dap_read_dp(swd_target_t *target, uint8_t reg) {
    swd_result_t result = {.error = SWD_ERROR_INVALID_PARAM, .value = 0};

    if (!target) {
        return result;
    }

    result.error = swd_read_dp_raw(target, reg, &result.value);
    if (result.error != SWD_OK) {
        swd_set_error(target, result.error, "DP read failed (reg=0x%02x)", reg);
    } else {
        SWD_DEBUG("DP read: reg=0x%02x, value=0x%08lx\n", reg, (unsigned long)result.value);
    }

    return result;
}

swd_error_t dap_write_dp(swd_target_t *target, uint8_t reg, uint32_t value) {
    if (!target) {
        return SWD_ERROR_INVALID_PARAM;
    }

    SWD_DEBUG("DP write: reg=0x%02x, value=0x%08lx\n", reg, (unsigned long)value);

    swd_error_t err = swd_write_dp_raw(target, reg, value);
    if (err != SWD_OK) {
        swd_set_error(target, err, "DP write failed (reg=0x%02x, value=0x%08x)", reg, value);
    }

    return err;
}

swd_result_t dap_read_ap(swd_target_t *target, uint8_t apsel, uint8_t reg) {
    swd_result_t result = {.error = SWD_ERROR_NOT_CONNECTED, .value = 0};

    if (!target || !target->connected) {
        return result;
    }

    uint8_t bank = (reg >> 4) & 0xF;
    result.error = select_ap_bank(target, apsel, bank);
    if (result.error != SWD_OK) {
        return result;
    }

    result.error = swd_read_ap_raw(target, reg, &result.value);
    if (result.error != SWD_OK) {
        swd_set_error(target, result.error,
                     "AP read failed (apsel=%u, reg=0x%02x)", apsel, reg);
        return result;
    }

    uint32_t final_value;
    result.error = swd_read_dp_raw(target, DP_RDBUFF, &final_value);
    if (result.error == SWD_OK) {
        result.value = final_value;
        SWD_DEBUG("AP read: apsel=%u, reg=0x%02x, value=0x%08lx\n",
                  apsel, reg, (unsigned long)result.value);
    } else {
        swd_set_error(target, result.error, "RDBUFF read failed");
    }

    return result;
}

swd_error_t dap_write_ap(swd_target_t *target, uint8_t apsel, uint8_t reg, uint32_t value) {
    if (!target || !target->connected) {
        return SWD_ERROR_NOT_CONNECTED;
    }

    uint8_t bank = (reg >> 4) & 0xF;
    swd_error_t err = select_ap_bank(target, apsel, bank);
    if (err != SWD_OK) {
        return err;
    }

    SWD_DEBUG("AP write: apsel=%u, reg=0x%02x, value=0x%08lx\n", apsel, reg, (unsigned long)value);

    err = swd_write_ap_raw(target, reg, value);
    if (err != SWD_OK) {
        swd_set_error(target, err,
                     "AP write failed (apsel=%u, reg=0x%02x, value=0x%08x)",
                     apsel, reg, value);
    }

    return err;
}

swd_result_t dap_read_mem32(swd_target_t *target, uint32_t addr) {
    swd_result_t result = {.error = SWD_ERROR_NOT_CONNECTED, .value = 0};

    if (!target || !target->connected) {
        return result;
    }

    if (addr & 0x3) {
        result.error = SWD_ERROR_ALIGNMENT;
        swd_set_error(target, result.error, "Address 0x%08x not 4-byte aligned", addr);
        return result;
    }

    SWD_DEBUG("MEM read: addr=0x%08lx\n", (unsigned long)addr);

    result.error = dap_write_ap(target, AP_RISCV, AP_TAR, addr);
    if (result.error != SWD_OK) {
        return result;
    }

    result.error = swd_read_ap_raw(target, AP_DRW, &result.value);
    if (result.error != SWD_OK) {
        swd_set_error(target, result.error, "DRW read failed");
        return result;
    }

    uint32_t value;
    result.error = swd_read_dp_raw(target, DP_RDBUFF, &value);
    if (result.error == SWD_OK) {
        result.value = value;
        SWD_DEBUG("MEM read: addr=0x%08lx -> 0x%08lx\n", (unsigned long)addr, (unsigned long)result.value);
    }

    return result;
}

swd_error_t dap_write_mem32(swd_target_t *target, uint32_t addr, uint32_t value) {
    if (!target || !target->connected) {
        return SWD_ERROR_NOT_CONNECTED;
    }

    if (addr & 0x3) {
        swd_set_error(target, SWD_ERROR_ALIGNMENT,
                     "Address 0x%08x not 4-byte aligned", addr);
        return SWD_ERROR_ALIGNMENT;
    }

    SWD_DEBUG("MEM write: addr=0x%08lx <- 0x%08lx\n", (unsigned long)addr, (unsigned long)value);

    swd_error_t err = dap_write_ap(target, AP_RISCV, AP_TAR, addr);
    if (err != SWD_OK) {
        return err;
    }

    err = dap_write_ap(target, AP_RISCV, AP_DRW, value);
    if (err != SWD_OK) {
        swd_set_error(target, err, "DRW write failed");
        return err;
    }

    uint32_t dummy;
    err = swd_read_dp_raw(target, DP_RDBUFF, &dummy);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to complete write");
        return err;
    }

    return SWD_OK;
}

swd_error_t dap_clear_errors(swd_target_t *target) {
    if (!target) {
        return SWD_ERROR_INVALID_PARAM;
    }

    SWD_INFO("Clearing sticky error flags\n");

    swd_error_t err = swd_write_dp_raw(target, DP_CTRL_STAT, CTRL_STAT_ERROR_CLEAR);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to clear error flags");
    }

    return err;
}
