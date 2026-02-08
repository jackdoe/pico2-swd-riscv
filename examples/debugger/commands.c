#include "commands.h"
#include "rv32i_disasm.h"
#include <pico2-swd-riscv/swd.h>
#include <pico2-swd-riscv/rp2350.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *abi_names[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10","s11","t3", "t4", "t5", "t6"
};

static int reg_by_name(const char *name) {
    if (name[0] == 'x') {
        int n = atoi(name + 1);
        if (n >= 0 && n < 32) return n;
    }
    for (int i = 0; i < 32; i++)
        if (strcmp(name, abi_names[i]) == 0) return i;
    if (strcmp(name, "fp") == 0) return 8;
    return -1;
}

static uint32_t parse_num(const char *s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return strtoul(s + 2, NULL, 16);
    return strtoul(s, NULL, 16);
}

static swd_result_t read_instruction(swd_target_t *target, uint32_t addr) {
    uint32_t aligned = addr & ~3u;
    swd_result_t r = rp2350_read_mem32(target, aligned);
    if (r.error != SWD_OK) return r;

    if (addr & 2) {
        uint16_t hw = (r.value >> 16) & 0xFFFF;
        if ((hw & 3) != 3) {
            r.value = hw;
        } else {
            swd_result_t r2 = rp2350_read_mem32(target, aligned + 4);
            if (r2.error != SWD_OK) return r2;
            r.value = ((r2.value & 0xFFFF) << 16) | hw;
        }
    }
    return r;
}

static void print_error(swd_error_t err, swd_target_t *target) {
    printf("error: %s\n", swd_error_string(err));
    const char *detail = swd_get_last_error_detail(target);
    if (detail && detail[0])
        printf("  %s\n", detail);
}

#define CHECK(expr) do { swd_error_t _e = (expr); if (_e != SWD_OK) { print_error(_e, target); return; } } while(0)
#define CHECK_RESULT(r) do { if ((r).error != SWD_OK) { print_error((r).error, target); return; } } while(0)

static void cmd_help(swd_target_t *target, uint8_t *hart, int argc, char **argv);

static void cmd_info(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)hart; (void)argc; (void)argv;
    printf("%s\n", swd_get_target_info(target));
    printf("freq: %lu kHz\n", (unsigned long)swd_get_frequency(target));
    swd_result_t id = swd_read_idcode(target);
    if (id.error == SWD_OK)
        printf("IDCODE: 0x%08lx\n", (unsigned long)id.value);
    for (uint8_t h = 0; h < 2; h++)
        printf("hart%u: %s\n", h, rp2350_is_halted(target, h) ? "halted" : "running");
}

static void cmd_hart(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)target;
    if (argc < 2) {
        printf("active hart: %u\n", *hart);
        return;
    }
    uint8_t h = atoi(argv[1]);
    if (h > 1) { printf("invalid hart (0 or 1)\n"); return; }
    *hart = h;
    printf("switched to hart%u\n", h);
}

static void cmd_halt(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)argc; (void)argv;
    swd_error_t err = rp2350_halt(target, *hart);
    if (err == SWD_ERROR_ALREADY_HALTED)
        printf("hart%u already halted\n", *hart);
    else if (err != SWD_OK)
        print_error(err, target);
    else
        printf("hart%u halted\n", *hart);
}

static void cmd_run(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)argc; (void)argv;
    CHECK(rp2350_resume(target, *hart));
    printf("hart%u running\n", *hart);
}

static void cmd_step(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    uint32_t n = (argc >= 2) ? parse_num(argv[1]) : 1;
    for (uint32_t i = 0; i < n; i++) {
        CHECK(rp2350_step(target, *hart));
        swd_result_t pc = rp2350_read_pc(target, *hart);
        CHECK_RESULT(pc);
        swd_result_t inst = read_instruction(target, pc.value);
        if (inst.error == SWD_OK) {
            char dis[64];
            int len = rv32i_disasm(inst.value, pc.value, dis, sizeof(dis));
            if (len == 2)
                printf("%08x: %04x      %s\n", pc.value, inst.value & 0xFFFF, dis);
            else
                printf("%08x: %08lx  %s\n", pc.value, (unsigned long)inst.value, dis);
        } else {
            printf("PC=0x%08x\n", pc.value);
        }
    }
}

static void cmd_reset(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    bool halt = (argc >= 2 && strcmp(argv[1], "halt") == 0);
    CHECK(rp2350_reset(target, *hart, halt));
    printf("hart%u reset%s\n", *hart, halt ? " and halted" : "");
}

