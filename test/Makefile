.POSIX:
.SUFFIXES:

TESTS := test-vm test-compiler
OBJECTS := test-vm.o test-compiler.o

VM_OBJECTS := test-vm.o ../src/util.o ../src/vm.o
COMPILER_OBJECTS := test-compiler.o ../src/util.o ../src/scanner.o ../src/dict.o \
	../src/ast.o ../src/token.o ../src/parser.o ../src/compiler.o ../src/vm.o \
	../src/err.o

include ../global.make

default: test-all
test-all: $(TESTS)

# Make noded to make sure all dependent objects are (re-)compiled.
../noded:
	make -C ../src $@

test-vm: ../noded $(VM_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(VM_OBJECTS) $(LDLIBS)
	./$@

test-compiler: ../noded $(COMPILER_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(COMPILER_OBJECTS) $(LDLIBS)
	./$@

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) $(TARGET) $(OBJECTS)

.PHONY: clean
.SUFFIXES: .c .o
