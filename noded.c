/*
 * noded - Noded bytecode interpreter
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

typedef struct CodeDict CodeDict;
struct CodeDict {
	size_t *ids;
	uint8_t **code_blocks;

	size_t len;
	size_t cap;
};

typedef struct WireRule WireRule;
struct WireRule {
	struct {
		size_t node_id;
		int portidx;
	} nodes[2];
};

static void
skip_buffer(Scanner *s)
{
	Token start;

	expect(s, BUFFER, &start);
	expect(s, IDENTIFIER, NULL);
	expect(s, ASSIGN, NULL);
	expect(s, STRING, NULL);
	expect(s, SEMICOLON, NULL);

	send_error(&start.pos, WARN,
		"buffers are unimplemented");
}

static void
skip_stack(Scanner *s)
{
	Token start;

	expect(s, STACK, &start);
	expect(s, IDENTIFIER, NULL);
	expect(s, SEMICOLON, NULL);

	send_error(&start.pos, WARN,
		"stacks are unimplemented");
}

static void
skip_wire(Scanner *s)
{
	Token start;

	expect(s, IDENTIFIER, &start);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, WIRE, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, SEMICOLON, NULL);

	send_error(&start.pos, WARN,
		"wires are unimplemented");
}

static void
skip_processor(Scanner *s)
{
	Token start;
	int depth;

	expect(s, PROCESSOR, &start);
	expect(s, IDENTIFIER, NULL);

	switch (peektype(s)) {
	case LBRACE:
		/* consume code block */
		depth = 0;
		do {
			Token tok;
			scan(s, &tok);
			switch (tok.type) {
			case LBRACE: depth++; break;
			case RBRACE: depth--; break;
			default: break;
			}
		} while (depth > 0);
		break;
	case ASSIGN:
		expect(s, ASSIGN, NULL);
		expect(s, IDENTIFIER, NULL);
		expect(s, SEMICOLON, NULL);
		break;
	default:
		send_error(&start.pos, ERR,
			"unexpected token %s", tokstr(peektype(s)));
		break;
	}

	send_error(&start.pos, WARN,
		"processors are unimplemented");
}

int
main(int argc, char *argv[])
{
	const char *fname;
	FILE *f;
	Scanner s;

	size_t nnodes = 0;
	/* CodeDict code = {0}; */
	/* WireRule *wires = NULL; */
	size_t nwires = 0;
	/* size_t wire_cap = 0; */

	if (argc != 2) {
		fprintf(stderr, "usage: %s FILE\n", argv[0]);
		return 1;
	}

	fname = argv[1];
	f = fopen(fname, "r");
	if (f == NULL)
		err(1, "%s", fname);

	init_error(f, fname);
	init_scanner(&s, f);
	while (peektype(&s) != TOK_EOF && !has_errors()) {
		nnodes++;
		switch (peektype(&s)) {
		case PROCESSOR:
			skip_processor(&s);
			break;
		case BUFFER:
			skip_buffer(&s);
			break;
		case STACK:
			skip_stack(&s);
			break;
		case IDENTIFIER:
			nwires++;
			skip_wire(&s);
			break;
		default:
			send_error(&s.peek.pos, ERR,
				"Unexpected token %s.", tokstr(s.peek.type));
			break;
		}
	}

	fclose(f);
	if (has_errors()) return 1;

	/* Give a status report */
	printf("Nodes: %lu\n", nnodes);
	printf("Wires: %lu\n", nwires);
}