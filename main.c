#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "trie.h"
#include "ops.h"

#include "instruction_trie.h"

/*

sections:
.data
.text

labels
instructions

source = { line }
line = section | label | instruction "\n"
instruction = rtype | xitype | lstype | btype | utype | xjtype | x1type | x2type
rtype = rop reg "," reg "," reg
xitype = iop | jalr reg "," reg "," imm
lstype = lop | sop reg "," imm "(" reg ")"
btype = bop reg "," reg "," label
xjtype = jal reg "," label
x1type = lui | auipc reg "," imm
x2type = ebreak | ecall

*/

typedef struct {
	char *begin;
	int len;
} string;

typedef struct {
	union {
		uint32_t instruction;
		string label;
		string section;
	} u;
	enum {
		UTYPE_INSTRUCTION,
		UTYPE_LABEL,
		UTYPE_SECTION,
		UTYPE_ERROR,
	} type;
} unit;

int whitespace(char c) {
	return c == ' ' || c == '\t';
}

int newline(char c) {
	return c == '\n' || c == '\r';
}

void skip_whitespace(char **_s) {
	char *s = *_s;
	while (whitespace(*s)) {
		s++;
	}
	*_s = s;
}

int expect_literal(char **_s, const char *expect, size_t expect_len) {
	char *s = *_s;
	for (size_t i = 0; i < expect_len; i++) {
		if (*s != *expect)
			return -1;
		s++;
		expect++;
	}
	*_s = s;
	return 0;
}

// when expect is a c-string literal
#define EXPECT_LITERAL(_s, expect) expect_literal(_s, expect, sizeof(expect) - 1)

int parse_reg_long_number(char **_s, uint32_t *result) {
	char *s = *_s;
	char tens = *s++;
	if (tens < '0' || tens > '9')
		return -1;
	char ones = *s++;
	if (ones < '0' || ones > '9') {
		// maybe it's x0-x9, so only 1 digit
		s--;
		ones = tens;
		tens = '0';
	}
	*result = 10 * (tens - '0') + (ones - '0');
	*_s = s;
	return 0;
}

// consumes:
// reg = "zero" | "ra" | "sp" | "gp" | "tp" | ( "x" n0t31 ) | ( "t" n0t6 )
//       | ( "s" n0t11 ) | ( "a" n0t7 )
// where nXtY is "X" | "X + 1" | ... | "Y - 1" | "Y"
// permits 0X as an alternative to X: x09 equals x9, t02 equals t2, etc.
// writes the register number (0..31, inclusive) to r
int parse_reg(char **_s, uint32_t *r) {
	char *s = *_s;
	uint32_t res;
	switch (*s++) {
	case 'x':
		// x0-x31
		if (parse_reg_long_number(&s, &res) || res > 31)
			return -1;
		*r = res;
		break;
	case 'z':
		if (EXPECT_LITERAL(&s, "ero"))
			return -1;
		*r = 0;
		break;
	case 'r':
		if (EXPECT_LITERAL(&s, "a"))
			return -1;
		*r = 1;
		break;
	case 's':
		// s0-s11 or sa
		if (!EXPECT_LITERAL(&s, "p")) {
			*r = 2;
			break;
		}
		// s0-s11
		if (parse_reg_long_number(&s, &res) || res > 11)
			return -1;
		if (res < 2)
			res += 8;
		else
			res += 16;
		*r = res;
		break;
	case 'g':
		if (EXPECT_LITERAL(&s, "p"))
			return -1;
		*r = 3;
		break;
	case 't':
		// t0-t6 or tp
		if (!EXPECT_LITERAL(&s, "p")) {
			*r = 4;
			break;
		}
		// t0-t6
		// NOTE: I really should have some parse_reg_short_number
		// function, but the following should work fine anyway
		if (parse_reg_long_number(&s, &res) || res > 6)
			return -1;
		if (res < 3)
			res += 5;
		else
			res += 25;
		*r = res;
		break;
	case 'a':
		if (parse_reg_long_number(&s, &res) || res > 7)
			return -1;
		res += 10;
		*r = res;
		break;
	default:
		return -1;
	}
	if (!whitespace(*s) && !newline(*s) && *s != ',' && *s != '\0')
		return -1;
	*_s = s;
	return 0;
}

int parse_imm(char **_s, uint32_t *imm) {
	char *s = *_s;
	char *end;
	long long res = strtoll(s, &end, 0);
	if (
		s == end || (
			(
				res == 0 
				|| res == LLONG_MAX
				|| res == LLONG_MIN
			)
			&& errno != 0
		)
	)
		return -1;
	*imm = res;
	*_s = s;
	return 0;
}

