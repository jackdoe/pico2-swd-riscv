#include "pico2-swd-riscv/rp2350.h"
#include "pico2-swd-riscv/dap.h"
#include "pico2-swd-riscv/swd.h"
#include "internal.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define RISCV_CSRR(rd, csr)  (0x00002073 | ((rd) << 7) | ((csr) << 20))
#define RISCV_CSRW(csr, rs1) (0x00001073 | ((rs1) << 15) | ((csr) << 20))
#define RISCV_EBREAK          0x00100073

static swd_error_t rp2350_init_sba(swd_target_t *target);

static inline hart_state_t *get_hart(swd_target_t *target, uint8_t hart_id) {
    if (!target || !target->rp2350.initialized || hart_id >= RP2350_NUM_HARTS)
        return NULL;
    return &target->rp2350.harts[hart_id];
}

static inline uint32_t encode_dmcontrol(uint8_t hart_id, bool haltreq,
                                         bool resumereq, bool ndmreset) {
    uint32_t dmcontrol = DMCONTROL_DMACTIVE;
    dmcontrol |= ((uint32_t)hart_id << DMCONTROL_HARTSELLO_SHIFT);
    if (haltreq) dmcontrol |= DMCONTROL_HALTREQ;
    if (resumereq) dmcontrol |= DMCONTROL_RESUMEREQ;
    if (ndmreset) dmcontrol |= DMCONTROL_NDMRESET;
    return dmcontrol;
}

static swd_error_t wait_abstract_command(swd_target_t *target) {
    for (int i = 0; i < 100; i++) {
        swd_result_t result = dap_read_mem32(target, DM_ABSTRACTCS);
        if (result.error != SWD_OK) {
            return result.error;
        }

        bool busy = result.value & ABSTRACTCS_BUSY;
        uint8_t cmderr = (result.value & ABSTRACTCS_CMDERR_MASK) >> ABSTRACTCS_CMDERR_SHIFT;

        if (!busy) {
            if (cmderr != 0) {
                dap_write_mem32(target, DM_ABSTRACTCS, ABSTRACTCS_CMDERR_MASK);
                swd_set_error(target, SWD_ERROR_ABSTRACT_CMD,
                             "Abstract command error: %u", cmderr);
                return SWD_ERROR_ABSTRACT_CMD;
            }
            return SWD_OK;
        }

        sleep_us(100);
    }

    swd_set_error(target, SWD_ERROR_TIMEOUT, "Abstract command timeout");
    return SWD_ERROR_TIMEOUT;
}

static swd_error_t poll_dmstatus_halted(swd_target_t *target, uint8_t hart_id,
                                         bool wait_for_halted) {
    hart_state_t *hart = &target->rp2350.harts[hart_id];

    for (int i = 0; i < 10; i++) {
        swd_result_t result = dap_read_mem32(target, DM_DMSTATUS);
        if (result.error != SWD_OK) {
            return result.error;
        }

        bool allhalted = result.value & DMSTATUS_ALLHALTED;
        bool allrunning = result.value & DMSTATUS_ALLRUNNING;

        if (wait_for_halted && allhalted) {
            hart->halted = true;
            hart->halt_state_known = true;
            return SWD_OK;
        }

        if (!wait_for_halted && allrunning) {
            hart->halted = false;
            hart->halt_state_known = true;
            return SWD_OK;
        }

        sleep_ms(10);
    }

    return SWD_ERROR_TIMEOUT;
}

static swd_error_t execute_progbuf_simple(swd_target_t *target, uint8_t hart_id,
                                           const uint32_t *instructions, uint8_t count) {
    if (!instructions || count == 0 || count > 16) {
        return SWD_ERROR_INVALID_PARAM;
    }

    uint32_t dmcontrol = encode_dmcontrol(hart_id, false, false, false);
    swd_error_t err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK) {
        return err;
    }

    for (uint8_t i = 0; i < count; i++) {
        err = dap_write_mem32(target, DM_PROGBUF0 + (i * 4), instructions[i]);
        if (err != SWD_OK) {
            return err;
        }
    }

    uint32_t command = ABSCMD_POSTEXEC;
    err = dap_write_mem32(target, DM_COMMAND, command);
    if (err != SWD_OK) {
        return err;
    }

    err = wait_abstract_command(target);
    if (err == SWD_OK && hart_id < RP2350_NUM_HARTS)
        target->rp2350.harts[hart_id].cache_valid = false;
    return err;
}

