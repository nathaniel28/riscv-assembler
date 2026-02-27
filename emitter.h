#include <stdint.h>

#include "cc.h"

typedef struct {
	char *begin;
	size_t len;
} string;

enum assign_type {
	ASSIGN_UTYPE,
	ASSIGN_JTYPE,
};

typedef struct {
	int fix_idx; // offset into text section of instruction that needs the
		     // immediate
	enum assign_type assign;
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

int labels_add(labels *l, string key, uint32_t val);

int64_t label_get_or_add_waiter(labels *l, uint32_t *wait_dst, enum assign_type wait_assign);


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
		int len; // position of next availible byte in buffer
		int pos; // relative to the first byte written, ignoring clears
		int swap; // fd of file buffer
	} section[N_SECTIONS];
	int current_section;
} emitter;

// write the contents of a buffer to a file to make room for more stuff
void emitter_clear_buffer(emitter *em, int sect);

// buffer some data
// buffer as in "to buffer" instead of "a buffer"
void emitter_buffer(emitter *em, void *data, size_t len);

// make space, possibly with the help of a hole in the file buffer
void emitter_advance(emitter *em, size_t len);