static void cmd_reg(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    if (argc >= 2) {
        int r = reg_by_name(argv[1]);
        if (r < 0) { printf("unknown register: %s\n", argv[1]); return; }
        swd_result_t v = rp2350_read_reg(target, *hart, r);
        CHECK_RESULT(v);
        printf("x%d/%s = 0x%08lx\n", r, abi_names[r], (unsigned long)v.value);
        return;
    }

    swd_result_t pc = rp2350_read_pc(target, *hart);
    CHECK_RESULT(pc);
    printf("hart%u [%s] PC=0x%08x\n", *hart,
           rp2350_is_halted(target, *hart) ? "halted" : "running", pc.value);

    uint32_t regs[32];
    CHECK(rp2350_read_all_regs(target, *hart, regs));

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 4; col++) {
            int i = row + col * 8;
            printf("%sx%d/%-4s=%08lx", col ? " " : " ", i, abi_names[i],
                   (unsigned long)regs[i]);
        }
        printf("\n");
    }
}

static void cmd_wreg(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    if (argc < 3) { printf("usage: wreg <reg> <value>\n"); return; }
    int r = reg_by_name(argv[1]);
    if (r < 0) { printf("unknown register: %s\n", argv[1]); return; }
    CHECK(rp2350_write_reg(target, *hart, r, parse_num(argv[2])));
    printf("x%d/%s <- 0x%08lx\n", r, abi_names[r], (unsigned long)parse_num(argv[2]));
}

static void cmd_pc(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    if (argc >= 2) {
        uint32_t val = parse_num(argv[1]);
        CHECK(rp2350_write_pc(target, *hart, val));
        printf("PC <- 0x%08lx\n", (unsigned long)val);
        return;
    }
    swd_result_t pc = rp2350_read_pc(target, *hart);
    CHECK_RESULT(pc);
    printf("PC = 0x%08x\n", pc.value);
}

static void cmd_csr(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    if (argc < 2) { printf("usage: csr <addr>\n"); return; }
    uint16_t addr = parse_num(argv[1]);
    swd_result_t v = rp2350_read_csr(target, *hart, addr);
    CHECK_RESULT(v);
    printf("CSR 0x%03x = 0x%08lx\n", addr, (unsigned long)v.value);
}

static void cmd_wcsr(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    if (argc < 3) { printf("usage: wcsr <addr> <value>\n"); return; }
    uint16_t addr = parse_num(argv[1]);
    uint32_t val = parse_num(argv[2]);
    CHECK(rp2350_write_csr(target, *hart, addr, val));
    printf("CSR 0x%03x <- 0x%08lx\n", addr, (unsigned long)val);
}

static void cmd_mem(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)hart;
    if (argc < 2) { printf("usage: mem <addr> [count]\n"); return; }
    uint32_t addr = parse_num(argv[1]) & ~3;
    uint32_t bytes = (argc >= 3) ? parse_num(argv[2]) : 64;
    uint32_t words = (bytes + 3) / 4;

    for (uint32_t i = 0; i < words; i += 4) {
        uint32_t line_addr = addr + i * 4;
        uint32_t line[4] = {0};
        uint32_t n = (words - i < 4) ? (words - i) : 4;

        for (uint32_t j = 0; j < n; j++) {
            swd_result_t r = rp2350_read_mem32(target, line_addr + j * 4);
            if (r.error != SWD_OK) { print_error(r.error, target); return; }
            line[j] = r.value;
        }

        printf("%08x:", line_addr);
        for (uint32_t j = 0; j < n; j++)
            printf(" %08lx", (unsigned long)line[j]);
        for (uint32_t j = n; j < 4; j++)
            printf("         ");
        printf("  ");
        for (uint32_t j = 0; j < n * 4; j++) {
            uint8_t b = (line[j / 4] >> ((j % 4) * 8)) & 0xFF;
            printf("%c", (b >= 0x20 && b < 0x7F) ? b : '.');
        }
        printf("\n");
    }
}

static void cmd_wmem(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)hart;
    if (argc < 3) { printf("usage: wmem <addr> <value> [8|16|32]\n"); return; }
    uint32_t addr = parse_num(argv[1]);
    uint32_t val = parse_num(argv[2]);
    uint32_t width = (argc >= 4) ? parse_num(argv[3]) : 32;

    switch (width) {
    case 8:  CHECK(rp2350_write_mem8(target, addr, val)); break;
    case 16: CHECK(rp2350_write_mem16(target, addr, val)); break;
    case 32: CHECK(rp2350_write_mem32(target, addr, val)); break;
    default: printf("width must be 8, 16, or 32\n"); return;
    }
    printf("[0x%08lx] <- 0x%lx (%lu-bit)\n",
           (unsigned long)addr, (unsigned long)val, (unsigned long)width);
}