static swd_error_t execute_with_saved_s0(swd_target_t *target, uint8_t hart_id,
                                          const uint32_t *progbuf, uint8_t count,
                                          uint32_t *result_s0) {
    swd_result_t saved = rp2350_read_reg(target, hart_id, 8);
    if (saved.error != SWD_OK)
        return saved.error;

    swd_error_t err = execute_progbuf_simple(target, hart_id, progbuf, count);
    if (err != SWD_OK) {
        rp2350_write_reg(target, hart_id, 8, saved.value);
        return err;
    }

    if (result_s0) {
        swd_result_t r = rp2350_read_reg(target, hart_id, 8);
        if (r.error != SWD_OK) {
            rp2350_write_reg(target, hart_id, 8, saved.value);
            return r.error;
        }
        *result_s0 = r.value;
    }

    rp2350_write_reg(target, hart_id, 8, saved.value);
    return SWD_OK;
}

swd_error_t rp2350_init(swd_target_t *target) {
    if (!target || !target->connected) {
        return SWD_ERROR_NOT_CONNECTED;
    }

    if (target->rp2350.initialized) {
        return SWD_OK;
    }

    SWD_INFO("Initializing RP2350 Debug Module...\n");

    uint32_t sel_bank0 = encode_dp_select(AP_RISCV, 0, true);
    swd_error_t err = dap_write_dp(target, DP_SELECT, sel_bank0);
    if (err != SWD_OK) {
        return err;
    }

    err = dap_write_ap(target, AP_RISCV, AP_CSW, CSW_32BIT_AUTOINC);
    if (err != SWD_OK) {
        return err;
    }

    err = dap_write_ap(target, AP_RISCV, AP_TAR, DM_DMCONTROL);
    if (err != SWD_OK) {
        return err;
    }

    uint32_t sel_bank1 = encode_dp_select(AP_RISCV, 1, true);
    err = dap_write_dp(target, DP_SELECT, sel_bank1);
    if (err != SWD_OK) {
        return err;
    }

    SWD_DEBUG("Performing DM activation handshake...\n");

    err = dap_write_ap(target, AP_RISCV, AP_CSW, DM_DEACTIVATE);
    if (err != SWD_OK) return err;
    dap_read_dp(target, DP_RDBUFF);
    sleep_ms(50);

    err = dap_write_ap(target, AP_RISCV, AP_CSW, DM_ACTIVATE);
    if (err != SWD_OK) return err;
    dap_read_dp(target, DP_RDBUFF);
    sleep_ms(50);

    err = dap_write_ap(target, AP_RISCV, AP_CSW, DM_FULL_CONFIG);
    if (err != SWD_OK) return err;
    dap_read_dp(target, DP_RDBUFF);
    sleep_ms(50);

    swd_read_ap_raw(target, AP_CSW, NULL);
    swd_result_t status_result = dap_read_dp(target, DP_RDBUFF);
    if (status_result.error != SWD_OK) {
        swd_set_error(target, status_result.error, "Failed to read DM status");
        return status_result.error;
    }

    if (status_result.value != DM_STATUS_READY) {
        swd_set_error(target, SWD_ERROR_INVALID_STATE,
                     "Unexpected DM status: 0x%08x (expected 0x%08x)",
                     status_result.value, DM_STATUS_READY);
        return SWD_ERROR_INVALID_STATE;
    }

    err = dap_write_dp(target, DP_SELECT, sel_bank0);
    if (err != SWD_OK) {
        return err;
    }

    SWD_INFO("Debug Module initialized successfully\n");

    target->rp2350.initialized = true;

    for (uint8_t i = 0; i < RP2350_NUM_HARTS; i++) {
        target->rp2350.harts[i].halt_state_known = false;
        target->rp2350.harts[i].halted = false;
        target->rp2350.harts[i].cache_valid = false;
    }

    rp2350_init_sba(target);

    return SWD_OK;
}

