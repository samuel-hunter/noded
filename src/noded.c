/*
 * noded - noded interpreter
 *
 * noded currently assumes the first declaration to be a processor,
 * compiles it, and runs the program. All SEND ops are sent as
 * characters to stdout, and all RECV ops are read from stdin.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include "noded.h"
#include "ast.h"
#include "vm.h"


static struct {
	// Source file
	FILE *f;
	const char *filename;

	size_t errors;
} Globals = {0};

// Number of errors before the parser panics.
static const size_t max_errors = 10;

// Return the port number from its id, or -1 if there is no port.
static int id_port(size_t port_ids[], size_t id)
{
	for (int i = 0; i < PROC_PORTS; i++) {
		if (id == port_ids[i]) return i;
	}

	return -1;
}

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error(Globals.filename, Globals.f, pos,
		type, fmt, ap);
	va_end(ap);

	// Send an exit depending on fatality.
	switch (type) {
	case WARN:
		break;
	case ERR:
		if (++Globals.errors > max_errors)
			errx(1, "Too many errors.");
		break;
	case FATAL:
		exit(1);
		break;
	}
}

static bool has_errors(void)
{
	return Globals.errors > 0;
}

int main(int argc, char **argv)
{
	struct symdict dict = {0}; // a zero-value dict is freshly initialized.
	struct runtime env = {0}; // a zero-value runtime is freshly initialized.
	struct parser parser;
	struct decl *decl;
	struct node *io_node, *proc_node;
	size_t port_ids[PROC_PORTS];
	size_t code_size;
	uint8_t *code;
	int porti;

	if (argc != 2) {
		// Noded requires a file argument
		fprintf(stderr, "Usage: %s FILE\n", argv[0]);
		return 1;
	}

	// Open the file
	Globals.filename = argv[1];
	Globals.f = fopen(Globals.filename, "r");
	if (Globals.f == NULL)
		err(1, "Cannot open %s", Globals.filename);

	// Grab a declaration (hopefully a processor decl)
	init_parser(&parser, Globals.f, &dict);
	decl = parse_decl(&parser);
	if (has_errors())
		return 1;

	if (decl->type != PROC_DECL) {
		fprintf(stderr, "Expected proc decl\n");
		return 1;
	}

	// Compile the processor from its declaration
	code_size = bytecode_size(&decl->data.proc);
	if (has_errors()) // check errors here because we'll be
		return 1; // allocating memory that needs to have a
			  // reliable return value.

	code = ecalloc(code_size, sizeof(*code));
	compile(&decl->data.proc, code, port_ids, NULL);

	// Add the proc and io nodes.
	io_node = add_io_node(&env);
	proc_node = add_proc_node(&env, code, code_size);

	// Manually route all the wires here.
	// Todo: implement the routing module to route for us.
	if (porti = id_port(port_ids, sym_id(&dict, "in")), porti >= 0) {
		struct proc_node *proc = proc_node->dat;
		struct io_node *io = io_node->dat;
		struct wire *wire = add_wire(&env);

		proc->wires[porti] = wire;
		io->in_wire = wire;
	}

	if (porti = id_port(port_ids, sym_id(&dict, "out")), porti >= 0) {
		struct proc_node *proc = proc_node->dat;
		struct io_node *io = io_node->dat;
		struct wire *wire = add_wire(&env);

		proc->wires[porti] = wire;
		io->out_wire = wire;
	}

	// free the dict and AST, since we no longer need it.
	free_decl(decl);
	clear_dict(&dict);
	fclose(Globals.f);

	// Run, clear, and exit.
	run(&env);
	clear_runtime(&env);
	free(code);

	return 0;
}
