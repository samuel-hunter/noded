
CFLAGS := -Werror -Wall -Wextra -Wpedantic -g
LDFLAGS :=
RM ?= rm -f

nodedc: alloc.o dict.o err.o nodedc.o scanner.o token.o
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c noded.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) *.o nodedc
