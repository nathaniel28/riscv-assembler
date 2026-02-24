#ifndef TRIE_H
#define TRIE_H

#include <stdint.h>

// read-only at runtime without great effort
// due to the size of the bitsets, this only works for ascii
// it won't exactly fail for strings with bytes containing values >127, but it
// ignores the top bit, so it treats 127 and 255 the same, for example
// this could work for anything if the bitsets held 256 bits each,
// and some modifications were made to trie_next_
typedef struct {
	uint64_t keys[2];
	uint64_t terms[2];
	int next; // offset into some auxiliary array
	int data; // ditto to above
} trie;

int trie_next_(uint64_t keys[2], int next, unsigned char c);

/*
usage:

int nextidx = trie_next(t, k);
if (nextidx > 0) {
	t = tbase + aux[nextidx];
}

where t is the current trie pointer,
tbase is the pointer to trie buffer of which t is a member of,
k is the next character,
and aux is the auxiliary array
*/
#define trie_next(t, c) trie_next_((t)->keys, (t)->next, c)
#define trie_term(t, c) trie_next_((t)->terms, (t)->data, c)

#endif
