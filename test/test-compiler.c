#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

struct compiler_test {
	const char *name;
	const char *src;

	struct test_port {
		const uint8_t *send;
		size_t send_len;
		size_t send_idx;

		const uint8_t *recv;
		size_t recv_len;
		size_t recv_idx;
	} ports[PROC_PORTS];
};

static struct {
	bool has_error;
	const struct compiler_test *cur;
} Globals = {0};

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error(Globals.cur->name, Globals.cur->src, pos,
		type, fmt, ap);
	va_end(ap);

	// Send an exit if fatal.
	if (type == FATAL)
		exit(1);
}

bool has_errors(void)
{
	return Globals.has_error;
}

static void send(uint8_t val, int porti, void *dat)
{
	struct compiler_test *test = (struct compiler_test *)dat;
	struct test_port *port = &test->ports[porti];

	printf("Port%d -> %d\n", porti, val);

	if (port->send_idx == port->send_len)
		errx(1, "Too many messages from port %d "
		        "(expected only %lu messages)", porti, port->send_len);

	if (port->send[port->send_idx++] != val)
		errx(1, "Expected %d from port %d, but received %d",
		     port->send[port->send_idx-1], porti, val);
}

static uint8_t recv(int porti, void *dat)
{
	struct compiler_test *test = (struct compiler_test *)dat;
	struct test_port *port = &test->ports[porti];

	if (port->recv_idx == port->recv_len)
		errx(1, "Too many messages requested from port %d "
		        "(expected only %lu messages)", porti, port->recv_len);

	uint8_t result = port->recv[port->recv_idx++];
	printf("Port%d <- %d\n", porti, result);
	return result;
}

static void test_compiler(struct compiler_test *test)
{
	struct parser parser;
	struct decl *decl;
	uint16_t code_size;
	uint8_t *code;

	Globals.cur = test;

	printf("Running %s...\n", test->name);

	init_parser(&parser, test->src, strlen(test->src));
	decl = parse_decl(&parser);
	if (has_errors())
		exit(1);

	if (decl->type != PROC_DECL) {
		fprintf(stderr, "Expected proc decl\n");
		exit(1);
	}

	code = compile(&decl->data.proc, &code_size);
	if (has_errors() || code == NULL) {
		exit(1);
	}

	struct proc_node *node =
		new_proc_node(code, code_size, &send, &recv);
	run(node, test);

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

	// Free all constructs.
	free_proc_node(node);
	free(code);
	free_decl(decl);

	printf("OK\n\n");
}

int main(void)
{
	uint8_t test_if_1s[] = {20};
	struct compiler_test test_if = {
		.name = "If Test",
		.src = "processor test {"
		"	if (10) {"
		"		%out <- 20;"
		"	}"
		"	halt;"
		"}",
		.ports = {
			{
				.send = test_if_1s,
				.send_len = sizeof(test_if_1s)/
				            sizeof(*test_if_1s)
			}
		}
	};
	test_compiler(&test_if);

	printf("Looks good!\n");
	return 0;
}