bool rp2350_is_initialized(const swd_target_t *target) {
    return target && target->rp2350.initialized;
}

swd_error_t rp2350_execute_progbuf(swd_target_t *target, uint8_t hart_id,
                                    const uint32_t *instructions, uint8_t count) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    return execute_progbuf_simple(target, hart_id, instructions, count);
}

swd_error_t rp2350_halt(swd_target_t *target, uint8_t hart_id) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    if (hart->halt_state_known && hart->halted) {
        SWD_DEBUG("Hart %u already halted\n", hart_id);
        return SWD_ERROR_ALREADY_HALTED;
    }

    SWD_INFO("Halting hart %u...\n", hart_id);

    uint32_t dmcontrol = encode_dmcontrol(hart_id, true, false, false);
    swd_error_t err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK) {
        return err;
    }

    err = poll_dmstatus_halted(target, hart_id, true);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to halt hart %u", hart_id);
        return err;
    }

    hart->halted = true;
    hart->halt_state_known = true;
    hart->cache_valid = false;

    SWD_INFO("Hart %u halted\n", hart_id);
    return SWD_OK;
}

swd_error_t rp2350_resume(swd_target_t *target, uint8_t hart_id) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    if (hart->halt_state_known && !hart->halted) {
        SWD_DEBUG("Hart %u already running\n", hart_id);
        return SWD_OK;
    }

    SWD_INFO("Resuming hart %u...\n", hart_id);

    uint32_t dmcontrol = encode_dmcontrol(hart_id, false, true, false);
    swd_error_t err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK) {
        return err;
    }

    err = poll_dmstatus_halted(target, hart_id, false);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to resume hart %u", hart_id);
        return err;
    }

    hart->halted = false;
    hart->halt_state_known = true;
    hart->cache_valid = false;

    SWD_INFO("Hart %u resumed\n", hart_id);
    return SWD_OK;
}

swd_error_t rp2350_step(swd_target_t *target, uint8_t hart_id) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    if (!hart->halted)
        return SWD_ERROR_NOT_HALTED;

    SWD_INFO("Single-stepping hart %u...\n", hart_id);

    swd_result_t dcsr_result = rp2350_read_csr(target, hart_id, DCSR_ADDR);
    if (dcsr_result.error != SWD_OK) {
        swd_set_error(target, dcsr_result.error, "Failed to read DCSR");
        return dcsr_result.error;
    }

    swd_error_t err = rp2350_write_csr(target, hart_id, DCSR_ADDR, dcsr_result.value | DCSR_STEP);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to write DCSR");
        return err;
    }

    uint32_t dmcontrol = encode_dmcontrol(hart_id, false, false, false);
    err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK)
        return err;

    dmcontrol = encode_dmcontrol(hart_id, false, true, false);
    err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK)
        return err;

    hart->halted = false;
    hart->halt_state_known = true;

    err = poll_dmstatus_halted(target, hart_id, true);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Step did not halt");
        return err;
    }

    hart->halted = true;
    hart->halt_state_known = true;
    hart->cache_valid = false;

    err = rp2350_write_csr(target, hart_id, DCSR_ADDR, dcsr_result.value);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to clear step bit");
    }

    SWD_INFO("Step completed\n");
    return err;
}

swd_error_t rp2350_reset(swd_target_t *target, uint8_t hart_id, bool halt_on_reset) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    SWD_INFO("Resetting hart %u (halt=%d)...\n", hart_id, halt_on_reset);

    uint32_t dmcontrol = encode_dmcontrol(hart_id, halt_on_reset, false, true);

    swd_error_t err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK) {
        return err;
    }

    sleep_ms(10);

    dmcontrol = encode_dmcontrol(hart_id, halt_on_reset, false, false);

    err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK) {
        return err;
    }

    sleep_ms(50);

    if (halt_on_reset) {
        err = poll_dmstatus_halted(target, hart_id, true);
        if (err != SWD_OK) {
            swd_set_error(target, err, "Failed to halt after reset");
            return err;
        }
        hart->halted = true;
        hart->halt_state_known = true;
        SWD_INFO("Hart %u reset and halted\n", hart_id);
    } else {
        hart->halted = false;
        hart->halt_state_known = true;
        SWD_INFO("Hart %u reset and running\n", hart_id);
    }

    hart->cache_valid = false;

    return SWD_OK;
}

