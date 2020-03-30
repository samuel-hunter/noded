#include <err.h>
#include <stdio.h>

#include "noded.h"
#include "vm-framework.h"

#define DEBUG stdout

static void send(uint8_t val, int porti, void *dat)
{
	struct vm_test *test = (struct vm_test *)dat;
	struct test_port *port = &test->ports[porti];

	fprintf(stdout, "%%%d <- %d\n", porti, val);

	if (port->send_idx == port->send_len)
		errx(1, "Too many messages from port %d "
		        "(expected only %lu messages)", porti, port->send_len);

	if (port->send[port->send_idx] != val)
		errx(1, "Expected %d as item #%lu from port %d, but received %d",
		     port->send[port->send_idx], port->send_idx, porti, val);
	port->send_idx++;
}

static uint8_t recv(int porti, void *dat)
{
	struct vm_test *test = (struct vm_test *)dat;
	struct test_port *port = &test->ports[porti];

	if (port->recv_idx == port->recv_len)
		errx(1, "Too many messages requested from port %d "
		        "(expected only %lu messages)", porti, port->recv_len);

	fprintf(DEBUG, "%d <- %%%d\n", port->recv[port->recv_idx], porti);

	uint8_t result = port->recv[port->recv_idx++];
	return result;
}

void test_code(struct vm_test *test)
{
	struct proc_node node;
	init_proc_node(&node, test->code, test->code_size, &send, &recv);
	run(&node, test);

	// Make sure *all* messages were sent and received
	for (size_t i = 0; i < PROC_PORTS; i++) {
		struct test_port *port = &test->ports[i];
		if (port->send_idx != port->send_len) {
			errx(1, "Expected %lu messages to be sent to port %lu, "
			     "but found only %lu.", port->send_len, i,
				port->send_idx);
		}

		if (port->recv_idx != port->recv_len) {
			errx(1, "Expected %lu messages to be received to "
			     "port %lu, but found only %lu.",
			     port->recv_len, i, port->recv_idx);
		}
	}

	clear_proc_node(&node);
}
