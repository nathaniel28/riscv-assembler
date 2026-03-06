#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "argparse.h"
#include "emitter.h"
#include "parser.h"

int main(int argc, char **argv) {
	char *output_file = "a.out";
	long long text_vaddr = 0x00400000;
	long long data_vaddr = 0x10010000;
	Option opts[] = {
		OPT('o', NULL, OPT_STR, &output_file),
		OPT('\0', "text-vaddr", OPT_LLONG, &text_vaddr),
		OPT('\0', "data-vaddr", OPT_LLONG, &data_vaddr),
	};
	int extra_args = argparse(argc, argv, opts, sizeof(opts) / sizeof(opts[0]));
	if (extra_args != 2) {
		printf("Exactly one input must be specified.\n");
		return 1;
	}
	char *input_file = argv[1];
	if (strcmp(input_file, output_file) == 0) {
		printf("Input and output files share the same name %s\n", input_file);
		return 1;
	}

	// TODO: write to a temporary file then link that to the expected
	// output location because calls to lseek that expand the file must
	// pad with zeros and that might not happen if the file exists and has
	// data up to the seek
	int output_fd = open(output_file, O_WRONLY | O_CREAT/* | O_EXCL*/, 0755);
	if (output_fd == -1) {
		printf("Failed to open %s: %s\n", output_file, strerror(errno));
		return 1;
	}

	int input_fd = open(input_file, O_RDONLY);
	if (input_fd == -1) {
		printf("Failed to open %s: %s\n", input_file, strerror(errno));
		return 1;
	}
	struct stat sb;
	if (fstat(input_fd, &sb)) {
		printf("Failed to stat %s: %s\n", input_file, strerror(errno));
		return 1;
	}
	if (sb.st_size == 0) {
		printf("%s is completely empty!\n", input_file);
		return 1;
	}
	// NOTE: changes made to the underlying file should not be visible in
	// this mapped region, but man pages say this behavior is unspecified
	// the worst that can happen is that this process reads out-of-bounds,
	// general UB, and possibly crash :)
	char *in = mmap(NULL, sb.st_size + 1, PROT_READ | PROT_WRITE, MAP_PRIVATE, input_fd, 0);
	close(input_fd);
	if (in == MAP_FAILED) {
		printf("Failed to mmap %s: %s\n", input_file, strerror(errno));
		return 1;
	}
	in[sb.st_size] = '\n';

	emitter *em = calloc(1, sizeof *em);
	if (!em) {
		printf("Out of memory!\n");
		return 1;
	}
	// Why do I have to make this definition? No idea! Should've been done
	// in fcntl.h
#define O_TMPFILE __O_TMPFILE
	em->section[SECT_TEXT].swap = open("/var/tmp", O_TMPFILE | O_RDWR, 0600);
	em->section[SECT_DATA].swap = open("/var/tmp", O_TMPFILE | O_RDWR, 0600);
	if (em->section[SECT_TEXT].swap == -1 || em->section[SECT_DATA].swap == -1) {
		printf("Failed to open temporary files.\n");
		return 1;
	}
	em->section[SECT_TEXT].vaddr = text_vaddr;
	em->section[SECT_DATA].vaddr = data_vaddr;
	em->current_section = SECT_TEXT;
	cc_init(&em->labels);

	char *pos = in;
	char *end = in + sb.st_size;
	int line = 1;
	do {
		char *err = parse_line(&pos, em);
		if (err) {
			printf("%s:%d: %s\n", input_file, line, err);
			return 1;
		}
		line++;
		pos++;
	} while (pos < end);

	int err = emitter_output_elf(em, output_fd);
	if (err) {
		printf("Failed to emit to %s: %s\n", output_file, strerror(errno));
		return 1;
	}

	close(em->section[SECT_TEXT].swap);
	close(em->section[SECT_DATA].swap);
	free(em);
	munmap(in, sb.st_size + 1);
	close(output_fd);
	return 0;
}