bool rp2350_is_halted(swd_target_t *target, uint8_t hart_id) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return false;

    if (hart->halt_state_known)
        return hart->halted;

    uint32_t dmcontrol = encode_dmcontrol(hart_id, false, false, false);
    swd_error_t err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK)
        return false;

    swd_result_t result = dap_read_mem32(target, DM_DMSTATUS);
    if (result.error == SWD_OK) {
        bool halted = result.value & DMSTATUS_ALLHALTED;
        hart->halted = halted;
        hart->halt_state_known = true;
        return halted;
    }

    return false;
}

swd_result_t rp2350_read_reg(swd_target_t *target, uint8_t hart_id, uint8_t reg_num) {
    swd_result_t result = {.error = SWD_ERROR_NOT_INITIALIZED, .value = 0};

    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return result;

    if (!hart->halted) {
        result.error = SWD_ERROR_NOT_HALTED;
        swd_set_error(target, result.error, "Hart %u must be halted to read registers", hart_id);
        return result;
    }

    if (reg_num >= 32) {
        result.error = SWD_ERROR_INVALID_PARAM;
        swd_set_error(target, result.error, "Invalid register number: %u", reg_num);
        return result;
    }

    if (target->rp2350.cache_enabled && hart->cache_valid) {
        result.value = hart->cached_gprs[reg_num];
        result.error = SWD_OK;
        SWD_DEBUG("Read cached hart%u x%u = 0x%08lx\n", hart_id, (unsigned)reg_num, (unsigned long)result.value);
        return result;
    }

    SWD_DEBUG("Reading hart%u x%u...\n", hart_id, reg_num);

    uint32_t dmcontrol = encode_dmcontrol(hart_id, false, false, false);
    result.error = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (result.error != SWD_OK) {
        return result;
    }

    uint32_t command = (ABSCMD_GPR_BASE + reg_num) | ABSCMD_TRANSFER | ABSCMD_AARSIZE_32;

    result.error = dap_write_mem32(target, DM_COMMAND, command);
    if (result.error != SWD_OK) {
        return result;
    }

    result.error = wait_abstract_command(target);
    if (result.error != SWD_OK) {
        return result;
    }

    result = dap_read_mem32(target, DM_DATA0);
    if (result.error == SWD_OK) {
        if (target->rp2350.cache_enabled) {
            hart->cached_gprs[reg_num] = result.value;
        }
        SWD_INFO("hart%u x%u = 0x%08lx\n", hart_id, (unsigned)reg_num, (unsigned long)result.value);
    }

    return result;
}

swd_error_t rp2350_write_reg(swd_target_t *target, uint8_t hart_id, uint8_t reg_num, uint32_t value) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    if (!hart->halted) {
        swd_set_error(target, SWD_ERROR_NOT_HALTED,
                     "Hart %u must be halted to write registers", hart_id);
        return SWD_ERROR_NOT_HALTED;
    }

    if (reg_num >= 32) {
        swd_set_error(target, SWD_ERROR_INVALID_PARAM,
                     "Invalid register number: %u", reg_num);
        return SWD_ERROR_INVALID_PARAM;
    }

    SWD_INFO("Writing hart%u x%u = 0x%08lx\n", hart_id, (unsigned)reg_num, (unsigned long)value);

    uint32_t dmcontrol = encode_dmcontrol(hart_id, false, false, false);
    swd_error_t err = dap_write_mem32(target, DM_DMCONTROL, dmcontrol);
    if (err != SWD_OK) {
        return err;
    }

    err = dap_write_mem32(target, DM_DATA0, value);
    if (err != SWD_OK) {
        return err;
    }

    uint32_t command = (ABSCMD_GPR_BASE + reg_num) | ABSCMD_WRITE | ABSCMD_TRANSFER | ABSCMD_AARSIZE_32;

    err = dap_write_mem32(target, DM_COMMAND, command);
    if (err != SWD_OK) {
        return err;
    }

    err = wait_abstract_command(target);
    if (err == SWD_OK) {
        if (target->rp2350.cache_enabled) {
            hart->cached_gprs[reg_num] = value;
        }
    }

    return err;
}

