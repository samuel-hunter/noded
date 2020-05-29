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
	uint16_t *code_sizes;

	size_t len;
	size_t cap;
};

typedef struct NodeRule NodeRule;
struct NodeRule {
	NodeType type;
	union {
		struct {
			const uint8_t *code;
			uint16_t code_size;
		} proc;
	};
};

typedef struct NodeRuleVec NodeRuleVec;
struct NodeRuleVec {
	NodeRule *rules;
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
codedict_add(CodeDict *dict, size_t id,
	uint8_t *code_block, uint16_t code_size)
{
	if (dict->len == dict->cap) {
		dict->cap = dict->cap ? dict->cap*2 : 8;
		dict->ids = erealloc(dict->ids,
			dict->cap * sizeof(*dict->ids));
		dict->code_blocks = erealloc(dict->code_blocks,
			dict->cap * sizeof(*dict->code_blocks));
		dict->code_sizes = erealloc(dict->code_sizes,
			dict->cap * sizeof(*dict->code_sizes));
	}

	dict->ids[dict->len] = id;
	dict->code_blocks[dict->len] = code_block;
	dict->code_sizes[dict->len] = code_size;
	dict->len++;
}

static uint8_t *
codedict_find(const CodeDict *dict, size_t id, uint16_t *size)
{
	for (size_t i = 0; i < dict->len; i++) {
		if (dict->ids[i] == id) {
			*size = dict->code_sizes[i];
			return dict->code_blocks[i];
		}
	}

	return NULL;
}

/* Free the memory of the entire code dictionary, *except* for
 * the individual code blocks.
 */
static void
codedict_clear(CodeDict *dict)
{
	free(dict->ids);
	free(dict->code_blocks);
	free(dict->code_sizes);
	memset(dict, 0, sizeof(*dict));
}

/*
 * Return a pointer to a new node rule. This pointer
 * is valid until the next `new_noderule` call.
 */
static NodeRule *
new_noderule(NodeRuleVec *vec)
{
	if (vec->cap == vec->len) {
		vec->cap = vec->cap ? vec->cap*2 : 8;
		vec->rules = erealloc(vec->rules,
			vec->cap * sizeof(*vec->rules));
	}

	return &vec->rules[vec->len++];
}

/*
 * Free the memory allocated by the NodeRuleVec.
 */
static void
clear_noderules(NodeRuleVec *vec)
{
	free(vec->rules);
	memset(vec, 0, sizeof(*vec));
}

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

int
main(int argc, char *argv[])
{
	const char *fname;
	FILE *f;
	Scanner s;

	SymDict dict = {0};
	CodeDict code = {0};
	NodeRuleVec rules = {0};
	
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
		Token name;
		Token source;
		uint8_t *code_block;
		uint16_t code_size;
		NodeRule *rule;

		switch (peektype(&s)) {
		case PROCESSOR:
			expect(&s, PROCESSOR, NULL);
			expect(&s, IDENTIFIER, &name);
			rule = new_noderule(&rules);
			/* Either compile the code block, or piggyback off of another node's code. */
			switch (peektype(&s)) {
			case LBRACE:
				/* compile it */
				code_block = compile(&s, &dict, &code_size);
				break;
			case ASSIGN:
				/* find a previous node's code block */
				expect(&s, ASSIGN, NULL);
				expect(&s, IDENTIFIER, &source);
				expect(&s, SEMICOLON, NULL);
				code_block = codedict_find(&code, sym_id(&dict, source.lit), &code_size);
				if (!code_block)
					send_error(&source.pos, ERR, "processor node %s does not exist", source.lit);
				break;
			default:
				send_error(&source.pos, ERR, "unexpected token %s", tokstr(peektype(&s)));
				break;
			}
			if (has_errors()) break;
			codedict_add(&code, sym_id(&dict, name.lit), code_block, code_size);
			rule->type = PROC_NODE;
			rule->proc.code = code_block;
			rule->proc.code_size = code_size;
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
				"unexpected token %s", tokstr(s.peek.type));
			break;
		}
	}

	fclose(f);
	if (has_errors()) return 1;

	/* Give a status report */
	printf("Nodes: %lu\n", rules.len);
	printf("Wires: %lu\n", nwires);

	/* Clean up some (but not all) memory */
	clear_noderules(&rules);
	codedict_clear(&code);
	clear_dict(&dict);

	return 0;
}