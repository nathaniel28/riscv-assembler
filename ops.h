#ifndef OPS_H
#define OPS_H

enum {
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

	JAL,
	JALR,

	LUI,
	AUIPC,

	ECALL,
	EBREAK,
} ops;

#endif