swd_error_t rp2350_read_all_regs(swd_target_t *target, uint8_t hart_id, uint32_t regs[32]) {
    if (!regs)
        return SWD_ERROR_INVALID_PARAM;

    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    SWD_INFO("Reading all 32 registers from hart%u...\n", hart_id);

    for (uint8_t i = 0; i < 32; i++) {
        swd_result_t result = rp2350_read_reg(target, hart_id, i);
        if (result.error != SWD_OK) {
            return result.error;
        }
        regs[i] = result.value;
    }

    if (target->rp2350.cache_enabled) {
        hart->cache_valid = true;
    }

    return SWD_OK;
}

swd_result_t rp2350_read_pc(swd_target_t *target, uint8_t hart_id) {
    return rp2350_read_csr(target, hart_id, DPC_ADDR);
}

swd_error_t rp2350_write_pc(swd_target_t *target, uint8_t hart_id, uint32_t pc) {
    return rp2350_write_csr(target, hart_id, DPC_ADDR, pc);
}

swd_result_t rp2350_read_csr(swd_target_t *target, uint8_t hart_id, uint16_t csr_addr) {
    swd_result_t result = {.error = SWD_ERROR_NOT_INITIALIZED, .value = 0};

    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return result;

    if (!hart->halted) {
        result.error = SWD_ERROR_NOT_HALTED;
        return result;
    }

    const uint32_t progbuf[] = {
        RISCV_CSRR(8, csr_addr),
        RISCV_EBREAK
    };

    result.error = execute_with_saved_s0(target, hart_id, progbuf, 2, &result.value);
    return result;
}

swd_error_t rp2350_write_csr(swd_target_t *target, uint8_t hart_id, uint16_t csr_addr, uint32_t value) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    if (!hart->halted)
        return SWD_ERROR_NOT_HALTED;

    swd_result_t saved_s0 = rp2350_read_reg(target, hart_id, 8);
    if (saved_s0.error != SWD_OK)
        return saved_s0.error;

    swd_error_t err = rp2350_write_reg(target, hart_id, 8, value);
    if (err != SWD_OK) {
        rp2350_write_reg(target, hart_id, 8, saved_s0.value);
        return err;
    }

    const uint32_t progbuf[] = {
        RISCV_CSRW(csr_addr, 8),
        RISCV_EBREAK
    };

    err = execute_progbuf_simple(target, hart_id, progbuf, 2);

    rp2350_write_reg(target, hart_id, 8, saved_s0.value);

    return err;
}

void rp2350_invalidate_cache(swd_target_t *target, uint8_t hart_id) {
    hart_state_t *hart = get_hart(target, hart_id);
    if (hart)
        hart->cache_valid = false;
}

void rp2350_enable_cache(swd_target_t *target, bool enable) {
    if (target) {
        target->rp2350.cache_enabled = enable;
        if (!enable) {
            for (uint8_t i = 0; i < RP2350_NUM_HARTS; i++) {
                target->rp2350.harts[i].cache_valid = false;
            }
        }
    }
}

static swd_error_t rp2350_init_sba(swd_target_t *target) {
    SWD_INFO("Initializing System Bus Access...\n");

    swd_result_t result = dap_read_mem32(target, DM_SBCS);
    if (result.error != SWD_OK) {
        return result.error;
    }

    uint32_t sbasize = (result.value >> 5) & 0x7F;
    if (sbasize == 0) {
        SWD_WARN("SBA: No address width reported (sbasize=0)\n");
        return SWD_ERROR_INVALID_STATE;
    }

    uint32_t sberror = result.value & SBCS_SBERROR_MASK;
    if (sberror != 0) {
        dap_write_mem32(target, DM_SBCS, result.value | SBCS_SBERROR_MASK);
    }

    uint32_t sbcs = SBCS_SBACCESS_32 | SBCS_SBREADONADDR;

    swd_error_t err = dap_write_mem32(target, DM_SBCS, sbcs);
    if (err == SWD_OK) {
        target->rp2350.sba_initialized = true;
        SWD_INFO("SBA initialized\n");
    }

    return err;
}

