.POSIX:
.SUFFIXES:

TARGETS := ../disasm
OBJECTS := disasm.o

DISASM_OBJECTS := disasm.o

include ../global.make

all: $(TARGETS)

../disasm: $(DISASM_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(DISASM_OBJECTS) $(LDLIBS)

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) $(TARGETS) $(OBJECTS)

.PHONY: all clean
.SUFFIXES: .c .o
