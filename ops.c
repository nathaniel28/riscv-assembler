#include <stdint.h>

#include "ops.h"

uint8_t formats[] = {
	[ADD] = R_TYPE,
	[SUB] = R_TYPE,
	[XOR] = R_TYPE,
	[OR] = R_TYPE,
	[AND] = R_TYPE,
	[SLL] = R_TYPE,
	[SRL] = R_TYPE,
	[SRA] = R_TYPE,
	[SLT] = R_TYPE,
	[SLTU] = R_TYPE,

	[ADDI] = I_TYPE,
	[XORI] = I_TYPE,
	[ORI] = I_TYPE,
	[ANDI] = I_TYPE,
	[SLLI] = I_TYPE,
	[SRLI] = I_TYPE,
	[SRAI] = I_TYPE,
	[SLTI] = I_TYPE,
	[SLTIU] = I_TYPE,

	[LB] = I_TYPE,
	[LH] = I_TYPE,
	[LW] = I_TYPE,
	[LBU] = I_TYPE,
	[LHU] = I_TYPE,

	[SB] = S_TYPE,
	[SH] = S_TYPE,
	[SW] = S_TYPE,

	[BEQ] = B_TYPE,
	[BNE] = B_TYPE,
	[BLT] = B_TYPE,
	[BGE] = B_TYPE,
	[BLTU] = B_TYPE,
	[BGEU] = B_TYPE,

	[JALR] = I_TYPE,

	[ECALL] = I_TYPE,
	[EBREAK] = I_TYPE,

	[JAL] = J_TYPE,

	[LUI] = U_TYPE,
	[AUIPC] = U_TYPE,
};
static_assert(sizeof formats / sizeof *formats == N_OPS);

uint8_t opcodes[] = {
	[ADD] = 0b0110011,
	[SUB] = 0b0110011,
	[XOR] = 0b0110011,
	[OR] = 0b0110011,
	[AND] = 0b0110011,
	[SLL] = 0b0110011,
	[SRL] = 0b0110011,
	[SRA] = 0b0110011,
	[SLT] = 0b0110011,
	[SLTU] = 0b0110011,

	[ADDI] = 0b0010011,
	[XORI] = 0b0010011,
	[ORI] = 0b0010011,
	[ANDI] = 0b0010011,
	[SLLI] = 0b0010011,
	[SRLI] = 0b0010011,
	[SRAI] = 0b0010011,
	[SLTI] = 0b0010011,
	[SLTIU] = 0b0010011,

	[LB] = 0b0000011,
	[LH] = 0b0000011,
	[LW] = 0b0000011,
	[LBU] = 0b0000011,
	[LHU] = 0b0000011,

	[SB] = 0b0100011,
	[SH] = 0b0100011,
	[SW] = 0b0100011,

	[BEQ] = 0b1100011,
	[BNE] = 0b1100011,
	[BLT] = 0b1100011,
	[BGE] = 0b1100011,
	[BLTU] = 0b1100011,
	[BGEU] = 0b1100011,

	[JALR] = 0b1100111,

	[ECALL] = 0b1110011,
	[EBREAK] = 0b1110011,

	[JAL] = 0b1101111,

	[LUI] = 0b0110111,
	[AUIPC] = 0b0010111,
};
static_assert(sizeof opcodes / sizeof *opcodes == N_OPS);

// not for U- or J-type
uint8_t func3s[] = {
	[ADD] = 0x0,
	[SUB] = 0x0,
	[XOR] = 0x4,
	[OR] = 0x6,
	[AND] = 0x7,
	[SLL] = 0x1,
	[SRL] = 0x5,
	[SRA] = 0x5,
	[SLT] = 0x2,
	[SLTU] = 0x3,

	[ADDI] = 0x0,
	[XORI] = 0x4,
	[ORI] = 0x6,
	[ANDI] = 0x7,
	[SLLI] = 0x1,
	[SRLI] = 0x5,
	[SRAI] = 0x5,
	[SLTI] = 0x2,
	[SLTIU] = 0x3,

	[LB] = 0x0,
	[LH] = 0x1,
	[LW] = 0x2,
	[LBU] = 0x4,
	[LHU] = 0x5,

	[SB] = 0x0,
	[SH] = 0x1,
	[SW] = 0x2,

	[BEQ] = 0x0,
	[BNE] = 0x1,
	[BLT] = 0x4,
	[BGE] = 0x5,
	[BLTU] = 0x6,
	[BGEU] = 0x7,

	[JALR] = 0x0,

	[ECALL] = 0x0,
	[EBREAK] = 0x0,

	//[JAL] = not used,

	//[LUI] = not used,
	//[AUIPC] = not used,
};

// only for R-type
uint8_t func7s[] = {
	[ADD] = 0x00,
	[SUB] = 0x20,
	[XOR] = 0x00,
	[OR] = 0x00,
	[AND] = 0x00,
	[SLL] = 0x00,
	[SRL] = 0x00,
	[SRA] = 0x20,
	[SLT] = 0x00,
	[SLTU] = 0x00,
};
