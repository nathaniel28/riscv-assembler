SOURCES=main.c trie.c emitter.c parser.c ops.c instruction_trie.c
OBJS=$(addsuffix .o, $(basename $(notdir $(SOURCES))))
CFLAGS=-g -Wall -Wextra -pedantic
LIBS=

default: asm

instruction_trie/builder: instruction_trie/main.c trie.o ops.o
	$(CC) $(CFLAGS) instruction_trie/main.c -o $@

instruction_trie.c: instruction_trie/builder
	./instruction_trie/builder > instruction_trie.c

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

asm: instruction_trie.h $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) -o $@

clean:
	rm -f asm instruction_trie/builder instruction_trie.c *.o
