.POSIX:
.SUFFIXES:

TARGET := noded
OBJECTS := noded.o token.o scanner.o ast.o dict.o parser.o util.o vm.o

# Compiled files that aren't part of the main program, like testing binaries.
OTHER_BINS := test-vm
OTHER_OBJS := test-vm.o

CFLAGS := -g --std=c99 -Werror -Wall -Wextra -Wpedantic
LDLIBS :=

default: $(TARGET)
all: $(TARGET) $(OTHER_BINS)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-vm: util.o vm.o test-vm.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.c.o: noded.h
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) $(TARGET) $(OBJECTS) $(OTHER_BINS) $(OTHER_OBJS)


.PHONY: all default clean
.SUFFIXES: .c .o
