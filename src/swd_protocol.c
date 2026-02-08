#include "pico2-swd-riscv/swd.h"
#include "pico2-swd-riscv/dap.h"
#include "internal.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "swd.pio.h"
#include <stdio.h>

typedef enum {
    CMD_WRITE = 0,
    CMD_SKIP,
    CMD_TURNAROUND,
    CMD_READ
} pio_cmd_t;

static inline uint32_t encode_pio_command(swd_target_t *target, uint bit_count,
                                           bool out_en, pio_cmd_t cmd) {
    uint cmd_addr;
    switch (cmd) {
        case CMD_WRITE:      cmd_addr = target->pio.pio_offset + probe_offset_write_cmd; break;
        case CMD_SKIP:       cmd_addr = target->pio.pio_offset + probe_offset_get_next_cmd; break;
        case CMD_TURNAROUND: cmd_addr = target->pio.pio_offset + probe_offset_turnaround_cmd; break;
        case CMD_READ:       cmd_addr = target->pio.pio_offset + probe_offset_read_cmd; break;
        default:             cmd_addr = target->pio.pio_offset + probe_offset_get_next_cmd; break;
    }
    return ((bit_count - 1) & 0xff) | ((uint)out_en << 8) | (cmd_addr << 9);
}

static inline void pio_set_direction(swd_target_t *target, bool output) {
    pio_sm_put_blocking(target->pio.pio, target->pio.sm,
                       encode_pio_command(target, 0, output, CMD_SKIP));

    PIO pio = target->pio.pio;
    uint sm = target->pio.sm;
    pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    while (!(pio->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + sm))));
}

static inline void pio_write_bits(swd_target_t *target, uint bit_count, uint32_t data) {
    pio_sm_put_blocking(target->pio.pio, target->pio.sm,
                       encode_pio_command(target, bit_count, true, CMD_WRITE));
    pio_sm_put_blocking(target->pio.pio, target->pio.sm, data);
    SWD_DEBUG("  Write %d bits: 0x%08lx\n", bit_count, (unsigned long)data);
}

static inline uint32_t pio_read_bits(swd_target_t *target, uint bit_count) {
    pio_sm_put_blocking(target->pio.pio, target->pio.sm,
                       encode_pio_command(target, bit_count, false, CMD_READ));
    uint32_t data = pio_sm_get_blocking(target->pio.pio, target->pio.sm);
    uint32_t shifted = bit_count < 32 ? (data >> (32 - bit_count)) : data;
    SWD_DEBUG("  Read %d bits: 0x%08lx (raw: 0x%08lx)\n", bit_count, (unsigned long)shifted, (unsigned long)data);
    return shifted;
}

static inline void pio_turnaround(swd_target_t *target, uint cycles) {
    pio_sm_put_blocking(target->pio.pio, target->pio.sm,
                       encode_pio_command(target, cycles, false, CMD_TURNAROUND));
    pio_sm_put_blocking(target->pio.pio, target->pio.sm, 0);
}

uint8_t calculate_parity(uint32_t value) {
    return __builtin_popcount(value) & 1;
}

static uint8_t encode_swd_header(bool APnDP, bool RnW, uint8_t addr) {
    uint8_t a2 = (addr >> 2) & 1;
    uint8_t a3 = (addr >> 3) & 1;
    uint8_t parity = (APnDP + RnW + a2 + a3) & 1;

    uint8_t request = 0;
    request |= (1 << 0);
    request |= (APnDP << 1);
    request |= (RnW << 2);
    request |= (a2 << 3);
    request |= (a3 << 4);
    request |= (parity << 5);
    request |= (0 << 6);
    request |= (1 << 7);

    return request;
}

void swd_send_idle_clocks(swd_target_t *target, uint count) {
    SWD_DEBUG("Sending %d idle clocks\n", count);
    pio_set_direction(target, true);
    while (count >= 8) {
        pio_write_bits(target, 8, 0x00);
        count -= 8;
    }
    if (count)
        pio_write_bits(target, count, 0);
}

void swd_line_reset(swd_target_t *target) {
    SWD_DEBUG("Line reset (>50 ones)\n");
    pio_set_direction(target, true);
    for (uint i = 0; i < 7; i++)
        pio_write_bits(target, 8, 0xFF);
}

