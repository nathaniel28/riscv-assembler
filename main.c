
#include "parser.h"
#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "directives.h"
#include "emitter.h"
#include "instruction_trie.h"
#include "ops.h"

#include "trie.h"

int test_parse_line() {
	struct {
		char *in;
		void *res;
		size_t len;
		_Bool ok;
	} T[] = {
#define U(in, u32) { in, (uint32_t []) { u32 }, sizeof(uint32_t), 1 }
		U(" \t\t add \t  x0   ,\t  x1  \t,   x2  ", 0x00208033),
		U("add x0,x1,x2", 0x00208033),
		U("add x0, x1, x2", 0x00208033),
		U("sub x3, x4, x5", 0x405201b3),
		U("xor x6, x7, x8", 0x0083c333),
		U("or x9, x10, x11", 0x00b564b3),
		U("and x12, x13, x14", 0x00e6f633),
		U("sll x15, x16, x17", 0x011817b3),
		U("srl x18, x19, x20", 0x0149d933),
		U("sra x21, x22, x23", 0x417b5ab3),
		U("slt x24, x25, x26", 0x01acac33),
		U("sltu x27, x28, x29", 0x01de3db3),
		U("addi x30, x31, -2048", 0x800f8f13),
		U("xori x14, x7, 2047", 0x7ff3c713),
		U("ori x14, x7, -683", 0xd553e713),
		U("andi x14, x7, 1365", 0x5553f713),
		U("slli x14, x7, 31", 0x01f39713),
		U("srli x14, x7, 0", 0x0003d713),
		U("srai x14, x7, 15", 0x40f3d713),
		U("slti x14, x7, 25", 0x0193a713),
		U("sltiu x14, x7, -1", 0xfff3b713),
		U("lb t1, 2047(t6)", 0x7fff8303),
		U("lh t1, -2048(t6)", 0x800f9303),
		U("lw t1, -683(t6)", 0xd55fa303),
		U("lbu t1, 1365(t6)", 0x555fc303),
		U("lhu t1, -1(t6)", 0xffffd303),
		U("sb a2, -683(a7)", 0xd4c88aa3),
		U("sh a2, -1(a7)", 0xfec89fa3),
		U("sw a2, 1365(a7)", 0x54c8aaa3),
		U("lui a1, 1048575", 0xfffff5b7),
		U("auipc t6, 1048575", 0xffffff97),
		U("ecall", 0x00000073),
		U("ebreak", 0x00100073),
#undef U
#define U(in) { in, NULL, 0, 0 }
		U(""),
		U("addi x0, x0, -2049"),
		U("addi x0, x0, 2048"),
		U("lb t0, 2048(t0)"),
		U("lh t0, -2049(t0)"),
		U("slli x0, x0, -1"),
		U("srai x0, x0, 32"),
		U("\n"),
		U("and"),
		U("and x99, x0, x0"),
		U("and x0, x0"),
		U("lh t1, 0, t6"),
		U("a x0, x0, x0"),
		U("add x0, x0, x0 add x0, x0, x0"),
#undef U
#define U(in, ...) { in, __VA_ARGS__, sizeof(__VA_ARGS__) / sizeof((__VA_ARGS__)[0]), 1 }
		U(".ascii \"~!@#$%^&*()_+`-=[]{}|;':,./<>?\\\\\\\"\\b\\f\\n\\r\\tabcdefghijklmnopqrstuvwxyz\"", (char []) {126, 33, 64, 35, 36, 37, 94, 38, 42, 40, 41, 95, 43, 96, 45, 61, 91, 93, 123, 125, 124, 59, 39, 58, 44, 46, 47, 60, 62, 63, 92, 34, 8, 12, 10, 13, 9, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122}),
		U(".byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14", (uint8_t []) {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}),
		U(".byte 255", (uint8_t []) {255}),
		U(".half 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14", (uint16_t []) {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}),
		U(".half 65535", (uint16_t []) {65535}),
		U(".word 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14", (uint32_t []) {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}),
		U(".word 4294967295", (uint32_t []) {4294967295}),
		U(".dword 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14", (uint64_t []) {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}),
		U(".dword -1", (uint64_t []) {18446744073709551615UL}),
		//U(".dword 18446744073709551615", (uint64_t []) {18446744073709551615UL}),
		// NOTE: about above test: strtoll is signed and won't accept
		// the above test, so I'd need to write my own number parser
		// which isn't the end of the world but I don't want to now
#undef U
	};
	static emitter em;
	em.current_section = SECT_TEXT;
	for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
		// note that it's fine to write more than one of the section
		// buffer's size to an emitter, but the extra data will be
		// stored elsewhere which means the memcmp won't work
		// thus we make this assertion
		assert(T[i].len <= sizeof em.section_buf[0]);
		em.section[em.current_section].pos = 0;
		em.section[em.current_section].len = 0;
		char *pos = T[i].in;
		char *err = parse_line(&pos, &em);
		if ((err != NULL) == T[i].ok) {
			printf("failed test %ld (%s): fail/success mismatch, reported %s\n", i, T[i].in, err ? err : "no errors");
			return 1;
		}
		if (!err) {
			if (memcmp(em.section_buf[em.current_section], T[i].res, T[i].len)) {
				printf("failed test %ld (%s)", i, T[i].in);
				if (T[i].len == sizeof(uint32_t)) {
					uint32_t want, got;
					memcpy(&want, T[i].res, sizeof want);
					memcpy(&got, em.section_buf[em.current_section], sizeof got);
					printf(": expect\n%032b, got\n%032b", want, got);
				}
				printf("\n");
				return 1;
			}
			if (T[i].ok && *pos != '\0') {
				printf("failed test %ld (%s): bad advancement (%s)\n", i, T[i].in, pos);
				return 1;
			}
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
		if ((err != 0) == T[i].ok) {
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
	test_parse_line();
	return 0;
}
