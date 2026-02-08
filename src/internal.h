#ifndef PICO2_SWD_RISCV_INTERNAL_H
#define PICO2_SWD_RISCV_INTERNAL_H

#include "pico2-swd-riscv/types.h"
#include "hardware/pio.h"

#ifndef PICO2_SWD_DEBUG_LEVEL
#define PICO2_SWD_DEBUG_LEVEL 1
#endif

#if PICO2_SWD_DEBUG_LEVEL >= 3
#define SWD_DEBUG(fmt, ...) printf("[SWD DEBUG] " fmt, ##__VA_ARGS__)
#else
#define SWD_DEBUG(fmt, ...) ((void)0)
#endif

#if PICO2_SWD_DEBUG_LEVEL >= 2
#define SWD_INFO(fmt, ...) printf("[SWD INFO] " fmt, ##__VA_ARGS__)
#else
#define SWD_INFO(fmt, ...) ((void)0)
#endif

#if PICO2_SWD_DEBUG_LEVEL >= 1
#define SWD_WARN(fmt, ...) printf("[SWD WARN] " fmt, ##__VA_ARGS__)
#else
#define SWD_WARN(fmt, ...) ((void)0)
#endif

#define SWD_TURNAROUND_CYCLES 1
#define SWD_IDLE_CYCLES 8

#define ERROR_DETAIL_SIZE 128

#define DM_DMCONTROL   (0x10 * 4)
#define DM_DMSTATUS    (0x11 * 4)
#define DM_ABSTRACTCS  (0x16 * 4)
#define DM_COMMAND     (0x17 * 4)
#define DM_DATA0       (0x04 * 4)
#define DM_PROGBUF0    (0x20 * 4)
#define DM_PROGBUF1    (0x21 * 4)
#define DM_SBCS        (0x38 * 4)
#define DM_SBADDRESS0  (0x39 * 4)
#define DM_SBDATA0     (0x3C * 4)

#define DMCONTROL_DMACTIVE          (1U << 0)
#define DMCONTROL_NDMRESET          (1U << 1)
#define DMCONTROL_HARTSELLO_SHIFT   16
#define DMCONTROL_RESUMEREQ         (1U << 30)
#define DMCONTROL_HALTREQ           (1U << 31)

#define DMSTATUS_ALLHALTED          (1U << 9)
#define DMSTATUS_ALLRUNNING         (1U << 11)

#define ABSTRACTCS_CMDERR_SHIFT     8
#define ABSTRACTCS_CMDERR_MASK      0x700
#define ABSTRACTCS_BUSY             (1U << 12)

#define ABSCMD_WRITE                (1U << 16)
#define ABSCMD_TRANSFER             (1U << 17)
#define ABSCMD_POSTEXEC             (1U << 18)
#define ABSCMD_AARSIZE_32           (2U << 20)
#define ABSCMD_GPR_BASE             0x1000
#define ABSCMD_CSR_BASE             0x0000

#define SBCS_SBREADONDATA           (1U << 15)
#define SBCS_SBAUTOINCREMENT        (1U << 16)
#define SBCS_SBACCESS_32            (2U << 17)
#define SBCS_SBREADONADDR           (1U << 20)
#define SBCS_SBERROR_MASK           (0x7U << 12)

#define DCSR_STEP                   (1U << 2)
#define DCSR_ADDR                   0x7b0
#define DPC_ADDR                    0x7b1

#define CSW_32BIT_AUTOINC           0xA2000002
#define DM_STATUS_READY             0x04010001

#define DM_DEACTIVATE               0x00000000
#define DM_ACTIVATE                 0x00000001
#define DM_FULL_CONFIG              0x07FFFFC1

typedef struct {
    uint8_t current_apsel;
    uint8_t current_bank;
    bool ctrlsel;
    uint32_t select_cache;
    bool powered;
    uint retry_count;
} dap_state_t;

#define RP2350_NUM_HARTS 2

typedef struct {
    bool halt_state_known;
    bool halted;
    bool cache_valid;
    uint32_t cached_pc;
    uint32_t cached_gprs[32];
    uint64_t cache_timestamp;
} hart_state_t;

typedef struct {
    bool initialized;
    bool sba_initialized;
    hart_state_t harts[RP2350_NUM_HARTS];
    bool cache_enabled;
} rp2350_state_t;

typedef struct {
    PIO pio;
    uint sm;
    uint pio_offset;
    uint pin_swclk;
    uint pin_swdio;
    uint freq_khz;
    bool initialized;
} pio_state_t;

struct swd_target {
    pio_state_t pio;
    bool connected;
    uint32_t idcode;
    dap_state_t dap;
    rp2350_state_t rp2350;
    swd_error_t last_error;
    uint8_t last_ack;
    char error_detail[ERROR_DETAIL_SIZE];
    bool resource_registered;
};

typedef struct {
    swd_target_t *pio0_sm_owners[4];
    swd_target_t *pio1_sm_owners[4];
    uint active_count;
} resource_tracker_t;

extern resource_tracker_t g_resources;

void swd_set_error(swd_target_t *target, swd_error_t error, const char *detail, ...);
swd_error_t swd_ack_to_error(uint8_t ack);

swd_error_t allocate_pio_sm(PIO *pio, uint *sm);
void release_pio_sm(PIO pio, uint sm);
bool register_target(swd_target_t *target, PIO pio, uint sm);
void unregister_target(swd_target_t *target);

swd_error_t swd_io_raw(swd_target_t *target, uint8_t request, uint32_t *data, bool write);
void swd_line_reset(swd_target_t *target);
void swd_send_idle_clocks(swd_target_t *target, uint count);

swd_error_t swd_read_dp_raw(swd_target_t *target, uint8_t reg, uint32_t *value);
swd_error_t swd_write_dp_raw(swd_target_t *target, uint8_t reg, uint32_t value);
swd_error_t swd_read_ap_raw(swd_target_t *target, uint8_t reg, uint32_t *value);
swd_error_t swd_write_ap_raw(swd_target_t *target, uint8_t reg, uint32_t value);

uint8_t calculate_parity(uint32_t value);
uint32_t encode_dp_select(uint8_t apsel, uint8_t bank, bool ctrlsel);

#endif
