#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

static const int ERROR_MAX = 10;

/* Module-global variables */
static struct {
	SymDict dict; /* todo move to main() in the future */
	char *fname;
	FILE *f;
	int nerrors;
} Globals = {0};

void send_error(const Position *pos, ErrorType type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprint_error(Globals.fname, Globals.f, pos, type, fmt, ap);
	va_end(ap);

	switch (type) {
	case WARN:
		break;
	case ERR:
		if (++Globals.nerrors > ERROR_MAX)
			errx(1, "too many errors.");
		break;
	case FATAL:
		/* Fatal errors mean the program should die */
		errx(1, "fatal error.");
		break;
	}
}

/* Print disassembled code */
void disasm(uint8_t *bytecode, size_t n)
{
	uint8_t *end = &bytecode[n];
	while (bytecode < end) {
		int advance = 1;
		printf("    %s", opstr(bytecode[0]));
		switch (bytecode[0]) {
		case OP_PUSH:
			advance = 2;
			printf(" 0x%02x\n", bytecode[1]);
			break;
		default:
			printf("\n");
			break;
		}

		bytecode += advance;
	}
}

static void report_processor(Scanner *s)
{
	Token name;
	Token source;
	size_t n;
	uint8_t *bytecode;

	expect(s, PROCESSOR, NULL);
	expect(s, IDENTIFIER, &name);

	switch (peektype(s)) {
	case LBRACE:
		/* assumes compile returns non-NULL because
		 * send_error() automatically exits */
		bytecode = compile(s, &Globals.dict, &n);

		if (Globals.nerrors == 0) {
			printf("Processor %s:\n", name.lit);
			disasm(bytecode, n);
		}

		free(bytecode);
		break;
	case ASSIGN:
		scan(s, NULL);
		expect(s, IDENTIFIER, &source);
		expect(s, SEMICOLON, NULL);
		printf("Processor %s copies %s\n", name.lit, source.lit);
		break;
	default:
		send_error(&s->peek.pos, ERR, "Unexpected token %s",
			tokstr(s->peek.type));
	}
}

static void report_buffer(Scanner *s)
{
	Token name;
	Token value;

	expect(s, BUFFER, NULL);
	expect(s, IDENTIFIER, &name);
	expect(s, ASSIGN, NULL);
	expect(s, STRING, &value);
	expect(s, SEMICOLON, NULL);

	printf("Buffer %s = \"%s\"\n", name.lit, value.lit);
}

static void report_stack(Scanner *s)
{
	Token name;

	expect(s, STACK, NULL);
	expect(s, IDENTIFIER, &name);
	expect(s, SEMICOLON, NULL);

	printf("Stack %s\n", name.lit);
}

static void report_wire(Scanner *s)
{
	Token srcnode;
	Token srcport;
	Token destnode;
	Token destport;

	expect(s, IDENTIFIER, &srcnode);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, &srcport);
	expect(s, WIRE, NULL);
	expect(s, IDENTIFIER, &destnode);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, &destport);
	expect(s, SEMICOLON, NULL);

	printf("Wire %s.%s -> %s.%s\n",
		srcnode.lit, srcport.lit, destnode.lit, destport.lit);
}

int main(int argc, char *argv[])
{
	Scanner s;

	if (argc != 2)
		errx(1, "usage: %s file", argv[0]);

	Globals.fname = argv[1];
	Globals.f = fopen(Globals.fname, "r");
	if (Globals.f == NULL)
		err(1, "%s", argv[1]);

	init_scanner(&s, Globals.f);
	while (peektype(&s) != TOK_EOF) {
		switch (peektype(&s)) {
		case PROCESSOR:
			report_processor(&s);
			break;
		case BUFFER:
			report_buffer(&s);
			break;
		case STACK:
			report_stack(&s);
			break;
		case IDENTIFIER:
			report_wire(&s);
			break;
		default:
			send_error(&s.peek.pos, ERR,
				"Unexpected token %s", tokstr(s.peek.type));
			break;
		}
	}

	fclose(Globals.f);
	return Globals.nerrors ? 1 : 0;
}
