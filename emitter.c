#include <elf.h>
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

/*
#define TEXT_VADDR 0x400000

void emitter_output_elf(emitter *em, int dst) {
	struct {
		Elf64_Ehdr ehdr;
		Elf64_Phdr text;
		Elf64_Phdr data;
	} header;
	header.ehdr.e_ident[EI_MAG0] = 0x7f;
	header.ehdr.e_ident[EI_MAG1] = 'E';
	header.ehdr.e_ident[EI_MAG2] = 'L';
	header.ehdr.e_ident[EI_MAG3] = 'F';
	header.ehdr.e_ident[EI_CLASS] = ELFCLASS64;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	header.ehdr.e_ident[EI_DATA] = ELFDATA2MSB;
#else
	header.ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
#endif
	header.ehdr.e_ident[EI_VERSION] = EV_CURRENT;
	header.ehdr.e_ident[EI_OSABI] = ELFOSABI_SYSV; // gcc uses this for me
	header.ehdr.e_ident[EI_ABIVERSION] = 0;
	header.ehdr.e_ident[EI_PAD] = 0; // unused padding
	header.ehdr.e_type = ET_EXEC;
	header.ehdr.e_machine = EM_RISCV; // now we're getting somewhere
	header.ehdr.e_version = EV_CURRENT;
	// TODO: lookup _start label, and add that to hdr.e_entry
	header.ehdr.e_entry = TEXT_VADDR;
	header.ehdr.e_phoff = 0x40; // right after the elf header
	header.ehdr.e_shoff = 0;
	header.ehdr.e_flags = 0;
	header.ehdr.e_ehsize = 64;
	header.ehdr.e_phentsize = 0;
	header.ehdr.e_phnum = 2; // TODO: verify
	header.ehdr.e_shentsize = 0x40; // TODO: verify
	header.ehdr.e_shnum = 0;
	header.ehdr.e_shstrndx 0;

	header.text.p_type = PT_LOAD;
	header.text.p_flags = PF_X | PF_R;
	header.text.p_vaddr = TEXT_VADDR;
	header.text.p_paddr = TEXT_VADDR; // likely unused
	header.text.p_filesz = em.section[SECT_TEXT].pos;
	header.text.p_memsz = em.section[SECT_TEXT].pos;
	header.text.p_align = 4;

	header.data.p_type = PT_LOAD;
	header.data.p_flags = PF_R | PF_W;
	//header.data.p_vaddr = ; // TODO
	//header.data.p_paddr = ; // TODO
	header.data.p_filesz = em.section[SECT_DATA].pos;
	header.data.p_memsz = em.section[SECT_DATA].pos;
	header.text.p_align = 4;
}
*/

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

