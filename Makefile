
CFLAGS := -std=c99 -Werror -Wall -Wextra -Wpedantic -O2
LDFLAGS :=
PREFIX := /usr/local
TARGS := noded nodedc

NODED_OBJS := alloc.o compiler.o dict.o err.o noded.o parse.o scanner.o token.o vec.o vm.o
NODEDC_OBJS := alloc.o compiler.o dict.o err.o nodedc.o parse.o scanner.o token.o vec.o

default: noded

all: $(TARGS)

noded: $(NODED_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(NODED_OBJS)

nodedc: $(NODEDC_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(NODEDC_OBJS)

%.o: %.c noded.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: noded
	install -m 755 -d $(PREFIX)/bin
	install -m 755 noded $(PREFIX)/bin/

clean:
	rm -f *.o $(TARGS)

.PHONY: default all install clean
