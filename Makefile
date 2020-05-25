
CFLAGS := -Werror -Wall -Wextra -Wpedantic -g
LDFLAGS :=
RM ?= rm -f

nodedc: alloc.o compiler.o dict.o err.o nodedc.o parse.o scanner.o token.o vec.o
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c noded.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) *.o nodedc
