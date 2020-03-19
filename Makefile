.POSIX:
.SUFFIXES:

TARGET := noded

default: $(TARGET)
all: $(TARGET) tools

$(TARGET):
	$(MAKE) -C src ../$(TARGET)

tools:
	$(MAKE) -C tools all

test:
	$(MAKE) -C test test-all

clean:
	$(MAKE) -C src clean
	$(MAKE) -C tools clean
	$(MAKE) -C test clean

.PHONY: default all tools test clean
.SUFFIXES: .c .o
