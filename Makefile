.POSIX:
.SUFFIXES:

TARGET := noded
OBJECTS := noded.o token.o scanner.o ast.o parser.o util.o dict.o

CFLAGS := -g --std=c99 -Werror -Wall -Wextra -Wpedantic
LDLIBS :=


all: $(TARGET)
default: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.c.o: noded.h
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) $(TARGET) $(OBJECTS)


.PHONY: all default clean
.SUFFIXES: .c .o
