#include <assert.h>
#include <elf.h>
#include <limits.h>
#include <string.h>
#include <sys/param.h>
#define _GNU_SOURCE
// TODO: why do I have to define __USE_GNU?! I want copy_file_range, and the
// man pages say I get that with just _GNU_SOURCE and the file offset thing
#define __USE_GNU
#define _FILE_OFFSET_BITS 64
#include <unistd.h>

#include "emitter.h"
#include "parser.h"

const char *const no_mem = "memory allocation failed";

[[noreturn]] void panic(const char *const msg) {
	puts(msg);
	exit(-1);
}

// write the contents of a buffer to a file to make room for more stuff
void emitter_clear_buffer(emitter *em, int sect) {
	ssize_t written = write(em->section[sect].swap, em->section_buf[sect], em->section[sect].len);
	if (written < 0 || (uint64_t) written != em->section[sect].len)
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

int emitter_label_add(emitter *em, string key) {
	size_t old_sz = cc_size(&em->labels);
	label nu;
	nu.val = em->section[em->current_section].pos + em->section[em->current_section].vaddr;
	label *e = cc_get_or_insert(&em->labels, key, nu);
	if (!e)
		panic(no_mem);
	if (cc_size(&em->labels) != old_sz)
		return 0; // inserted new label with val
	if (e->val >= 0)
		return -1; // that's a duplicate label
	// resolve each waiter
	cc_for_each(&e->waiters, waiter) {
		int64_t offset = nu.val - (waiter->fix_idx + em->section[waiter->section].vaddr);
		int64_t len = em->section[waiter->section].len;
		int64_t pos = em->section[waiter->section].pos;
		if (pos - len > waiter->fix_idx) {
			// TODO TODO TODO
			//read();
			//write();
			panic("unimplimented");
		} else {
			uint32_t instr;
			assert((int64_t) sizeof instr + len - (pos - waiter->fix_idx) <= len);
			uint8_t *target = &em->section_buf[waiter->section][len - (pos - waiter->fix_idx)];
			memcpy(&instr, target, sizeof instr);
			switch (waiter->assign) {
			case ASSIGN_BTYPE:
				// TODO: verify the immediate fits in b-type
				// immediate field, but take care to allow
				// negative values
				set_btype_imm(&instr, offset);
				break;
			case ASSIGN_JTYPE:
				// TODO: verify the immediate fits in j-type
				// immediate field, but take care to allow
				// negative values
				set_jtype_imm(&instr, offset);
				break;
			default:
				// should never occur
				assert(0);
			}
			memcpy(target, &instr, sizeof instr);
		}
	}
	cc_cleanup(&e->waiters);
	return 0;
}

// returns the label's value if known, otherwise returns -1 and adds a waiter
// for that label
int64_t emitter_label_get_or_add_waiter(emitter *em, string key, enum assign_type wait_assign) {
	label nu;
	nu.val = -1;
	cc_init(&nu.waiters);
	label *e = cc_get_or_insert(&em->labels, key, nu);
	if (!e)
		panic(no_mem);
	if (e->val < 0) {
		label_waiter waiter = {
			.fix_idx = em->section[em->current_section].pos,
			.section = em->current_section,
			.assign = wait_assign,
		};
		if (!cc_push(&e->waiters, waiter))
			panic(no_mem);
		return -1;
	}
	return e->val;
}

int emit_section(emitter *em, int dst, int sect) {
	int l = em->section[sect].len;
	int m = em->section[sect].pos - l;
	off_t zero = 0;
	return (
		copy_file_range(em->section[sect].swap, &zero, dst, NULL, m, 0) != m
		|| write(dst, em->section_buf[sect], l) != l
	);
}

#define BYTESIZE(x) (sizeof(x) * (CHAR_BIT / 8))

int emitter_output_elf(emitter *em, int dst) {
	// TODO: this does not work on a big endian machine
	// (it should emit a little-endian executable, but it should still work)
	// TODO: this probably relies on CHAR_BIT being 8 despite the effort
	// to be independent of this
	// NOTE: actually, it definitely does rely on this, since the
	// emitter increments its pos and len by sizeof but relies on
	// this being in bytes
	struct {
		Elf64_Ehdr ehdr;
		Elf64_Phdr text;
		Elf64_Phdr data;
	} header;
	bzero(&header, sizeof header);

	const size_t pagesize = 0x1000;
	ssize_t after = BYTESIZE(header.ehdr) + BYTESIZE(header.text);

	header.ehdr.e_phnum = 1;
	header.text.p_type = PT_LOAD;
	header.text.p_flags = PF_X | PF_R;
	// map from 0 because it's page aligned
	// this wraps in the elf/program headers
	// so adjust the entry point to compensate
	header.text.p_offset = 0;
	header.text.p_vaddr = em->section[SECT_TEXT].vaddr;
	header.text.p_paddr = em->section[SECT_TEXT].vaddr;
	header.text.p_filesz = em->section[SECT_TEXT].pos;
	header.text.p_memsz = em->section[SECT_TEXT].pos;
	header.text.p_align = pagesize;

	if (em->section[SECT_DATA].pos > 0) {
		after += BYTESIZE(header.data);
		header.ehdr.e_phnum++;
		header.data.p_type = PT_LOAD;
		header.data.p_flags = PF_R | PF_W;
		header.data.p_offset = roundup(header.text.p_filesz + header.text.p_offset, pagesize);
		header.data.p_vaddr = em->section[SECT_DATA].vaddr;
		header.data.p_paddr = em->section[SECT_DATA].vaddr;
		header.data.p_filesz = em->section[SECT_DATA].pos;
		header.data.p_memsz = em->section[SECT_DATA].pos;
		header.data.p_align = pagesize;
	}

	header.ehdr.e_ident[EI_MAG0] = 0x7f;
	header.ehdr.e_ident[EI_MAG1] = 'E';
	header.ehdr.e_ident[EI_MAG2] = 'L';
	header.ehdr.e_ident[EI_MAG3] = 'F';
	header.ehdr.e_ident[EI_CLASS] = ELFCLASS64;
	header.ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
	header.ehdr.e_ident[EI_VERSION] = EV_CURRENT;
	header.ehdr.e_ident[EI_OSABI] = ELFOSABI_SYSV;
	header.ehdr.e_ident[EI_ABIVERSION] = 0;
	header.ehdr.e_ident[EI_PAD] = 0;
	header.ehdr.e_type = ET_EXEC;
	header.ehdr.e_machine = EM_RISCV;
	header.ehdr.e_version = EV_CURRENT;
	const string start_label = {
		.begin = "_start",
		.len = 6
	};
	label *start = cc_get(&em->labels, start_label);
	if (start) {
		// bad things will happen if _start was defined outside of .text
		header.ehdr.e_entry = start->val;
	} else {
		header.ehdr.e_entry = em->section[SECT_TEXT].vaddr;
	}
	header.ehdr.e_entry += after;
	header.ehdr.e_phoff = 0x40; // program headers come after the elf header
	header.ehdr.e_shoff = 0;
	header.ehdr.e_flags = 0;
	header.ehdr.e_ehsize = 64;
	header.ehdr.e_phentsize = 0x38;
	//header.ehdr.e_phnum set earlier
	header.ehdr.e_shentsize = 0;
	header.ehdr.e_shnum = 0;
	header.ehdr.e_shstrndx = 0;

	if (
		write(dst, &header, after) != after
		|| emit_section(em, dst, SECT_TEXT)
	)
		return 1;
	if (em->section[SECT_DATA].pos > 0) {
		return (
			lseek(dst, header.data.p_offset, SEEK_SET) == (off_t) -1
			|| emit_section(em, dst, SECT_DATA)
		);
	}
	return 0;
}