swd_result_t rp2350_read_mem32(swd_target_t *target, uint32_t addr) {
    swd_result_t result = {.error = SWD_ERROR_NOT_INITIALIZED, .value = 0};

    if (!target || !target->rp2350.initialized)
        return result;

    if (addr & 0x3) {
        result.error = SWD_ERROR_ALIGNMENT;
        return result;
    }

    if (target->rp2350.sba_initialized) {
        result.error = dap_write_mem32(target, DM_SBADDRESS0, addr);
        if (result.error != SWD_OK)
            return result;

        result = dap_read_mem32(target, DM_SBDATA0);
    } else {
        result = dap_read_mem32(target, addr);
    }

    return result;
}

swd_error_t rp2350_write_mem32(swd_target_t *target, uint32_t addr, uint32_t value) {
    if (!target || !target->rp2350.initialized)
        return SWD_ERROR_NOT_INITIALIZED;

    if (addr & 0x3)
        return SWD_ERROR_ALIGNMENT;

    if (target->rp2350.sba_initialized) {
        swd_error_t err = dap_write_mem32(target, DM_SBADDRESS0, addr);
        if (err != SWD_OK)
            return err;

        return dap_write_mem32(target, DM_SBDATA0, value);
    } else {
        return dap_write_mem32(target, addr, value);
    }
}

swd_result_t rp2350_read_mem16(swd_target_t *target, uint32_t addr) {
    swd_result_t result = {.error = SWD_ERROR_NOT_INITIALIZED, .value = 0};

    if (!target)
        return result;

    if (addr & 0x1) {
        result.error = SWD_ERROR_ALIGNMENT;
        return result;
    }

    result = rp2350_read_mem32(target, addr & ~3);
    if (result.error != SWD_OK)
        return result;

    uint32_t offset = addr & 3;
    if (offset == 0) {
        result.value = result.value & 0xFFFF;
    } else {
        result.value = (result.value >> 16) & 0xFFFF;
    }

    return result;
}

swd_error_t rp2350_write_mem16(swd_target_t *target, uint32_t addr, uint16_t value) {
    if (!target)
        return SWD_ERROR_INVALID_PARAM;

    if (addr & 0x1)
        return SWD_ERROR_ALIGNMENT;

    uint32_t aligned_addr = addr & ~3;
    swd_result_t current = rp2350_read_mem32(target, aligned_addr);
    if (current.error != SWD_OK)
        return current.error;

    uint32_t offset = addr & 3;
    uint32_t new_value;
    if (offset == 0) {
        new_value = (current.value & 0xFFFF0000) | value;
    } else {
        new_value = (current.value & 0x0000FFFF) | ((uint32_t)value << 16);
    }

    return rp2350_write_mem32(target, aligned_addr, new_value);
}

swd_result_t rp2350_read_mem8(swd_target_t *target, uint32_t addr) {
    swd_result_t result = rp2350_read_mem32(target, addr & ~3);
    if (result.error == SWD_OK) {
        uint32_t offset = addr & 3;
        result.value = (result.value >> (offset * 8)) & 0xFF;
    }
    return result;
}

swd_error_t rp2350_write_mem8(swd_target_t *target, uint32_t addr, uint8_t value) {
    uint32_t aligned_addr = addr & ~3;
    swd_result_t current = rp2350_read_mem32(target, aligned_addr);
    if (current.error != SWD_OK)
        return current.error;

    uint32_t offset = addr & 3;
    uint32_t mask = ~(0xFF << (offset * 8));
    uint32_t new_value = (current.value & mask) | ((uint32_t)value << (offset * 8));

    return rp2350_write_mem32(target, aligned_addr, new_value);
}

static swd_error_t sba_check_error(swd_target_t *target) {
    swd_result_t sbcs = dap_read_mem32(target, DM_SBCS);
    if (sbcs.error != SWD_OK)
        return sbcs.error;

    if (sbcs.value & SBCS_SBERROR_MASK) {
        dap_write_mem32(target, DM_SBCS, sbcs.value | SBCS_SBERROR_MASK);
        swd_set_error(target, SWD_ERROR_BUS, "SBA bus error during block transfer");
        return SWD_ERROR_BUS;
    }

    return SWD_OK;
}

