#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "../trie.h"
#include "../ops.h"

void trie_set_key(uint64_t keys[2], unsigned char c) {
	int ik = (c >> 6) & 1;
	int frag = c & 63;
	keys[ik] |= ((uint64_t) 1) << frag;
}

typedef struct {
	struct trie_builder *next;
	int data;
	char key;
	char term;
} trie_builder_entry;

typedef struct trie_builder {
	trie_builder_entry **ents;
	int len;
} trie_builder;

void *calloc_(size_t sz) {
	void *res = calloc(1, sz);
	if (!res) {
		printf("out of memory\n");
		exit(1);
	}
	return res;
}

void *realloc_(void *ptr, size_t sz) {
	void *res = realloc(ptr, sz);
	if (!res) {
		printf("out of memory\n");
		exit(1);
	}
	return res;
}

int trie_builder_insert(trie_builder *base, const char *str, int data) {
	char c = *str;
	if (!c) {
		return 0;
	}
	for (int i = 0; i < base->len; i++) {
		if (base->ents[i]->key == c) {
			return trie_builder_insert(base->ents[i]->next, str + 1, data);
		}
	}
	trie_builder_entry *nu = calloc_(sizeof(trie_builder_entry));
	nu->key = c;
	nu->next = calloc_(sizeof(trie_builder));

	base->len++;
	base->ents = realloc_(base->ents, sizeof(trie_builder_entry *) * base->len);
	base->ents[base->len - 1] = nu;
	if (str[1]) {
		return trie_builder_insert(nu->next, str + 1, data) + 1;
	}
	nu->data = data;
	nu->term = 1;
	return 0;
}

void print_keys_(uint64_t keys[2]) {
	uint64_t x = keys[0];
	for (int i = 0; i < 64; i++) {
		if (x & 1) {
			fprintf(stderr, "%c", i);
		}
		x >>= 1;
	}
	x = keys[1];
	for (int i = 0; i < 64; i++) {
		if (x & 1) {
			fprintf(stderr, "%c", i + 64);
		}
		x >>= 1;
	}
}

trie *gbuf;
int *aux;
int apos = 0;
int gpos = 0;

void gbuf_size(trie_builder *base) {
	for (int i = 0; i < base->len; i++) {
		if (base->ents[i]->term) {
			apos++;
		}
		if (base->ents[i]->next->len > 0) {
			apos++;
			gpos++;
			gbuf_size(base->ents[i]->next);
		}
	}
}

void gbuf_fill(trie_builder *base) {
	int n = gpos;
	int np = 0;
	int dp = 0;
	gbuf[n].keys[0] = 0;
	gbuf[n].keys[1] = 0;
	gbuf[n].terms[0] = 0;
	gbuf[n].terms[1] = 0;
	for (int i = 0; i < base->len; i++) {
		unsigned char k = base->ents[i]->key;
		if (base->ents[i]->term) {
			trie_set_key(gbuf[n].terms, k);
			dp++;
		}
		if (base->ents[i]->next->len > 0) {
			trie_set_key(gbuf[n].keys, k);
			np++;
		}
	}
	if (np == 0) {
		gbuf[n].next = 0; // can really be anything
	} else {
		gbuf[n].next = apos;
	}
	if (dp == 0) {
		gbuf[n].data = 0; // can really be anything
	} else {
		gbuf[n].data = apos + np;
	}
	int npsv = np;
	int dpsv = dp;
	(void) npsv;
	(void) dpsv;
	apos += np + dp;
	np = gbuf[n].next;
	dp = gbuf[n].data;
	for (int i = 0; i < base->len; i++) {
		if (base->ents[i]->term) {
			unsigned char k = base->ents[i]->key;
			int ik = (k >> 6) & 1;
			int frag = k & 63;
			int in = 0;
			if (ik) {
				in = __builtin_popcountl(gbuf[n].terms[0]);
			}
			in += __builtin_popcountl(gbuf[n].terms[ik] << (64 - frag));
			aux[gbuf[n].data + in] = base->ents[i]->data;
		}
		if (base->ents[i]->next->len > 0) {
			unsigned char k = base->ents[i]->key;
			int ik = (k >> 6) & 1;
			int frag = k & 63;
			int in = 0;
			if (ik) {
				in = __builtin_popcountl(gbuf[n].keys[0]);
			}
			in += __builtin_popcountl(gbuf[n].keys[ik] << (64 - frag));
			gpos++;
			aux[gbuf[n].next + in] = gpos;
			gbuf_fill(base->ents[i]->next);
		}
	}

	/*
	fprintf(stderr, "links: ");
	for (int i = 0; i < npsv; i++) {
		fprintf(stderr, "%d ", aux[gbuf[n].next + i]);
	}
	fprintf(stderr, "@ %d: ", n);
	print_keys_(gbuf[n].keys);
	fprintf(stderr, "/");
	print_keys_(gbuf[n].terms);
	fprintf(stderr, " ");
	for (int i = 0; i < dpsv; i++) {
		switch (aux[gbuf[n].data + i]) {
#define C(n) case n: fprintf(stderr, "%s ", #n); break
		C(ADD);
		C(SUB);
		C(XOR);
		C(OR);
		C(AND);
		C(SLL);
		C(SRL);
		C(SRA);
		C(SLT);
		C(SLTU);
		C(ADDI);
		C(XORI);
		C(ORI);
		C(ANDI);
		C(SLLI);
		C(SRLI);
		C(SRAI);
		C(SLTI);
		C(SLTIU);
		C(LB);
		C(LH);
		C(LW);
		C(LBU);
		C(LHU);
		C(SB);
		C(SH);
		C(SW);
		C(BEQ);
		C(BNE);
		C(BLT);
		C(BGE);
		C(BLTU);
		C(BGEU);
		C(JAL);
		C(JALR);
		C(LUI);
		C(AUIPC);
		C(ECALL);
		C(EBREAK);
		}
	}
	fprintf(stderr, "\n");
	*/
}

