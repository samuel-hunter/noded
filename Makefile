.POSIX:
.SUFFIXES:

RM ?= rm -f

CFLAGS := --std=c99 -Werror -Wall -Wextra -Wpedantic -D_DEFAULT_SOURCE -Iinclude

TARGET := noded
OBJECTS := build/noded.o build/err.o build/token.o \
	build/scanner.o build/ast.o build/dict.o build/parser.o \
	build/util.o build/vm.o build/compiler.o

OTHER_OBJS := build/disasm.o build/test-vm.o build/test-compiler.o

TOOLS := disasm
TESTS := test-vm test-compiler

DISASM_OBJS := build/disasm.o

TEST_VM_OBJS := build/test-vm.o build/vm.o build/util.o
TEST_COMPILER_OBJS := build/test-compiler.o build/err.o build/token.o \
	build/scanner.o build/ast.o build/dict.o build/parser.o \
	build/util.o build/vm.o build/compiler.o

default: $(TARGET)
all: $(TARGET) tools

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

tools: $(TOOLS)

disasm: $(DISASM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(DISASM_OBJS) $(LDLIBS)

test: $(TESTS)
	./test-vm
	./test-compiler

test-vm: $(TEST_VM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(TEST_VM_OBJS) $(LDLIBS)

test-compiler: $(TEST_COMPILER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(TEST_COMPILER_OBJS) $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) $(TARGET) $(TOOLS) $(TESTS) $(OBJECTS) $(OTHER_OBJS)

.PHONY: default all tools test clean