swd_error_t rp2350_read_mem_block(swd_target_t *target, uint32_t addr,
                                   uint32_t *buffer, uint32_t count) {
    if (!target || !buffer)
        return SWD_ERROR_INVALID_PARAM;

    if (count == 0)
        return SWD_OK;

    if (!target->rp2350.sba_initialized) {
        for (uint32_t i = 0; i < count; i++) {
            swd_result_t result = rp2350_read_mem32(target, addr + (i * 4));
            if (result.error != SWD_OK)
                return result.error;
            buffer[i] = result.value;
        }
        return SWD_OK;
    }

    swd_result_t saved_sbcs = dap_read_mem32(target, DM_SBCS);
    if (saved_sbcs.error != SWD_OK)
        return saved_sbcs.error;

    uint32_t sbcs = SBCS_SBAUTOINCREMENT | SBCS_SBACCESS_32 | SBCS_SBREADONADDR | SBCS_SBREADONDATA;
    swd_error_t err = dap_write_mem32(target, DM_SBCS, sbcs);
    if (err != SWD_OK)
        return err;

    err = dap_write_mem32(target, DM_SBADDRESS0, addr);
    if (err != SWD_OK)
        goto restore;

    for (uint32_t i = 0; i < count; i++) {
        swd_result_t r = dap_read_mem32(target, DM_SBDATA0);
        if (r.error != SWD_OK) {
            err = r.error;
            goto restore;
        }
        buffer[i] = r.value;
    }

    err = sba_check_error(target);

restore:
    dap_write_mem32(target, DM_SBCS, saved_sbcs.value);
    return err;
}

swd_error_t rp2350_write_mem_block(swd_target_t *target, uint32_t addr,
                                    const uint32_t *buffer, uint32_t count) {
    if (!target || !buffer)
        return SWD_ERROR_INVALID_PARAM;

    if (count == 0)
        return SWD_OK;

    if (!target->rp2350.sba_initialized) {
        for (uint32_t i = 0; i < count; i++) {
            swd_error_t err = rp2350_write_mem32(target, addr + (i * 4), buffer[i]);
            if (err != SWD_OK)
                return err;
        }
        return SWD_OK;
    }

    swd_result_t saved_sbcs = dap_read_mem32(target, DM_SBCS);
    if (saved_sbcs.error != SWD_OK)
        return saved_sbcs.error;

    uint32_t sbcs = SBCS_SBAUTOINCREMENT | SBCS_SBACCESS_32;
    swd_error_t err = dap_write_mem32(target, DM_SBCS, sbcs);
    if (err != SWD_OK)
        return err;

    err = dap_write_mem32(target, DM_SBADDRESS0, addr);
    if (err != SWD_OK)
        goto restore;

    for (uint32_t i = 0; i < count; i++) {
        err = dap_write_mem32(target, DM_SBDATA0, buffer[i]);
        if (err != SWD_OK)
            goto restore;
    }

    err = sba_check_error(target);

restore:
    dap_write_mem32(target, DM_SBCS, saved_sbcs.value);
    return err;
}

swd_error_t rp2350_upload_code(swd_target_t *target, uint32_t addr,
                                const uint32_t *code, uint32_t word_count) {
    if (!target || !code || word_count == 0)
        return SWD_ERROR_INVALID_PARAM;

    if (addr & 0x3)
        return SWD_ERROR_ALIGNMENT;

    SWD_INFO("Uploading %lu words to 0x%08lx...\n", (unsigned long)word_count, (unsigned long)addr);

    swd_error_t err = rp2350_write_mem_block(target, addr, code, word_count);
    if (err != SWD_OK)
        return err;

    uint32_t readback[word_count];
    err = rp2350_read_mem_block(target, addr, readback, word_count);
    if (err != SWD_OK)
        return err;

    if (memcmp(code, readback, word_count * sizeof(uint32_t)) != 0) {
        for (uint32_t i = 0; i < word_count; i++) {
            if (readback[i] != code[i]) {
                swd_set_error(target, SWD_ERROR_VERIFY,
                             "Verification failed at word %u: wrote 0x%08x, read 0x%08x",
                             i, code[i], readback[i]);
                return SWD_ERROR_VERIFY;
            }
        }
    }

    SWD_INFO("Code upload complete\n");
    return SWD_OK;
}

