
CFLAGS := -Werror -Wall -Wextra -Wpedantic -g
LDFLAGS :=
TARGS := noded nodedc

all: $(TARGS)

noded: alloc.o compiler.o dict.o err.o noded.o parse.o scanner.o token.o vec.o
	$(CC) $(LDFLAGS) -o $@ $^

nodedc: alloc.o compiler.o dict.o err.o nodedc.o parse.o scanner.o token.o vec.o
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c noded.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGS)
