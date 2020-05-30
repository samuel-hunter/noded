/*
 * noded - Noded bytecode interpreter
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

typedef struct NodeRule NodeRule;
struct NodeRule {
	NodeType type;
	size_t id;
	union {
		CodeBlock proc;
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

/*
 * Return a pointer to a new node rule. This pointer
 * is valid until the next `new_noderule` call.
 */
static NodeRule *
new_noderule(NodeRuleVec *vec, NodeType type, size_t id)
{
	NodeRule *rule;

	if (vec->cap == vec->len) {
		vec->cap = vec->cap ? vec->cap*2 : 8;
		vec->rules = erealloc(vec->rules,
			vec->cap * sizeof(*vec->rules));
	}

	rule = &vec->rules[vec->len++];
	rule->type = type;
	rule->id = id;
	return rule;
}

/*
 * Return a pointer to the target node rule, or
 * NULL if it doesn't exist. This pointer is valid
 * until the next `new_noderule` call.
 */
static NodeRule *
find_noderule(NodeRuleVec *vec, size_t id)
{
	for (size_t i = 0; i < vec->len; i++) {
		if (vec->rules[i].id == id)
			return &vec->rules[i];
	}

	return NULL;
}

static void
add_processor(Scanner *s, NodeRuleVec *rules, SymDict *dict)
{
	Token name, source;
	NodeRule *rule, *source_rule;

	expect(s, PROCESSOR, NULL);
	expect(s, IDENTIFIER, &name);
	/* Either compile the code block, or piggyback off of another node's code. */
	switch (peektype(s)) {
	case LBRACE:
		/* compile it */
		rule = new_noderule(rules, PROC_NODE, sym_id(dict, name.lit));
		compile(s, dict, &rule->proc);
		break;
	case ASSIGN:
		/* find a previous node's code block */
		expect(s, ASSIGN, NULL);
		expect(s, IDENTIFIER, &source);
		expect(s, SEMICOLON, NULL);

		source_rule = find_noderule(rules, sym_id(dict, source.lit));
		if (source_rule) {
			rule = new_noderule(rules, PROC_NODE, sym_id(dict, name.lit));
			compile(s, dict, &rule->proc);
		} else {
			send_error(&source.pos, ERR,
				"processor node %s does not exist", source.lit);
		}
		break;
	default:
		send_error(&source.pos, ERR, "unexpected token %s", tokstr(peektype(s)));
		break;
	}
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
		switch (peektype(&s)) {
		case PROCESSOR:
			add_processor(&s, &rules, &dict);
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
	clear_dict(&dict);

	return 0;
}