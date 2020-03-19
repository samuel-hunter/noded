.POSIX:
.SUFFIXES:

TARGET := ../noded
OBJECTS := noded.o err.o token.o scanner.o ast.o dict.o parser.o util.o vm.o compiler.o

include ../global.make

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) $(TARGET) $(OBJECTS)

.PHONY: clean
.SUFFIXES: .c .o
