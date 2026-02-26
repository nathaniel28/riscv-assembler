#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "trie.h"
#include "ops.h"
#include "directives.h"

#include "instruction_trie.h"

#define CC_NO_SHORT_NAMES
#include "cc.h"

const char *const no_mem = "memory allocation failed";

const char *const bad_write = "write call failed";

[[noreturn]] void panic(const char *const msg) {
	puts(msg);
	exit(-1);
}

enum {
	SECT_TEXT,
	SECT_DATA,
	N_SECTIONS,
} section;

typedef struct {
	uint8_t section_buf[N_SECTIONS][4096];
	struct {
		int len; // position of next availible byte in buffer
		int pos; // relative to the first byte written, ignoring flushes
		int swap;
	} section[N_SECTIONS];
	int current_section;
} emitter;

// write the contents of a buffer to a file to make room for more stuff
void emitter_clear_buffer(emitter *em, int sect) {
	if (write(em->section[sect].swap, em->section_buf[sect], em->section[sect].len) != em->section[sect].len)
		panic(bad_write);
	em->section[sect].len = 0;
}

// buffer some data
void emitter_buffer(emitter *em, void *data, size_t len) {
	// for fewer copies, we could check if len > the buffer size
	// and if it is, write directly from data instead of copying data
	// to the buffer and writing from there
	int sect = em->current_section;
	size_t pos = em->section[sect].len;
	while (pos + len >= sizeof em->section_buf[0]) {
		size_t copy = sizeof(em->section_buf[0]) - pos;
		memcpy(&em->section_buf[sect][pos], data, copy);
		emitter_clear_buffer(em, sect);
		data = (uint8_t *) data + copy;
		len -= copy;
		pos = 0;
	}
	memcpy(&em->section_buf[sect][pos], data, len);
	em->section[sect].len += len;
	em->section[sect].pos += len;
}

typedef struct {
	char *begin;
	size_t len;
} string;

/*
typedef struct {
	int fix_idx; // offset into text section of instruction that needs the
		     // immediate
	enum {
		ASSIGN_UTYPE,
		ASSIGN_JTYPE,
	} assign;
} label_waiter;

typedef struct {
	cc_vec(label_waiter) waiters;
	int64_t val; // negative if unassigned and there are waiters
} label;

#define CC_DTOR label, { if (val.val < 0) cc_cleanup(&val.waiters); }
#define CC_CMPR string, { return val_1.len != val_2.len || strncmp(val_1.begin, val_2.begin, val_1.len); }
#define CC_HASH string, { return cc_wyhash(val.begin, val.len); }
#include "cc.h"

typedef struct {
	cc_map(string, label) map;
} labels;

int labels_add(labels *l, string key, uint32_t val) {
	size_t old_sz = cc_size(&l->map);
	label nu;
	nu.val = val;
	label *e = cc_get_or_insert(&l->map, key, nu);
	if (!e)
		panic(no_mem);
	if (cc_size(&l->map) != old_sz)
		return 0; // inserted new label with val
	if (e->val >= 0)
		return -1; // that's a duplicate label
	e->val = val;
	// resolve each waiter
	cc_for_each(&e->waiters, waiter) {
		// TODO: set imm in the label_waiter
		// this will mean defining functions to set U-/J-type immediates
		// it also needs a way to get the u32 holding the instruction
	}
	cc_cleanup(&e->waiters);
	return 0;
}

int64_t label_get_or_add_waiter(labels *l, uint32_t *wait_dst, enum assign_type wait_assign) {
}
*/

int whitespace(char c) {
	return c == ' ' || c == '\t';
}

int newline(char c) {
	return c == '\n' || c == '\r';
}

