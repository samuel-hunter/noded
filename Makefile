
CFLAGS := -std=c99 -Werror -Wall -Wextra -Wpedantic -g
LDFLAGS :=
PREFIX := /usr/local
TARGS := noded nodedc

default: noded

all: $(TARGS)

noded: alloc.o compiler.o dict.o err.o noded.o parse.o scanner.o token.o vec.o vm.o
	$(CC) $(LDFLAGS) -o $@ $^

nodedc: alloc.o compiler.o dict.o err.o nodedc.o parse.o scanner.o token.o vec.o
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c noded.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: noded
	install -m 755 -d $(PREFIX)/bin
	install -m 755 noded $(PREFIX)/bin/

clean:
	rm -f *.o $(TARGS)

.PHONY: default all install clean