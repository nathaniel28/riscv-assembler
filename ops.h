#ifndef OPS_H
#define OPS_H

#include <stdint.h>

enum format {
	R_TYPE,
	I_TYPE,
	S_TYPE,
	B_TYPE,
	U_TYPE,
	J_TYPE,
};

enum op {
	ADD,
	SUB,
	XOR,
	OR,
	AND,
	SLL,
	SRL,
	SRA,
	SLT,
	SLTU,

	ADDI,
	XORI,
	ORI,
	ANDI,
	SLLI,
	SRLI,
	SRAI,
	SLTI,
	SLTIU,

	LB,
	LH,
	LW,
	LBU,
	LHU,

	SB,
	SH,
	SW,

	BEQ,
	BNE,
	BLT,
	BGE,
	BLTU,
	BGEU,

	JALR,

	ECALL,
	EBREAK,

	JAL,

	LUI,
	AUIPC,

	N_OPS
};

extern uint8_t formats[];

extern uint8_t opcodes[];

extern uint8_t func3s[];

// only for R-type
extern uint8_t func7s[];

#endif
