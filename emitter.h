#ifndef EMITTER_H
#define EMITTER_H

#include <stdint.h>

#include "cc.h"

typedef struct {
	char *begin;
	size_t len;
} string;

enum assign_type {
	ASSIGN_BTYPE,
	ASSIGN_JTYPE,
};

typedef struct {
	int64_t fix_idx; // offset into text section of instruction
			 // that needs the immediate
	int section;
	enum assign_type assign;
} label_waiter;

typedef struct {
	cc_vec(label_waiter) waiters;
	int64_t val; // negative if unassigned (meaning there are waiters)
} label;

#define CC_DTOR label, { if (val.val < 0) cc_cleanup(&val.waiters); }
#define CC_CMPR string, { return val_1.len != val_2.len || strncmp(val_1.begin, val_2.begin, val_1.len); }
#define CC_HASH string, { return cc_wyhash(val.begin, val.len); }
#include "cc.h"

typedef struct {
	cc_map(string, label) map;
} labels;

enum section {
	SECT_TEXT,
	SECT_DATA,
	N_SECTIONS,
};

// the goal of an emitter is to store data in seperate places for all sections
// (currently .text or .data) because since the program being assembled need
// not list the sections in the "correct" order, or may swap between the same
// sections more than once, all the assembled instructions/data needs to be
// buffered
typedef struct {
	uint8_t section_buf[N_SECTIONS][4096];
	struct {
		uint64_t vaddr;
		uint64_t len; // position of next availible byte in buffer
		uint64_t pos; // relative to the first byte ever written
		int swap; // fd of file buffer
	} section[N_SECTIONS];
	labels labels;
	int current_section;
} emitter;

// buffer some data
// buffer as in "to buffer" instead of "a buffer"
extern void emitter_buffer(emitter *em, void *data, size_t len);

// make space, possibly with the help of a hole in the file buffer
extern void emitter_advance(emitter *em, size_t len);

extern int emitter_label_add(emitter *em, string key);

extern int64_t emitter_label_get_or_add_waiter(emitter *em, string key, enum assign_type wait_assign);

#endif