static void cmd_dis(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    uint32_t addr, count = 16;
    if (argc >= 2) {
        addr = parse_num(argv[1]);
        if (argc >= 3) count = parse_num(argv[2]);
    } else {
        swd_result_t pc = rp2350_read_pc(target, *hart);
        CHECK_RESULT(pc);
        addr = pc.value;
    }

    for (uint32_t i = 0; i < count; i++) {
        swd_result_t inst = read_instruction(target, addr);
        CHECK_RESULT(inst);
        char dis[64];
        int len = rv32i_disasm(inst.value, addr, dis, sizeof(dis));
        if (len == 2)
            printf("%08x: %04x      %s\n", addr, inst.value & 0xFFFF, dis);
        else
            printf("%08x: %08lx  %s\n", addr, (unsigned long)inst.value, dis);
        addr += len;
    }
}

typedef struct {
    uint32_t index;
} trace_ctx_t;

static bool trace_cb(const trace_record_t *rec, void *user_data) {
    trace_ctx_t *ctx = user_data;
    char dis[64];
    rv32i_disasm(rec->instruction, rec->pc, dis, sizeof(dis));
    printf("[%3lu] %08x: %08lx  %s\n",
           (unsigned long)ctx->index, rec->pc,
           (unsigned long)rec->instruction, dis);
    ctx->index++;
    return true;
}

static void cmd_trace(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    uint32_t n = (argc >= 2) ? parse_num(argv[1]) : 10;
    trace_ctx_t ctx = {0};
    int traced = rp2350_trace(target, *hart, n, trace_cb, &ctx, false);
    if (traced < 0)
        print_error(-traced, target);
    else
        printf("traced %d instructions\n", traced);
}

static void cmd_upload(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    if (argc < 3) { printf("usage: exec <addr> <hex word>...\n"); return; }
    uint32_t addr = parse_num(argv[1]);
    uint32_t count = argc - 2;
    uint32_t code[64];
    if (count > 64) { printf("max 64 words\n"); return; }
    for (uint32_t i = 0; i < count; i++)
        code[i] = parse_num(argv[i + 2]);
    CHECK(rp2350_execute_code(target, *hart, addr, code, count));
    printf("uploaded and executing at 0x%08lx (%lu words)\n",
           (unsigned long)addr, (unsigned long)count);
}

static void cmd_fill(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)hart;
    if (argc < 4) { printf("usage: fill <addr> <count> <pattern>\n"); return; }
    uint32_t addr = parse_num(argv[1]);
    uint32_t count = parse_num(argv[2]);
    uint32_t pattern = parse_num(argv[3]);
    for (uint32_t i = 0; i < count; i++) {
        CHECK(rp2350_write_mem32(target, addr + i * 4, pattern));
    }
    printf("filled %lu words at 0x%08lx with 0x%08lx\n",
           (unsigned long)count, (unsigned long)addr, (unsigned long)pattern);
}

static void cmd_freq(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)hart;
    if (argc >= 2) {
        uint32_t khz = parse_num(argv[1]);
        CHECK(swd_set_frequency(target, khz));
        printf("frequency set to %lu kHz\n", (unsigned long)khz);
    } else {
        printf("frequency: %lu kHz\n", (unsigned long)swd_get_frequency(target));
    }
}

static const command_t command_table[] = {
    {"help",  "help [cmd]",                   cmd_help},
    {"info",  "info",                          cmd_info},
    {"hart",  "hart [0|1]",                    cmd_hart},
    {"halt",  "halt",                          cmd_halt},
    {"run",   "run",                           cmd_run},
    {"step",  "step [N]",                      cmd_step},
    {"reset", "reset [halt]",                  cmd_reset},
    {"reg",   "reg [name|num]",                cmd_reg},
    {"wreg",  "wreg <reg> <value>",            cmd_wreg},
    {"pc",    "pc [value]",                    cmd_pc},
    {"csr",   "csr <addr>",                    cmd_csr},
    {"wcsr",  "wcsr <addr> <value>",           cmd_wcsr},
    {"mem",   "mem <addr> [count]",            cmd_mem},
    {"wmem",  "wmem <addr> <val> [8|16|32]",   cmd_wmem},
    {"dis",   "dis [addr] [count]",            cmd_dis},
    {"trace", "trace [N]",                     cmd_trace},
    {"exec",  "exec <addr> <hex word>...",     cmd_upload},
    {"fill",  "fill <addr> <count> <pattern>", cmd_fill},
    {"freq",  "freq [khz]",                    cmd_freq},
};

const int command_count = sizeof(command_table) / sizeof(command_table[0]);

static void cmd_help(swd_target_t *target, uint8_t *hart, int argc, char **argv) {
    (void)target; (void)hart;
    if (argc >= 2) {
        for (int i = 0; i < command_count; i++) {
            if (strcmp(argv[1], command_table[i].name) == 0) {
                printf("%s\n", command_table[i].usage);
                return;
            }
        }
        printf("unknown command: %s\n", argv[1]);
        return;
    }
    for (int i = 0; i < command_count; i++)
        printf("  %-8s %s\n", command_table[i].name, command_table[i].usage);
}

const command_t *debugger_commands = command_table;
