#ifndef RV32I_DISASM_H
#define RV32I_DISASM_H

#include <stdint.h>
#include <stddef.h>

int rv32i_disasm(uint32_t instruction, uint32_t pc, char *buf, size_t buf_size);

#endif