swd_error_t rp2350_execute_code(swd_target_t *target, uint8_t hart_id, uint32_t entry_point,
                                 const uint32_t *code, uint32_t word_count) {
    if (!target || !code)
        return SWD_ERROR_INVALID_PARAM;

    hart_state_t *hart = get_hart(target, hart_id);
    if (!hart)
        return SWD_ERROR_NOT_INITIALIZED;

    SWD_INFO("Executing code on hart%u at 0x%08lx (%lu words)...\n", hart_id, (unsigned long)entry_point, (unsigned long)word_count);

    swd_error_t err = rp2350_upload_code(target, entry_point, code, word_count);
    if (err != SWD_OK)
        return err;

    if (!hart->halted) {
        err = rp2350_halt(target, hart_id);
        if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED)
            return err;
    }

    err = rp2350_write_pc(target, hart_id, entry_point);
    if (err != SWD_OK)
        return err;

    swd_result_t pc = rp2350_read_pc(target, hart_id);
    if (pc.error != SWD_OK || pc.value != entry_point) {
        swd_set_error(target, SWD_ERROR_VERIFY,
                     "PC verification failed: expected 0x%08x, got 0x%08x",
                     entry_point, pc.value);
        return SWD_ERROR_VERIFY;
    }

    err = rp2350_resume(target, hart_id);
    if (err != SWD_OK)
        return err;

    SWD_INFO("Code execution started on hart%u\n", hart_id);
    return SWD_OK;
}

int rp2350_trace(swd_target_t *target, uint8_t hart_id, uint32_t max_instructions,
                 trace_callback_t callback, void *user_data,
                 bool capture_regs) {
    if (!target || !callback)
        return -SWD_ERROR_INVALID_PARAM;

    if (!target->rp2350.initialized)
        return -SWD_ERROR_NOT_INITIALIZED;

    if (hart_id >= RP2350_NUM_HARTS)
        return -SWD_ERROR_INVALID_PARAM;

    if (!target->rp2350.harts[hart_id].halted) {
        swd_error_t err = rp2350_halt(target, hart_id);
        if (err != SWD_OK && err != SWD_ERROR_ALREADY_HALTED)
            return -err;
    }

    uint32_t count = 0;
    bool unlimited = (max_instructions == 0);

    SWD_INFO("Starting instruction trace on hart%u (max=%lu, capture_regs=%d)...\n",
             hart_id, (unsigned long)max_instructions, capture_regs);

    while (unlimited || count < max_instructions) {
        trace_record_t record = {0};

        swd_result_t pc_result = rp2350_read_pc(target, hart_id);
        if (pc_result.error != SWD_OK) {
            SWD_INFO("Trace stopped: failed to read PC\n");
            return count > 0 ? (int)count : -pc_result.error;
        }
        record.pc = pc_result.value;

        swd_result_t inst_result = rp2350_read_mem32(target, record.pc);
        if (inst_result.error != SWD_OK) {
            SWD_INFO("Trace stopped: failed to read instruction at 0x%08lx\n", (unsigned long)record.pc);
            return count > 0 ? (int)count : -inst_result.error;
        }
        record.instruction = inst_result.value;

        if (capture_regs) {
            swd_error_t err = rp2350_read_all_regs(target, hart_id, record.regs);
            if (err != SWD_OK) {
                SWD_INFO("Trace stopped: failed to read registers\n");
                return count > 0 ? (int)count : -err;
            }
        }

        count++;

        if (!callback(&record, user_data)) {
            SWD_INFO("Trace stopped by callback after %lu instructions\n", (unsigned long)count);
            break;
        }

        swd_error_t err = rp2350_step(target, hart_id);
        if (err != SWD_OK) {
            SWD_INFO("Trace stopped: step failed\n");
            return count > 0 ? (int)count : -err;
        }
    }

    SWD_INFO("Trace completed: %lu instructions\n", (unsigned long)count);
    return (int)count;
}
