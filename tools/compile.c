/*
 * compile - compile a processor from stdin to bytecode.
 *
 * compile reads a single processor declaration from standard input
 * and writes binary bytecode to standard output. I've been pairing
 * this with disasm to spot-check compiled code
 *
 * see also tools/disasm.c.
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

// Number of errors before the tool dies.
static const size_t max_errors = 10;

static struct {
	size_t errors;
} Globals = {0};

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error("<stdin>", stdin, pos,
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

int main(int argc, char **argv)
{
	struct symdict dict = {0}; // A zero-valued symdict is an initialized symdict
	struct parser parser;

	// This program doesn't accept any arguments.
	if (argc != 1) {
		fprintf(stderr, "Usage: %s < INPUT\n", argv[0]);
		return 1;
	}

	// Parse a declaration
	init_parser(&parser, stdin, &dict);
	struct decl *decl = parse_decl(&parser);
	if (Globals.errors > 0)
		return 1;

	// Make sure it's a processor declaration
	if (decl->type != PROC_DECL) {
		fprintf(stderr, "Expected a processor\n");
		return 1;
	}

	// Check size required and compile.
	size_t code_size = bytecode_size(&decl->data.proc);
	if (Globals.errors > 0)
		return 1;

	uint8_t *code = ecalloc(code_size, sizeof(*code));
	compile(&decl->data.proc, code);
	if (Globals.errors > 0)
		return 1;

	// Write the compiled code to stdout
	fwrite(code, sizeof(*code), code_size, stdout);

	// Free our data structures
	free(code);
	free_decl(decl);
	clear_dict(&dict);

	// :)
	return 0;
}
