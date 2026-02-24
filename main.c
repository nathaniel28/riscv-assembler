#include <stdio.h>
#include <stdint.h>

#include <stdlib.h>

#include "ops.h"

/*

sections:
.data
.text

labels
instructions

*/

// read-only at runtime without great effort
typedef struct {
	uint64_t keys[2];
	uint64_t terms[2];
	int next;
	int data;
} trie;

// this trie is kinda strange: it keeps a bitset keys and terminals
// the index of the value of an ascii char is 1 iff that char is in the set
// to find the link to the next part of the trie, you offset a base trie pointer
// by next[x], and to find x you count up the number of set bits preceding the
// found key
// this function returns the offset from some base trie pointer of the next
// trie in the chain, or -1 to indicate there is no next part
int trie_next(trie *t, unsigned char c) {
	int ik = (c >> 6) & 1; // lo or hi part of keys
	int frag = c & 63; // bit index of keys[ik]
	if (((t->keys[ik] >> frag) & 1) == 0) {
		return -1;
	}
	int in = 0;
	// ensure popcountl is suitable for u64 (it probably is)
	static_assert(sizeof(unsigned long) == sizeof(uint64_t));
	if (ik) {
		in = __builtin_popcountl(t->keys[0]);
	}
	in += __builtin_popcountl(t->keys[ik] << (64 - frag));
	return t->next + in;
}

int trie_term(trie *t, unsigned char c) {
	int ik = (c >> 6) & 1; // lo or hi part of keys
	int frag = c & 63; // bit index of keys[ik]
	if (((t->terms[ik] >> frag) & 1) == 0) {
		return -1;
	}
	int in = 0;
	// ensure popcountl is suitable for u64 (it probably is)
	static_assert(sizeof(unsigned long) == sizeof(uint64_t));
	if (ik) {
		in = __builtin_popcountl(t->terms[0]);
	}
	in += __builtin_popcountl(t->terms[ik] << (64 - frag));
	return t->data + in;
}

void trie_set_key_(uint64_t keys[2], unsigned char c) {
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
			printf("%c", i);
		}
		x >>= 1;
	}
	x = keys[1];
	for (int i = 0; i < 64; i++) {
		if (x & 1) {
			printf("%c", i + 64);
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
	for (int i = 0; i < base->len; i++) {
		if (base->ents[i]->term) {
			dp++;
		}
		if (base->ents[i]->next->len > 0) {
			np++;
		}
	}
	gbuf[n].keys[0] = 0;
	gbuf[n].keys[1] = 0;
	gbuf[n].terms[0] = 0;
	gbuf[n].terms[1] = 0;
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
	apos += np + dp;
	np = gbuf[n].next;
	dp = gbuf[n].data;
	for (int i = 0; i < base->len; i++) {
		unsigned char k = base->ents[i]->key;
		if (base->ents[i]->term) {
			trie_set_key_(gbuf[n].terms, k);
			aux[dp++] = base->ents[i]->data;
		}
		if (base->ents[i]->next->len > 0) {
			trie_set_key_(gbuf[n].keys, k);
			gpos++;
			aux[np++] = gpos;
			gbuf_fill(base->ents[i]->next);
		}
	}

	/*
	printf("links: ");
	for (int i = 0; i < np; i++) {
		printf("%d ", gbuf[n].next[i]);
	}
	printf("@ %d: ", n);
	print_keys_(gbuf[n].keys);
	printf("/");
	print_keys_(gbuf[n].terms);
	printf("\n");
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

	printf("trie instr_tbase[] = {\n");
	for (int i = 0; i <= gpos; i++) {
		printf("\t{{%luL,%luL},{%luL,%luL},%d,%d},\n", gbuf[i].keys[0], gbuf[i].keys[1], gbuf[i].terms[0], gbuf[i].terms[1], gbuf[i].next, gbuf[i].data);
	}
	printf(
		"};\n"
		"\n"
		"int instr_tbase_auxiliary[] = {\n"
	);
	for (int i = 0; i < apos; i++) {
		printf("\t%d,\n", aux[i]);
	}
	printf("};\n");

	return 0;
}