int identifier(char c) {
	return (
		(c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9')
		|| c == '_'
	);
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

// unlike expect_literal, this consumes leading whitespace
// additionally, this assigns skipped whitespace to _s unconditionally, which is
// required to do something like
// if (expect_char_literal(&s, 'a') || *s != 'b' || *s != 'c')
// which checks for the absence of one of several chars
int expect_char_literal(char **_s, char c) {
	char *s = *_s;
	skip_whitespace(&s);
	*_s = s;
	if (*s++ != c)
		return -1;
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

// consumes leading whitespace and
// reg = "zero" | "ra" | "sp" | "gp" | "tp" | "fp" | ( "x" n0t31 )
//       | ( "t" n0t6 ) | ( "s" n0t11 ) | ( "a" n0t7 )
// where nXtY is "X" | "X + 1" | ... | "Y - 1" | "Y"
// permits 0X as an alternative to X: x09 equals x9, t02 equals t2, etc.
// writes the register number (0..31, inclusive) to r
int parse_reg(char **_s, uint32_t *r) {
	char *s = *_s;
	skip_whitespace(&s);
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
	case 'f':
		if (EXPECT_LITERAL(&s, "p"))
			return -1;
		*r = 8;
		break;
	default:
		return -1;
	}
	if (identifier(*s))
		return -1;
	*_s = s;
	return 0;
}

// consumes leading whitespace and an integer in hex/octal/decimal
int parse_imm(char **_s, long long *imm) {
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
	*_s = end;
	return 0;
}

int parse_identifier(char **_s, char **begin, char **end) {
	(void) _s;
	(void) begin;
	(void) end;
	// TODO
	return -1;
}

int parse_reg_reg_reg(char **_s, uint32_t *r0, uint32_t *r1, uint32_t *r2) {
	char *s = *_s;
	if (
		parse_reg(&s, r0)
		|| expect_char_literal(&s, ',')
		|| parse_reg(&s, r1)
		|| expect_char_literal(&s, ',')
		|| parse_reg(&s, r2)
	)
		return -1;
	*_s = s;
	return 0;
}

// this is only used for instructions with 11-bit immidiates
// caller should validate *imm is small enough to be used by their instruction
int parse_reg_reg_imm(char **_s, uint32_t *r0, uint32_t *r1, uint32_t *imm) {
	char *s = *_s;
	long long res;
	if (
		parse_reg(&s, r0)
		|| expect_char_literal(&s, ',')
		|| parse_reg(&s, r1)
		|| expect_char_literal(&s, ',')
		|| parse_imm(&s, &res)
		|| res >= 2048 || res < -2048
	)
		return -1;
	*imm = res; // TODO: validate demotion from (signed!) ll to u32
	*_s = s;
	return 0;
}

// this is only used for instructions with 11-bit immidiates
// the load/stores: lb, lh, lw, lbu, lhu, sb, sh, sw
int parse_ls_reg_imm_reg(char **_s, uint32_t *r0, uint32_t *imm, uint32_t *r1) {
	char *s = *_s;
	long long res;
	if (
		parse_reg(&s, r0)
		|| expect_char_literal(&s, ',')
		|| parse_imm(&s, &res)
		|| res >= 2048 || res < -2048
		|| expect_char_literal(&s, '(')
		|| parse_reg(&s, r1)
		|| expect_char_literal(&s, ')')
	)
		return -1;
	*imm = res; // TODO: validate demotion from (signed!) ll to u32
	*_s = s;
	return 0;
}

// *_s is *optionally* null-terminated
// *_s *must have* at least one '\n'
// returns NULL if no error occured
// otherwise, returns a string with a description of the error that may be
// presented to the user
char *parse_line(char **_s, emitter *em) {
	char *s = *_s;

	skip_whitespace(&s);
	char *rewind = s; // go back here to parse as another kind of line

	// try parsing as an instruction/known identifier
	trie *pos = &tbase[0];
	char here = *s++;
	for (;;) {
		char next = *s;
		if (!identifier(next)) {
			int termidx = trie_term(pos, here);
			if (termidx < 0)
				break; // it's not a string we know about

			int operation = tbase_auxiliary[termidx];

			long long ibuf;

			// check if it's a directive
			// TODO
			switch (operation) {
			case K_BYTE:
			case K_HALF:
			case K_WORD:
			case K_DWORD:
			case K_TEXT:
			case K_DATA:
				goto out_check_line;
			case K_ASCII:
				// TODO
				goto out_check_line;
			case K_SPACE:
				if (
					parse_imm(&s, &ibuf)
					|| ibuf >= 4294967296LL || ibuf < 0
				)
					return "immediate out of range";
				// TODO
				goto out_check_line;
			}

			// not a directive, must be an operation

			assert(operation < N_OPS && operation >= 0);

			uint32_t t0, t1, t2;

			uint32_t instr = opcodes[operation];
			switch (formats[operation]) {
			case R_TYPE:
				if (parse_reg_reg_reg(&s, &t0, &t1, &t2))
					return "could not parse required register, register, register for this operation";
				instr |= t0 << 7; // rd
				instr |= t1 << 15; // rs1
				instr |= t2 << 20; // rs2
				instr |= (uint32_t) func3s[operation] << 12;
				instr |= (uint32_t) func7s[operation] << 25;
				break;
			case I_TYPE:
				switch (operation) {
				case ECALL:
					t0 = 0, t1 = 0, t2 = 0;
					break;
				case EBREAK:
					t0 = 0, t1 = 0, t2 = 1;
					break;
				case LB:
				case LH:
				case LW:
				case LBU:
				case LHU:
					// note order of t0, t1, t2
					if (parse_ls_reg_imm_reg(&s, &t0, &t2, &t1))
						return "could not parse required register, immediate(register) required for this operation";
					break;
				default:
					if (parse_reg_reg_imm(&s, &t0, &t1, &t2))
						return "could not parse required register, register, immediate for this operation";
					break;
				}
				switch (operation) {
				case SRAI:
					instr |= 0x20 << 25;
					// fallthrough
				case SLLI:
				case SRLI:
					if (t2 > 31)
						return "immediate out of range for shift operation";
				}
				instr |= t0 << 7; // rd
				instr |= t1 << 15; // rs1
				instr |= t2 << 20; // imm
				instr |= (uint32_t) func3s[operation] << 12;
				break;
			case S_TYPE:
				if (parse_ls_reg_imm_reg(&s, &t0, &t1, &t2))
					return "could not parse required register, immediate(register) required for this operation";
				instr |= t2 << 15; // rs1
				instr |= t0 << 20; // rs2
				instr |= (t1 & 0xfe0) << 20; // imm[11:5]
				instr |= (t1 & 0x1f) << 7; // imm[4:0]
				instr |= (uint32_t) func3s[operation] << 12;
				break;
			case B_TYPE:
				// TODO
				assert(0);
			case U_TYPE:
				if (
					parse_reg(&s, &t0)
					|| expect_char_literal(&s, ',')
					|| parse_imm(&s, &ibuf)
					|| ibuf >= 1048576 || ibuf < 0
				)
					return "could not parse required register, immediate required for this operation";
				instr |= t0 << 7; // rd
				instr |= (uint32_t) ibuf << 12;
				break;
			case J_TYPE:
				// TODO
				assert(0);
			default:
				// default should never occur
				printf("error: invalid format buffer\n");
				assert(0);
			}
			emitter_buffer(em, &instr, sizeof instr);
			goto out_check_line;
		}
		int nextidx = trie_next(pos, here);
		if (nextidx < 0)
			break; // not a string we know about
		pos = tbase + tbase_auxiliary[nextidx];
		s++;
		here = next;
	}

	s = rewind;

	// not an instruction/section/data entry, better be a label
	char *id_start, *id_end;
	if (
		parse_identifier(&s, &id_start, &id_end)
		|| expect_char_literal(&s, ':')
	)
		return "could not parse as label";
	// TODO: do stuff with id_start and id_end
	// like putting the string in the label structure

out_check_line:
	skip_whitespace(&s);
	if (!newline(*s) && *s != '\0')
		return "extra tokens";

	*_s = s;
	return NULL;
}

/*
void assemble(char *input, size_t len, int dst_fd) {
	char *pos = input;
	while (pos < input + len) {
		char *err = parse_line(&pos);
		// TODO
	}
}
*/

int test_parse_line() {
	struct {
		char *in;
		uint32_t res;
		_Bool ok;
	} T[] = {
		{ " \t\t add \t  x0   ,\t  x1  \t,   x2  ", 0x00208033, 1 },
		{ "add x0, x1, x2", 0x00208033, 1 },
		{ "sub x3, x4, x5", 0x405201b3, 1 },
		{ "xor x6, x7, x8", 0x0083c333, 1 },
		{ "or x9, x10, x11", 0x00b564b3, 1 },
		{ "and x12, x13, x14", 0x00e6f633, 1 },
		{ "sll x15, x16, x17", 0x011817b3, 1 },
		{ "srl x18, x19, x20", 0x0149d933, 1 },
		{ "sra x21, x22, x23", 0x417b5ab3, 1 },
		{ "slt x24, x25, x26", 0x01acac33, 1 },
		{ "sltu x27, x28, x29", 0x01de3db3, 1 },
		{ "addi x30, x31, -2048", 0x800f8f13, 1 },
		{ "xori x14, x7, 2047", 0x7ff3c713, 1 },
		{ "ori x14, x7, -683", 0xd553e713, 1 },
		{ "andi x14, x7, 1365", 0x5553f713, 1 },
		{ "slli x14, x7, 31", 0x01f39713, 1 },
		{ "srli x14, x7, 0", 0x0003d713, 1 },
		{ "srai x14, x7, 15", 0x40f3d713, 1 },
		{ "slti x14, x7, 25", 0x0193a713, 1 },
		{ "sltiu x14, x7, -1", 0xfff3b713, 1 },
		{ "lb t1, 2047(t6)", 0x7fff8303, 1 },
		{ "lh t1, -2048(t6)", 0x800f9303, 1 },
		{ "lw t1, -683(t6)", 0xd55fa303, 1 },
		{ "lbu t1, 1365(t6)", 0x555fc303, 1 },
		{ "lhu t1, -1(t6)", 0xffffd303, 1 },
		{ "sb a2, -683(a7)", 0xd4c88aa3, 1 },
		{ "sh a2, -1(a7)", 0xfec89fa3, 1 },
		{ "sw a2, 1365(a7)", 0x54c8aaa3, 1 },
		{ "lui a1, 1048575", 0xfffff5b7, 1 },
		{ "auipc t6, 1048575", 0xffffff97, 1 },
		{ "ecall", 0x00000073, 1 },
		{ "ebreak", 0x00100073, 1 },
	};
	static emitter em;
	em.current_section = SECT_TEXT;
	for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
		em.section[em.current_section].pos = 0;
		em.section[em.current_section].len = 0;
		char *pos = T[i].in;
		char *err = parse_line(&pos, &em);
		if (err != NULL && T[i].ok) {
			printf("failed test %ld (%s): fail/success mismatch, reported %s\n", i, T[i].in, err);
			return 1;
		}
		uint32_t res;
		memcpy(&res, em.section_buf[em.current_section], sizeof res);
		if (T[i].res != res) {
			printf("failed test %ld (%s): expect\n%032b, got\n%032b\n", i, T[i].in, T[i].res, res);
			return 1;
		}
		if (T[i].ok && *pos != '\0') {
			printf("failed test %ld (%s): bad advancement (%s)\n", i, T[i].in, pos);
			return 1;
		}
	}
	return 0;
}

// NOTE: tests do not cover ensuring a register ending in a comma or whitespace
// is permissable (ie "x0," or "x0 ") since it's covered by test_parse_line
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
		{ "fp", 8, 1 },
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
	test_parse_line();
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
