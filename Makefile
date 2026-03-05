SOURCES=main.c trie.c emitter.c parser.c ops.c instruction_trie.c argparse.c
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

asm: $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) -o $@

TEST_OBJS=$(filter-out main.o, $(OBJS)) test.o
test: $(TEST_OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(TEST_OBJS) -o $@

clean:
	rm -f asm test instruction_trie/builder instruction_trie.c *.o
