.POSIX:
.SUFFIXES:

TESTS := test-vm
OBJECTS := test-vm.o

VM_OBJECTS := test-vm.o ../src/vm.o ../src/util.o

include ../global.make

default: test-all
test-all: test-vm

# Make noded to make sure all dependent objects are (re-)compiled.
../noded:
	make -C ../src $@

test-vm: ../noded $(VM_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(VM_OBJECTS) $(LDLIBS)
	./$@

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) $(TARGET) $(OBJECTS)

.PHONY: clean
.SUFFIXES: .c .o