swd_error_t swd_io_raw(swd_target_t *target, uint8_t request, uint32_t *data, bool write) {
    if (!target || !target->pio.initialized) {
        return SWD_ERROR_INVALID_STATE;
    }

    pio_set_direction(target, true);
    pio_write_bits(target, 8, request);

    uint32_t ack_with_ta = pio_read_bits(target, SWD_TURNAROUND_CYCLES + 3);
    uint8_t ack = (ack_with_ta >> SWD_TURNAROUND_CYCLES) & 0x7;

    target->last_ack = ack;

    if (ack == SWD_ACK_OK) {
        if (write) {
            pio_turnaround(target, SWD_TURNAROUND_CYCLES);
            pio_write_bits(target, 32, *data);
            pio_write_bits(target, 1, calculate_parity(*data));
        } else {
            uint32_t value = pio_read_bits(target, 32);
            uint8_t parity = pio_read_bits(target, 1);

            if ((calculate_parity(value) ^ parity) != 0) {
                pio_turnaround(target, SWD_TURNAROUND_CYCLES);
                return SWD_ERROR_PARITY;
            }

            *data = value;
            pio_turnaround(target, SWD_TURNAROUND_CYCLES);
        }
        return SWD_OK;
    }

    if (ack == SWD_ACK_WAIT || ack == SWD_ACK_FAULT) {
        pio_turnaround(target, SWD_TURNAROUND_CYCLES);
        return swd_ack_to_error(ack);
    }

    if (ack == SWD_ACK_ERROR) {
        pio_read_bits(target, SWD_TURNAROUND_CYCLES + 33);
        swd_line_reset(target);
        return SWD_ERROR_PROTOCOL;
    }

    return SWD_ERROR_PROTOCOL;
}

static swd_error_t swd_transfer_with_retry(swd_target_t *target, bool ap,
                                            bool read, uint8_t reg, uint32_t *data) {
    uint8_t request = encode_swd_header(ap, read, reg);
    swd_error_t err = SWD_ERROR_WAIT;

    for (uint retry = 0; retry < target->dap.retry_count; retry++) {
        err = swd_io_raw(target, request, data, !read);
        if (err != SWD_ERROR_WAIT) break;
        sleep_us(100);
    }

    return err;
}

swd_error_t swd_read_dp_raw(swd_target_t *target, uint8_t reg, uint32_t *value) {
    return swd_transfer_with_retry(target, false, true, reg, value);
}

swd_error_t swd_write_dp_raw(swd_target_t *target, uint8_t reg, uint32_t value) {
    return swd_transfer_with_retry(target, false, false, reg, &value);
}

swd_error_t swd_read_ap_raw(swd_target_t *target, uint8_t reg, uint32_t *value) {
    return swd_transfer_with_retry(target, true, true, reg, value);
}

swd_error_t swd_write_ap_raw(swd_target_t *target, uint8_t reg, uint32_t value) {
    return swd_transfer_with_retry(target, true, false, reg, &value);
}

