#include <linux/elf.h>
#include <unistd.h>

#include "emitter.h"

const char *const no_mem = "memory allocation failed";

[[noreturn]] void panic(const char *const msg) {
	puts(msg);
	exit(-1);
}

void emitter_clear_buffer(emitter *em, int sect) {
	if (write(em->section[sect].swap, em->section_buf[sect], em->section[sect].len) != em->section[sect].len)
		panic("write call failed");
	em->section[sect].len = 0;
}

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

void emitter_advance(emitter *em, size_t len) {
	// TODO validate this
	int sect = em->current_section;
	size_t pos = em->section[sect].len;
	em->section[sect].pos += len;
	if (pos + len >= sizeof em->section_buf[0]) {
		emitter_clear_buffer(em, sect);
		if (lseek(em->section[sect].swap, len, SEEK_CUR) == -1)
			panic("lseek call failed");
	} else {
		bzero(&em->section_buf[sect][pos], len);
		em->section[sect].len += len;
	}
}

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
	// TODO
	(void) l;
	(void) wait_dst;
	(void) wait_assign;
	return 0;
}

