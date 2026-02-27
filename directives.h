#ifndef PARSE_H
#define PARSE_H

#include "ops.h"

enum directive {
	// to keep everything in the same trie, start after the op enum
	K_BYTE = N_OPS,
	K_HALF,
	K_WORD,
	K_DWORD,
	K_ASCII,
	K_SPACE,
	K_TEXT,
	K_DATA,

	N_DIRECTIVES,
};

#endif