static swd_error_t init_pio(swd_target_t *target) {
    gpio_init(target->pio.pin_swclk);
    gpio_set_dir(target->pio.pin_swclk, GPIO_OUT);
    gpio_init(target->pio.pin_swdio);
    gpio_set_dir(target->pio.pin_swdio, GPIO_OUT);
    gpio_pull_up(target->pio.pin_swdio);

    gpio_set_function(target->pio.pin_swclk,
                     target->pio.pio == pio0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
    gpio_set_function(target->pio.pin_swdio,
                     target->pio.pio == pio0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);

    if (!pio_can_add_program(target->pio.pio, &probe_program)) {
        return SWD_ERROR_RESOURCE_BUSY;
    }

    target->pio.pio_offset = pio_add_program(target->pio.pio, &probe_program);

    pio_sm_config sm_config = probe_program_get_default_config(target->pio.pio_offset);

    sm_config_set_sideset_pins(&sm_config, target->pio.pin_swclk);
    sm_config_set_out_pins(&sm_config, target->pio.pin_swdio, 1);
    sm_config_set_set_pins(&sm_config, target->pio.pin_swdio, 1);
    sm_config_set_in_pins(&sm_config, target->pio.pin_swdio);

    pio_sm_set_consecutive_pindirs(target->pio.pio, target->pio.sm,
                                   target->pio.pin_swclk, 1, true);
    pio_sm_set_consecutive_pindirs(target->pio.pio, target->pio.sm,
                                   target->pio.pin_swdio, 1, true);

    sm_config_set_out_shift(&sm_config, true, false, 0);
    sm_config_set_in_shift(&sm_config, true, false, 0);

    pio_sm_init(target->pio.pio, target->pio.sm,
                target->pio.pio_offset, &sm_config);

    swd_set_frequency(target, target->pio.freq_khz);

    pio_sm_exec(target->pio.pio, target->pio.sm,
                target->pio.pio_offset + probe_offset_get_next_cmd);
    pio_sm_set_enabled(target->pio.pio, target->pio.sm, true);

    target->pio.initialized = true;
    return SWD_OK;
}

swd_error_t swd_set_frequency(swd_target_t *target, uint32_t freq_khz) {
    if (!target) {
        return SWD_ERROR_INVALID_PARAM;
    }

    uint32_t clk_sys_khz = clock_get_hz(clk_sys) / 1000;
    uint32_t divider = (((clk_sys_khz + freq_khz - 1) / freq_khz) + 3) / 4;

    if (divider < 1) divider = 1;
    if (divider > 65535) divider = 65535;

    pio_sm_set_clkdiv_int_frac(target->pio.pio, target->pio.sm, divider, 0);
    target->pio.freq_khz = freq_khz;

    SWD_INFO("Set SWCLK to %lu kHz (sysclk %lu kHz, div %lu)\n",
             (unsigned long)freq_khz, (unsigned long)clk_sys_khz, (unsigned long)divider);

    return SWD_OK;
}

swd_error_t swd_connect(swd_target_t *target) {
    if (!target) {
        return SWD_ERROR_INVALID_PARAM;
    }

    if (target->connected) {
        swd_set_error(target, SWD_ERROR_INVALID_STATE, "Already connected");
        return SWD_ERROR_INVALID_STATE;
    }

    SWD_INFO("Connecting to target...\n");

    swd_error_t err = init_pio(target);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to initialize PIO");
        return err;
    }

    static const uint8_t seq_jtag_to_dormant[] = {
        0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xbc,0xe3
    };
    static const uint8_t seq_dormant_to_swd[] = {
        0xff,
        0x92,0xf3,0x09,0x62,0x95,0x2d,0x85,0x86,
        0xe9,0xaf,0xdd,0xe3,0xa2,0x0e,0xbc,0x19,
        0xa0,0xf1,0xff,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff, 0x00
    };

    pio_set_direction(target, true);

    SWD_DEBUG("Sending JTAG->Dormant sequence\n");
    for (size_t i = 0; i < sizeof(seq_jtag_to_dormant); i++) {
        pio_write_bits(target, 8, seq_jtag_to_dormant[i]);
    }

    SWD_DEBUG("Sending Dormant->SWD sequence\n");
    for (size_t i = 0; i < sizeof(seq_dormant_to_swd); i++) {
        pio_write_bits(target, 8, seq_dormant_to_swd[i]);
    }

    swd_send_idle_clocks(target, SWD_IDLE_CYCLES);
    sleep_ms(1);

    uint32_t idcode = 0;
    err = swd_read_dp_raw(target, DP_IDCODE, &idcode);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to read IDCODE");
        return err;
    }

    if ((idcode & 0x0fffffff) == 0) {
        swd_set_error(target, SWD_ERROR_PROTOCOL, "Invalid IDCODE: 0x%08x", idcode);
        return SWD_ERROR_PROTOCOL;
    }

    target->idcode = idcode;
    SWD_INFO("Connected! IDCODE: 0x%08lx\n", (unsigned long)idcode);

    err = dap_power_up(target);
    if (err != SWD_OK) {
        swd_set_error(target, err, "Failed to power up debug domains");
        return err;
    }

    target->connected = true;
    return SWD_OK;
}

swd_error_t swd_disconnect(swd_target_t *target) {
    if (!target) {
        return SWD_ERROR_INVALID_PARAM;
    }

    if (!target->connected) {
        return SWD_OK;
    }

    SWD_INFO("Disconnecting from target...\n");

    dap_power_down(target);

    if (target->pio.initialized) {
        pio_sm_set_enabled(target->pio.pio, target->pio.sm, false);

        pio_remove_program(target->pio.pio, &probe_program, target->pio.pio_offset);

        gpio_deinit(target->pio.pin_swclk);
        gpio_disable_pulls(target->pio.pin_swclk);
        gpio_deinit(target->pio.pin_swdio);
        gpio_disable_pulls(target->pio.pin_swdio);

        target->pio.initialized = false;
    }

    target->connected = false;
    target->rp2350.initialized = false;
    target->dap.powered = false;

    SWD_INFO("Disconnected\n");
    return SWD_OK;
}