int main() {
	static trie_builder base;
#define A(x, d) trie_builder_insert(&base, x, d)
	A("add", ADD);
	A("sub", SUB);
	A("xor", XOR);
	A("or", OR);
	A("and", AND);
	A("sll", SLL);
	A("srl", SRL);
	A("sra", SRA);
	A("slt", SLT);
	A("sltu", SLTU);

	A("addi", ADDI);
	A("xori", XORI);
	A("ori", ORI);
	A("andi", ANDI);
	A("slli", SLLI);
	A("srli", SRLI);
	A("srai", SRAI);
	A("slti", SLTI);
	A("sltiu", SLTIU);

	A("lb", LB);
	A("lh", LH);
	A("lw", LW);
	A("lbu", LBU);
	A("lhu", LHU);

	A("sb", SB);
	A("sh", SH);
	A("sw", SW);

	A("beq", BEQ);
	A("bne", BNE);
	A("blt", BLT);
	A("bge", BGE);
	A("bltu", BLTU);
	A("bgeu", BGEU);

	A("jal", JAL);
	A("jalr", JALR);

	A("lui", LUI);
	A("auipc", AUIPC);

	A("ecall", ECALL);
	A("ebreak", EBREAK);

	gpos = 0;
	apos = 0;
	gbuf_size(&base);
	gbuf = calloc_(sizeof(*gbuf) * (gpos + 1));
	aux = calloc_(sizeof(*aux) * apos);
	gpos = 0;
	apos = 0;
	gbuf_fill(&base);

	printf(
		"#ifndef INSTRUCTION_TRIE_H\n"
		"#define INSTRUCTION_TRIE_H\n"
		"\n"
		"// This code was auto-generated! Do not modify by hand.\n"
		"\n"
		"#include \"trie.h\"\n"
		"\n"
		"trie instr_tbase[] = {\n"
	);
	for (int i = 0; i <= gpos; i++) {
		printf("\t{{%luUL,%luUL},{%luUL,%luUL},%d,%d},\n", gbuf[i].keys[0], gbuf[i].keys[1], gbuf[i].terms[0], gbuf[i].terms[1], gbuf[i].next, gbuf[i].data);
	}
	printf(
		"};\n"
		"\n"
		"int instr_tbase_auxiliary[] = {\n"
	);
	for (int i = 0; i < apos; i++) {
		printf("\t%d,\n", aux[i]);
	}
	printf(
		"};\n"
		"\n"
		"#endif\n"
	);

	/*
	trie *tbase = gbuf;
	trie *t = &tbase[0];
	char *s = "add";
	char here = *s;
	if (!here) {
		return 0;
	}
	while (1) {
		printf("seek %c from @ %lu ", here, t - tbase);
		print_keys_(t->keys);
		printf("/");
		print_keys_(t->terms);
		printf("\n");
		char next = s[1];
		if (next == ' ' || next == '\t' || next == '\0') {
			int termidx = trie_term(t, here);
			if (termidx < 0) {
				printf("not found\n");
			} else {
				printf("data = %d\n", aux[termidx]);
			}
			break;
		}
		int nextidx = trie_next(t, here);
		if (nextidx < 0) {
			printf("not found (early)\n");
			break;
		}
		t = tbase + aux[nextidx];
		s++;
		here = next;
	}
	*/

	return 0;
}
