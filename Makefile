SOURCES=main.c trie.c
OBJS=$(addsuffix .o, $(basename $(notdir $(SOURCES))))
CFLAGS=-g -Wall -Wextra -pedantic
LIBS=

default: asm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

asm: $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) -o $@

clean:
	rm -f asm *.o
