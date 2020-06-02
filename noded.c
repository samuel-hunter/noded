/*
 * noded - Noded bytecode interpreter
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

/*
 * The VM recognizes ports as an index from 0 to PORT_MAX-1. The compiler
 * fills out an array mapping each port's name (as an id from sym_id())
 * to the index. Here, a vector of NodeRules stores all ports so that
 * scan_wire() can map `node.port` to a node# and port#.
 */
typedef struct NodeRule NodeRule;
struct NodeRule {
	size_t id;
	size_t ports[PORT_MAX];
	int nports;
};

/*
 * Search through the NodeRules, and if one is found,
 * set *idx (if non-NULL) to the index, and then return the node rule
 * pointer.
 */
static NodeRule *
find_rule(NodeRule *rules, size_t nrules, size_t node_id, size_t *idx)
{
	for (size_t i = 0; i < nrules; i++) {
		if (rules[i].id == node_id) {
			if (idx) *idx = i;
			return &rules[i];
		}
	}

	return NULL;
}

static int
find_port(NodeRule *rule, size_t port_id)
{
	for (int i = 0; i < rule->nports; i++) {
		if (rule->ports[i] == port_id)
			return i;
	}

	return -1;
}

/*
 * the skip_*() procedures consume and discard all the tokens
 * specific to a singular declaration.
 */

static void
skip_processor(Scanner *s)
{
	Token tok;
	int depth;

	expect(s, PROCESSOR, NULL);
	expect(s, IDENTIFIER, NULL);
	switch(peektype(s)) {
	case ASSIGN:
		expect(s, ASSIGN, NULL);
		expect(s, IDENTIFIER, NULL);
		expect(s, SEMICOLON, NULL);
		break;
	case LBRACE:
		depth = 0;
		do {
			scan(s, &tok);
			switch(tok.type) {
			case LBRACE: depth++; break;
			case RBRACE: depth--; break;
			case TOK_EOF:
				send_error(&tok.pos, ERR, "EOF reached within node block");
				depth = 0;
				break;
			default: break;
			}
		} while (depth > 0);
		break;
	default:
		send_error(&s->peek.pos, ERR, "unexpected token %s", tokstr(tok.type));
		break;
	}
}

static void
skip_buffer(Scanner *s)
{
	expect(s, BUFFER, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, ASSIGN, NULL);
	expect(s, STRING, NULL);
	expect(s, SEMICOLON, NULL);
}

static void
skip_stack(Scanner *s)
{
	expect(s, STACK, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, SEMICOLON, NULL);
}

static void
skip_wire(Scanner *s)
{
	expect(s, IDENTIFIER, NULL);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, WIRE, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, NULL);
	expect(s, SEMICOLON, NULL);
}

/*
 * the scan_*() procedures take in a node declaration (or a wire) and add it
 * to the VM. All nodes and their respective port data are added to the NodeRule
 * array.
 */

static void
scan_processor(Scanner *s, SymDict *dict, VM *vm, NodeRule *rules, size_t nrules)
{
	Token name, source;
	size_t source_id, source_idx;
	CodeBlock block;
	NodeRule *rule = &rules[nrules]; /* this node's rule */
	NodeRule *source_rule;

	expect(s, PROCESSOR, NULL);
	expect(s, IDENTIFIER, &name);

	switch (peektype(s)) {
	case LBRACE:
		compile(s, dict, &block);
		add_proc_node(vm, block.code, block.size);
		rule->id = sym_id(dict, name.lit);
		memcpy(rule->ports, block.ports, sizeof(rule->ports));
		rule->nports = block.nports;
		break;
	case ASSIGN:
		expect(s, ASSIGN, NULL);
		expect(s, IDENTIFIER, &source);
		expect(s, SEMICOLON, NULL);

		source_id = sym_id(dict, source.lit);
		source_rule = find_rule(rules, nrules, source_id, &source_idx);
		if (source_rule) {
			copy_proc_node(vm, source_idx);
			if (!has_errors()) {
				/* This node has the same exact rules as the previous node. */
				*rule = *source_rule;
				rule->id = sym_id(dict, name.lit);
			}
		} else {
			send_error(&name.pos, ERR, "processor %s does not exist", name.lit);
		}
		break;
	default:
		send_error(&s->peek.pos, ERR,
			"unexpected token %s", tokstr(s->peek.type));
	}
}

static void
scan_buffer(Scanner *s, SymDict *dict, VM *vm, NodeRule *rules, size_t nrules)
{
	Token name, value;
	uint8_t dat[BUFFER_NODE_MAX];
	NodeRule *rule;

	size_t ports[] = {
		[BUFFER_ELM] = sym_id(dict, "elm"),
		[BUFFER_IDX] = sym_id(dict, "idx"),
	};

	expect(s, BUFFER, NULL);
	expect(s, IDENTIFIER, &name);
	expect(s, ASSIGN, NULL);
	expect(s, STRING, &value);
	expect(s, SEMICOLON, NULL);

	/* Add the buffer to the VM */
	parse_string(dat, &value);
	add_buf_node(vm, dat);

	/* Set up the rules for wiring */
	rule = &rules[nrules];
	rule->id = sym_id(dict, name.lit);
	memcpy(rule->ports, ports, sizeof(ports));
	rule->nports = sizeof(ports)/sizeof(*ports);
}