int parse_reg_reg_reg(char **_s, uint32_t *r0, uint32_t *r1, uint32_t *r2) {
	char *s = *_s;
	if (parse_reg(&s, r0))
		return -1;

	skip_whitespace(&s);
	if (*s++ != ',')
		return -1;

	skip_whitespace(&s);
	if (parse_reg(&s, r1))
		return -1;

	skip_whitespace(&s);
	if (*s++ != ',')
		return -1;

	skip_whitespace(&s);
	if (parse_reg(&s, r2))
		return -1;

	*_s = s;
	return 0;
}

// caller should validate *imm is small enough to be used by their instruction
int parse_reg_reg_imm(char **_s, uint32_t *r0, uint32_t *r1, uint32_t *imm) {
	char *s = *_s;
	if (parse_reg(&s, r0))
		return -1;

	skip_whitespace(&s);
	if (*s++ != ',')
		return -1;

	skip_whitespace(&s);
	if (parse_reg(&s, r1))
		return -1;

	skip_whitespace(&s);
	if (*s++ != ',')
		return -1;

	skip_whitespace(&s);
	if (parse_imm(&s, imm))
		return -1;

	*_s = s;
	return 0;
}

/*
// *_s is *optionally* null-terminated
// *_s *must be* '\n'-terminated
unit parse_line(char **_s) {
}
*/

// NOTE: tests do not cover ensuring a register ending in a comma or whitespace
// is permissable (ie "x0," or "x0 ")
int test_parse_reg() {
	struct {
		char *in;
		uint32_t res;
		_Bool ok;
	} T[] = {
		{ "x0", 0, 1 },
		{ "x1", 1, 1 },
		{ "x2", 2, 1 },
		{ "x3", 3, 1 },
		{ "x4", 4, 1 },
		{ "x5", 5, 1 },
		{ "x6", 6, 1 },
		{ "x7", 7, 1 },
		{ "x8", 8, 1 },
		{ "x9", 9, 1 },
		{ "x10", 10, 1 },
		{ "x11", 11, 1 },
		{ "x12", 12, 1 },
		{ "x13", 13, 1 },
		{ "x14", 14, 1 },
		{ "x15", 15, 1 },
		{ "x16", 16, 1 },
		{ "x17", 17, 1 },
		{ "x18", 18, 1 },
		{ "x19", 19, 1 },
		{ "x20", 20, 1 },
		{ "x21", 21, 1 },
		{ "x22", 22, 1 },
		{ "x23", 23, 1 },
		{ "x24", 24, 1 },
		{ "x25", 25, 1 },
		{ "x26", 26, 1 },
		{ "x27", 27, 1 },
		{ "x28", 28, 1 },
		{ "x29", 29, 1 },
		{ "x30", 30, 1 },
		{ "x31", 31, 1 },
		{ "zero", 0, 1 },
		{ "ra", 1, 1 },
		{ "sp", 2, 1 },
		{ "gp", 3, 1 },
		{ "tp", 4, 1 },
		{ "t0", 5, 1 },
		{ "t1", 6, 1 },
		{ "t2", 7, 1 },
		{ "t3", 28, 1 },
		{ "t4", 29, 1 },
		{ "t5", 30, 1 },
		{ "t6", 31, 1 },
		{ "s0", 8, 1 },
		{ "s1", 9, 1 },
		{ "s2", 18, 1 },
		{ "s3", 19, 1 },
		{ "s4", 20, 1 },
		{ "s5", 21, 1 },
		{ "s6", 22, 1 },
		{ "s7", 23, 1 },
		{ "s8", 24, 1 },
		{ "s9", 25, 1 },
		{ "s10", 26, 1 },
		{ "s11", 27, 1 },
		{ "a0", 10, 1 },
		{ "a1", 11, 1 },
		{ "a2", 12, 1 },
		{ "a3", 13, 1 },
		{ "a4", 14, 1 },
		{ "a5", 15, 1 },
		{ "a6", 16, 1 },
		{ "a7", 17, 1 },
		{ "x32", 0, 0 },
		{ "x-1", 0, 0 },
		{ "x", 0, 0 },
		{ "t7", 0, 0 },
		{ "s12", 0, 0 },
		{ "a8", 0, 0 },
		{ "r0", 0, 0 },
		{ "x20a", 0, 0 },
		{ "zeroa", 0, 0 },
		{ "", 0, 0 },
	};
	for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
		char *pos = T[i].in;
		uint32_t reg;
		int err = parse_reg(&pos, &reg);
		if (err && T[i].ok) {
			printf("failed test %ld (%s): fail/success mismatch\n", i, T[i].in);
			return 1;
		}
		if (!err && T[i].res != reg) {
			printf("failed test %ld (%s): expect %d, got %d\n", i, T[i].in, T[i].res, reg);
			return 1;
		}
		if (T[i].ok && *pos != '\0') {
			printf("failed test %ld (%s): bad advancement\n", i, T[i].in);
			return 1;
		}
	}
	return 0;
}

int main() {
	test_parse_reg();
	/*
	char *test = (
		//".text\n"
		"addi s1, zero, x5\n"
	);
	char *pos = test;
	parse_line(&pos);
	*/
	return 0;
}
