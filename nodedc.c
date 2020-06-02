/*
 * nodedc - complement program to print a breakdown of a program's structure,
 * as well as reporting the disassembled bytecode.
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

/* Print disassembled code */
static void
disasm(CodeBlock *block)
{
	uint16_t addr = 0;
	while (addr < block->size) {
		uint8_t *instr = &block->code[addr];
		int advance = 1;
		uint16_t jmpaddr;

		printf("\t0x%04x    %s", addr, opstr(instr[0]));
		switch (instr[0]) {
		case OP_PUSH:
			advance = 2;
			printf("\t0x%02x\n", instr[1]);
			break;
		case OP_JMP:
		case OP_FJMP:
			advance = 3;
			jmpaddr = instr[1] + (instr[2]<<8);
			printf("\t0x%04x\n", jmpaddr);
			break;
		default:
			printf("\n");
			break;
		}

		addr += advance;
	}
	printf("\t0x%04x    EOF\n", block->size);
}

static void
report_processor(Scanner *s, SymDict *dict)
{
	Token name;
	Token source;
	CodeBlock block;

	expect(s, PROCESSOR, NULL);
	expect(s, IDENTIFIER, &name);

	switch (peektype(s)) {
	case LBRACE:
		/* assumes compile returns non-NULL because
		 * send_error() automatically exits */
		compile(s, dict, &block);
		if (has_errors()) break;

		printf("Processor %s:\n", name.lit);
		disasm(&block);
		free(block.code);
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

static void
report_buffer(Scanner *s)
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

static void
report_stack(Scanner *s)
{
	Token name;

	expect(s, STACK, NULL);
	expect(s, IDENTIFIER, &name);
	expect(s, SEMICOLON, NULL);

	printf("Stack %s\n", name.lit);
}

static void
report_wire(Scanner *s)
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

int
main(int argc, char *argv[])
{
	SymDict dict = {0};
	Scanner s;
	char *fname;
	FILE *f;

	if (argc != 2)
		errx(1, "usage: %s file", argv[0]);

	fname = argv[1];
	f = fopen(fname, "r");
	if (f == NULL)
		err(1, "%s", argv[1]);

	init_error(f, fname);
	init_scanner(&s, f);
	while (peektype(&s) != TOK_EOF) {
		switch (peektype(&s)) {
		case PROCESSOR:
			report_processor(&s, &dict);
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

	fclose(f);
	return has_errors() ? 1 : 0;
}
