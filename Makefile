.POSIX:
.SUFFIXES:

TARGET := noded
OBJECTS := noded.o

CFLAGS := --std=c90 -Werror -Wall -Wpedantic
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