static void
scan_stack(Scanner *s, SymDict *dict, VM *vm, NodeRule *rules, size_t nrules)
{
	Token name;
	NodeRule *rule;

	size_t ports[] = {
		[STACK_ELM] = sym_id(dict, "elm"),
	};

	expect(s, STACK, NULL);
	expect(s, IDENTIFIER, &name);
	expect(s, SEMICOLON, NULL);

	/* Add the stack to the VM */
	add_stack_node(vm);

	/* Set up the rules for wiring */
	rule = &rules[nrules];
	rule->id = sym_id(dict, name.lit);
	memcpy(rule->ports, ports, sizeof(ports));
	rule->nports = sizeof(ports)/sizeof(*ports);
}

static void
scan_wire(Scanner *s, SymDict *dict, VM *vm, NodeRule *rules, size_t nrules)
{
	Token node1, port1, node2, port2;
	size_t node1_id, node2_id;
	size_t node1_idx, node2_idx;
	int port1idx = -1, port2idx = -1;
	NodeRule *rule;

	expect(s, IDENTIFIER, &node1);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, &port1);
	expect(s, WIRE, NULL);
	expect(s, IDENTIFIER, &node2);
	expect(s, PERIOD, NULL);
	expect(s, IDENTIFIER, &port2);
	expect(s, SEMICOLON, NULL);

	node1_id = sym_id(dict, node1.lit);
	node2_id = sym_id(dict, node2.lit);

	/* The VM recognizes the indices of each node and port. Go through the
     * node rules to find them. */

	rule = find_rule(rules, nrules, node1_id, &node1_idx);
	if (rule) {
		port1idx = find_port(rule, sym_id(dict, port1.lit));
		if (port1idx < 0)
			send_error(&port1.pos, ERR, "undefined port %s", port1.lit);
	} else {
		send_error(&node1.pos, ERR, "undefined node %s", node1.lit);
	}

	rule = find_rule(rules, nrules, node2_id, &node2_idx);
	if (rule) {
		port2idx = find_port(rule, sym_id(dict, port2.lit));
		if (port2idx < 0)
			send_error(&port2.pos, ERR, "undefined port %s", port2.lit);
	} else {
		send_error(&node2.pos, ERR, "undefined node %s", node2.lit);
	}

	if (has_errors()) return;

	add_wire(vm, node1_idx, port1idx, node2_idx, port2idx);
}

int
main(int argc, char *argv[])
{
	const char *fname;
	FILE *f;
	Scanner s;

	size_t nnodes = 1; /* start with 1 for the IO node */
	size_t nwires = 0;

	VM vm;
	SymDict dict = {0};
	NodeRule *rules = NULL;
	size_t nodes_parsed = 0;

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

	/* First pass: count the number of nodes and wires
     * to allocate
     */
	while (peektype(&s) != TOK_EOF && !has_errors()) {
		switch (peektype(&s)) {
		case PROCESSOR:
			nnodes++;
			skip_processor(&s);
			break;
		case BUFFER:
			nnodes++;
			skip_buffer(&s);
			break;
		case STACK:
			nnodes++;
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
	if (has_errors()) return 1;

	/* nnodes+1 to account for IO node */
	vm_init(&vm, nnodes, nwires);
	rules = ecalloc(nnodes, sizeof(*rules));

	/* Rewind to the beginning and rescan, building everything up. */
	if (fseek(f, 0, SEEK_SET) < 0)
		err(1, "%s", fname);
	init_scanner(&s, f); /* re-initialize */

	/* begin with adding the IO node */
	{
		size_t io_ports[] = {
			[IO_IN] = sym_id(&dict, "in"),
			[IO_OUT] = sym_id(&dict, "out"),
			[IO_ERR] = sym_id(&dict, "err"),
		};
		NodeRule *rule = &rules[nodes_parsed++];

		rule->id = sym_id(&dict, "io");
		memcpy(rule->ports, io_ports, sizeof(io_ports));
		rule->nports = sizeof(io_ports)/sizeof(*io_ports);

		add_io_node(&vm);
	}

	/* Add all the nodes and wires */
	while (peektype(&s) != TOK_EOF && !has_errors()) {
		switch (peektype(&s)) {
		case PROCESSOR:
			scan_processor(&s, &dict, &vm, rules, nodes_parsed);
			nodes_parsed++;
			break;
		case BUFFER:
			scan_buffer(&s, &dict, &vm, rules, nodes_parsed);
			nodes_parsed++;
			break;
		case STACK:
			scan_stack(&s, &dict, &vm, rules, nodes_parsed);
			nodes_parsed++;
			break;
		case IDENTIFIER:
			scan_wire(&s, &dict, &vm, rules, nodes_parsed);
			break;
		default:
			send_error(&s.peek.pos, ERR,
				"unexpected token %s", tokstr(s.peek.type));
			break;
		}
	}

	if (has_errors()) return 1;

	free(rules);
	clear_dict(&dict);
	run(&vm);

	/* don't free the VM's memory -- the OS collects the garbage anyway */
	return 0;
}
