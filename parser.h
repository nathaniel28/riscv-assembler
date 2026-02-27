#ifndef PARSER_H
#define PARSER_H

#include "emitter.h"

// used in testing
extern int parse_reg(char **_s, uint32_t *r);

// used in testing
extern char *parse_line(char **_s, emitter *em);

//void assemble(char *input, size_t len, int dst_fd);

#endif
