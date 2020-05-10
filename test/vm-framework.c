#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h> // free

#include "noded.h"
#include "vm-framework.h"

#define DEBUG stdout

static bool tick_test_node(void *this);
static void free_test_node(void *this);
static void add_wire_to_test_node(void *this, int porti, struct wire *wire);

static struct node_class test_node_class = {
	.tick = &tick_test_node,
	.free = &free_test_node,
	.add_wire = &add_wire_to_test_node
};

struct test_node {
	const struct vm_test *vm_test;

	struct wire *wires[PROC_PORTS];
	enum port_type port_types[PROC_PORTS];
	size_t buf_idx[PROC_PORTS];
};

static struct node *add_test_node(struct runtime *env, struct vm_test *vm_test)
{
	struct node *node = add_node(env);
	struct test_node *test = ecalloc(1, sizeof(*test));

	node->class = &test_node_class;
	node->dat = test;

	test->vm_test = vm_test;

	return node;
}

static bool tick_test_node(void *this)
{
	struct test_node *test = this;
	bool made_progress = false;
	uint8_t val;
	size_t buf_idx;

	for (int i = 0; i < PROC_PORTS; i++) {
		const struct test_port *port = &test->vm_test->ports[i];
		buf_idx = test->buf_idx[i];

		switch (test->port_types[i]) {
		case SEND_PORT:
			// proc node sending data to us -- let's receive it and check.
			switch (recv(test->wires[i], &val)) {
			case PROCESSED:
				if (buf_idx == port->buf_len) {
					errx(1, "Too many messages from port "
						"%d (expected only %lu messages)",
						i, port->buf_len);
				}

				if (port->buf[buf_idx] != val) {
					errx(1, "Expected %d as item #%lu from "
						"port %d, but received %d",
						port->buf[buf_idx], buf_idx,
						i, val);
				}

				test->buf_idx[i]++;
				made_progress = true;
				break;
			default:
				break;
			}
			break;
		case RECV_PORT:
			if (buf_idx == port->buf_len) break;

			switch (send(test->wires[i], port->buf[buf_idx])) {
			case BUFFERED:
				made_progress = true;
				break;
			case BLOCKED:
				break;
			case PROCESSED:
				made_progress = true;
				test->buf_idx[i]++;
			}
			break;
		default:
			break;
		}
	}

	return made_progress;
}

static void add_wire_to_test_node(void *this, int porti, struct wire *wire)
{
	struct test_node *test = this;

	assert(porti >= 0 && porti < PROC_PORTS);
	test->wires[porti] = wire;
}

static void free_test_node(void *this)
{
	free(this);
}

void test_code(struct vm_test *vm_test)
{
	struct runtime env = {0};
	struct node *test_node, *proc_node;
	struct test_node *test;

	test_node = add_test_node(&env, vm_test);
	proc_node = add_proc_node(&env, vm_test->code, vm_test->code_size);

	test = test_node->dat;

	// Wire up all the nodes.
	for (size_t i = 0; i < PROC_PORTS; i++) {
		enum port_type type =
			test->port_types[i] = vm_test->ports[i].type;

		switch (type) {
		case SEND_PORT:
			add_wire(&env, proc_node, i, test_node, i);
			break;
		case RECV_PORT:
			add_wire(&env, test_node, i, proc_node, i);
			break;
		default:
			break;
		}
	}

	// Run
	run(&env);

	// Make sure *all* messages were sent and received
	for (size_t i = 0; i < PROC_PORTS; i++) {
		const struct test_port *port = &test->vm_test->ports[i];
		if (test->buf_idx[i] != port->buf_len) {
			errx(1, "Expected %lu messages to be passed through port %lu, "
			     "but instead found %lu.", port->buf_len, i,
				test->buf_idx[i]);
		}
	}

	// Clear runtime and go the merry way :)
	clear_runtime(&env);
}
