#include <assert.h>
#include <errno.h>

#include "directives.h"
#include "emitter.h"
#include "instruction_trie.h"
#include "ops.h"
#include "parser.h"
#include "trie.h"

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

char *parse_string_literal(char **_s, emitter *em) {
	char *s = *_s;
	if (expect_char_literal(&s, '"'))
		return "expected start of string";
	for (;;) {
		char c = *s++;
		switch (c) {
		case '"':
			// keep things 4-byte aligned
			emitter_advance(em, (s - *_s) % 4);
			*_s = s;
			return NULL;
		case '\0':
		case '\n':
			return "unexpected end of string literal";
		case '\\':
			c = *s++;
			switch (c) {
			case '"':
			case '\\':
			case '\n':
				// no change to c
				break;
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case 't':
				c = '\t';
				break;
			case '\0':
				return "unexpected end of string";
			default:
				return "unknown escape character";
			}
			// fallthrough
		default:
			emitter_buffer(em, &c, sizeof c);
		}
	}
}

// usable with bytes <= 8 (and bytes > 0 of course),
// since a u64 is used as a buffer
char *parse_data_array(char **_s, emitter *em, int bytes) {
	char *s = *_s;
	long long ibuf;
	// NOTE: O rm mul
	// TODO: this assumes a long long is 64 bits, maybe static assert that
	long long x = 1LL << (8 * bytes);
	long long y = -(x / 2);
	size_t bytes_emitted = 0;
	for (;;) {
		if (
			parse_imm(&s, &ibuf)
			|| (
				bytes < 8
				&& (ibuf >= x || ibuf < y)
			)
		)
			return "immediate doesn't fit";
		// TODO: validate this works on a big endian machine
		uint64_t res = htole64(ibuf);
		emitter_buffer(em, &res, bytes);
		bytes_emitted += bytes;
		if (expect_char_literal(&s, ','))
			break;
	}
	// keep things 4-byte aligned
	emitter_advance(em, bytes_emitted % 4);
	*_s = s;
	return NULL;
}

// *_s is *optionally* null-terminated
// *_s *must have* at least one '\n'
// returns NULL if no error occured
// otherwise, returns a string with a description of the error that may be
// presented to the user
// may add data to the emitter's buffer even in the event of parsing failure
char *parse_line(char **_s, emitter *em) {
	char *s = *_s;

	char *err;

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
				err = parse_data_array(&s, em, 1);
				if (err != NULL)
					return err;
				goto out_check_line;
			case K_HALF:
				err = parse_data_array(&s, em, 2);
				if (err != NULL)
					return err;
				goto out_check_line;
			case K_WORD:
				err = parse_data_array(&s, em, 4);
				if (err != NULL)
					return err;
				goto out_check_line;
			case K_DWORD:
				err = parse_data_array(&s, em, 8);
				if (err != NULL)
					return err;
				goto out_check_line;
			case K_TEXT:
				em->current_section = SECT_TEXT;
				goto out_check_line;
			case K_DATA:
				em->current_section = SECT_DATA;
				goto out_check_line;
			case K_ASCII:
				err = parse_string_literal(&s, em);
				if (err != NULL)
					return err;
				goto out_check_line;
			case K_SPACE:
				if (
					parse_imm(&s, &ibuf)
					|| ibuf >= 4294967296LL || ibuf < 0
				)
					return "immediate out of range";
				emitter_advance(em, ibuf);
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
