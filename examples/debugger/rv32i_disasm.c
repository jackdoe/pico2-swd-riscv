#include "rv32i_disasm.h"
#include <stdio.h>

static const char *reg_names[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10","s11","t3", "t4", "t5", "t6"
};

static const char *csr_name(uint16_t addr) {
    switch (addr) {
    case 0x300: return "mstatus";
    case 0x301: return "misa";
    case 0x304: return "mie";
    case 0x305: return "mtvec";
    case 0x340: return "mscratch";
    case 0x341: return "mepc";
    case 0x342: return "mcause";
    case 0x343: return "mtval";
    case 0x344: return "mip";
    case 0x7b0: return "dcsr";
    case 0x7b1: return "dpc";
    case 0x7b2: return "dscratch0";
    case 0xF11: return "mvendorid";
    case 0xF12: return "marchid";
    case 0xF13: return "mimpid";
    case 0xF14: return "mhartid";
    case 0xC00: return "cycle";
    case 0xC01: return "time";
    case 0xC02: return "instret";
    default:    return NULL;
    }
}

static int32_t sign_extend(uint32_t val, int bits) {
    uint32_t mask = 1U << (bits - 1);
    return (int32_t)((val ^ mask) - mask);
}

#define RD(inst)    (((inst) >> 7) & 0x1F)
#define RS1(inst)   (((inst) >> 15) & 0x1F)
#define RS2(inst)   (((inst) >> 20) & 0x1F)
#define FUNCT3(inst) (((inst) >> 12) & 0x7)
#define FUNCT7(inst) (((inst) >> 25) & 0x7F)

#define IMM_I(inst) sign_extend((inst) >> 20, 12)
#define IMM_S(inst) sign_extend((((inst) >> 25) << 5) | RD(inst), 12)
#define IMM_B(inst) sign_extend( \
    ((((inst) >> 31) & 1) << 12) | ((((inst) >> 7) & 1) << 11) | \
    ((((inst) >> 25) & 0x3F) << 5) | ((((inst) >> 8) & 0xF) << 1), 13)
#define IMM_U(inst) ((inst) & 0xFFFFF000)
#define IMM_J(inst) sign_extend( \
    ((((inst) >> 31) & 1) << 20) | ((((inst) >> 12) & 0xFF) << 12) | \
    ((((inst) >> 20) & 1) << 11) | ((((inst) >> 21) & 0x3FF) << 1), 21)

int rv32i_disasm(uint32_t inst, uint32_t pc, char *buf, size_t sz) {
    if ((inst & 0x3) != 0x3) {
        snprintf(buf, sz, "unknown (0x%04x)", inst & 0xFFFF);
        return 2;
    }

    uint8_t opcode = inst & 0x7F;
    uint8_t rd = RD(inst), rs1 = RS1(inst), rs2 = RS2(inst);
    uint8_t f3 = FUNCT3(inst), f7 = FUNCT7(inst);
    const char *rn = reg_names[rd], *r1 = reg_names[rs1], *r2 = reg_names[rs2];

    switch (opcode) {

    case 0x37:
        snprintf(buf, sz, "lui     %s, 0x%x", rn, IMM_U(inst) >> 12);
        return 4;

    case 0x17:
        snprintf(buf, sz, "auipc   %s, 0x%x", rn, IMM_U(inst) >> 12);
        return 4;

    case 0x6F: {
        int32_t off = IMM_J(inst);
        snprintf(buf, sz, "jal     %s, 0x%x", rn, pc + off);
        return 4;
    }

    case 0x67:
        if (f3 == 0) {
            snprintf(buf, sz, "jalr    %s, %d(%s)", rn, (int)IMM_I(inst), r1);
            return 4;
        }
        break;

    case 0x63: {
        static const char *bnames[] = {"beq","bne","?","?","blt","bge","bltu","bgeu"};
        if (f3 == 2 || f3 == 3) break;
        int32_t off = IMM_B(inst);
        snprintf(buf, sz, "%-8s%s, %s, 0x%x", bnames[f3], r1, r2, pc + off);
        return 4;
    }

    case 0x03: {
        static const char *lnames[] = {"lb","lh","lw","?","lbu","lhu","?","?"};
        if (f3 == 3 || f3 >= 6) break;
        snprintf(buf, sz, "%-8s%s, %d(%s)", lnames[f3], rn, (int)IMM_I(inst), r1);
        return 4;
    }

    case 0x23: {
        static const char *snames[] = {"sb","sh","sw"};
        if (f3 > 2) break;
        snprintf(buf, sz, "%-8s%s, %d(%s)", snames[f3], r2, (int)IMM_S(inst), r1);
        return 4;
    }

    case 0x13: {
        static const char *inames[] = {"addi","slti","sltiu","xori","ori","andi"};
        if (f3 <= 5 && f3 != 1 && f3 != 5) {
            snprintf(buf, sz, "%-8s%s, %s, %d", inames[f3], rn, r1, (int)IMM_I(inst));
            return 4;
        }
        if (f3 == 1 && f7 == 0) {
            snprintf(buf, sz, "slli    %s, %s, %d", rn, r1, rs2);
            return 4;
        }
        if (f3 == 5) {
            snprintf(buf, sz, "%s %s, %s, %d", f7 ? "srai   " : "srli   ", rn, r1, rs2);
            return 4;
        }
        break;
    }

    case 0x33: {
        static const char *rnames[2][8] = {
            {"add","sll","slt","sltu","xor","srl","or","and"},
            {"sub","?","?","?","?","sra","?","?"}
        };
        uint8_t alt = (f7 == 0x20) ? 1 : 0;
        if (f7 != 0 && f7 != 0x20) break;
        if (alt && f3 != 0 && f3 != 5) break;
        snprintf(buf, sz, "%-8s%s, %s, %s", rnames[alt][f3], rn, r1, r2);
        return 4;
    }

    case 0x0F:
        if (f3 == 0) { snprintf(buf, sz, "fence"); return 4; }
        if (f3 == 1) { snprintf(buf, sz, "fence.i"); return 4; }
        break;

    case 0x73:
        if (inst == 0x00000073) { snprintf(buf, sz, "ecall"); return 4; }
        if (inst == 0x00100073) { snprintf(buf, sz, "ebreak"); return 4; }
        if (inst == 0x30200073) { snprintf(buf, sz, "mret"); return 4; }
        if (inst == 0x10200073) { snprintf(buf, sz, "sret"); return 4; }
        if (inst == 0x10500073) { snprintf(buf, sz, "wfi"); return 4; }
        if (f3 >= 1 && f3 <= 3) {
            static const char *csrops[] = {"?","csrrw","csrrs","csrrc"};
            uint16_t csr = inst >> 20;
            const char *cn = csr_name(csr);
            if (cn)
                snprintf(buf, sz, "%-8s%s, %s, %s", csrops[f3], rn, cn, r1);
            else
                snprintf(buf, sz, "%-8s%s, 0x%03x, %s", csrops[f3], rn, csr, r1);
            return 4;
        }
        if (f3 >= 5 && f3 <= 7) {
            static const char *csriops[] = {"?","?","?","?","?","csrrwi","csrrsi","csrrci"};
            uint16_t csr = inst >> 20;
            const char *cn = csr_name(csr);
            if (cn)
                snprintf(buf, sz, "%-8s%s, %s, %d", csriops[f3], rn, cn, rs1);
            else
                snprintf(buf, sz, "%-8s%s, 0x%03x, %d", csriops[f3], rn, csr, rs1);
            return 4;
        }
        break;
    }

    snprintf(buf, sz, "unknown (0x%08x)", inst);
    return 4;
}
