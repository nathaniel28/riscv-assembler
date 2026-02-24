#include "trie.h"

int trie_next_(uint64_t keys[2], int next, unsigned char c) {
	int ik = (c >> 6) & 1; // lo or hi part of keys
	int frag = c & 63; // bit index of keys[ik]
	if (((keys[ik] >> frag) & 1) == 0) {
		return -1;
	}
	int in = 0;
	// ensure popcountl is suitable for u64 (it probably is)
	static_assert(sizeof(unsigned long) == sizeof(uint64_t));
	if (ik) {
		in = __builtin_popcountl(keys[0]);
	}
	in += __builtin_popcountl(keys[ik] << (64 - frag));
	return next + in;
}
