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
#include "bytecode.h"


static struct {
	// Source file
	FILE *f;
	const char *filename;

	size_t errors;
} Globals = {0};

// Number of errors before the parser panics.
static const size_t max_errors = 10;

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

static void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s FILE\n", prog_name);
}

static void handle_send(uint8_t val, int port, void *dat)
{
	// Route all SEND operations to stdout
	(void)port;
	(void)dat;

	putchar(val);
}

static uint8_t handle_recv(int port, void *dat)
{
	// Route all RECV operations to stdin
	(void)port;
	(void)dat;

	int chr = getchar();
	if (chr == EOF) {
		// No more input; halt.
		// TODO implement a way to trigger a halt in the VM.
		exit(0);
	}

	return (uint8_t)chr;
}

int main(int argc, char **argv)
{
	struct parser parser;
	struct decl *decl;
	size_t code_size;
	uint8_t *code;

	// Noded requires a file, it shouldn't just be stdin.
	if (argc != 2) {
		print_usage(argv[0]);
		return 1;
	}

	Globals.filename = argv[1];
	Globals.f = fopen(Globals.filename, "r");
	if (Globals.f == NULL)
		err(1, "Cannot open %s", Globals.filename);

	// Scan the file token-by-token
	init_parser(&parser, Globals.f);
	decl = parse_decl(&parser);
	clear_parser(&parser);

	if (has_errors())
		return 1;

	if (decl->type != PROC_DECL) {
		fprintf(stderr, "Expected proc decl\n");
		return 1;
	}

	errno = 0; // Reset errno
	code_size = bytecode_size(&decl->data.proc);
	if (errno == ERANGE) {
		send_error(&decl->data.proc.start, FATAL,
			"Node is too complex; the compiled bytecode is not "
			"within a 16-bit range");
	}

	code = ecalloc(code_size, sizeof(*code));
	compile(&decl->data.proc, code);

	if (has_errors())
		return 1;

	// free the AST and parser, since we no longer need it.
	free_decl(decl);
	clear_parser(&parser);
	fclose(Globals.f);

	// Give ownership of code to proc node.
	struct proc_node *node = new_proc_node(code, code_size,
		&handle_send, &handle_recv);
	run(node, NULL);
	free_proc_node(node);

	return 0;
}
